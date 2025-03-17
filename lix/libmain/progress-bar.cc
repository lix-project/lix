#include "lix/libmain/progress-bar.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/sync.hh"
#include "lix/libstore/names.hh"
#include "lix/libutil/terminal.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/thread-name.hh"

#include <map>
#include <thread>
#include <sstream>
#include <iostream>
#include <chrono>

namespace nix {

// 100 years ought to be enough for anyone (yet sufficiently smaller than max() to not cause signed integer overflow).
constexpr const auto A_LONG_TIME = std::chrono::duration_cast<std::chrono::milliseconds>(
    100 * 365 * std::chrono::seconds(86400)
);

using namespace std::literals::chrono_literals;

static std::string_view getS(const std::vector<Logger::Field> & fields, size_t n)
{
    assert(n < fields.size());
    assert(fields[n].type == Logger::Field::tString);
    return fields[n].s;
}

static uint64_t getI(const std::vector<Logger::Field> & fields, size_t n)
{
    assert(n < fields.size());
    assert(fields[n].type == Logger::Field::tInt);
    return fields[n].i;
}

static std::string_view storePathToName(std::string_view path)
{
    auto base = baseNameOf(path);
    auto i = base.find('-');
    return i == std::string::npos ? base.substr(0, 0) : base.substr(i + 1);
}

ProgressBar::ProgressBar(bool isTTY)
    : isTTY(isTTY)
{
    resume();
}

ProgressBar::~ProgressBar()
{
    pause();
}

void ProgressBar::pause()
{
    if (!isTTY) return;
    {
        auto state(state_.lock());
        state->paused++;
        if (state->paused > 1) return; // recursive pause, the update thread is already gone
        updateCV.notify_one();
        quitCV.notify_one();
    }
    updateThread.join();
}

void ProgressBar::resetProgress()
{
    auto state(state_.lock());
    auto prevPaused = state->paused;
    *state = ProgressBar::State {
        .paused = prevPaused,
    };
    update(*state);
}

void ProgressBar::resume()
{
    if (!isTTY) return;
    auto state(state_.lock());
    assert(state->paused > 0); // should be paused
    state->paused--;
    if (state->paused > 0) return; // recursive pause, wait for the parents to resume too
    state->haveUpdate = true;
    updateThread = std::thread([&]() {
        setCurrentThreadName("progress bar");
        auto state(state_.lock());
        auto nextWakeup = A_LONG_TIME;
        while (state->paused == 0) {
            if (!state->haveUpdate)
                state.wait_for(updateCV, nextWakeup);
            eraseProgressDisplay(*state);
            nextWakeup = restoreProgressDisplay(*state);
            state.wait_for(quitCV, std::chrono::milliseconds(50));
        }
        eraseProgressDisplay(*state);
    });
}

bool ProgressBar::isVerbose()
{
    return printBuildLogs;
}

void ProgressBar::log(Verbosity lvl, std::string_view s)
{
    if (lvl > verbosity) return;
    auto state(state_.lock());
    log(*state, lvl, s);
}

void ProgressBar::logEI(const ErrorInfo & ei)
{
    auto state(state_.lock());

    std::stringstream oss;
    showErrorInfo(oss, ei, loggerSettings.showTrace.get());

    log(*state, ei.level, oss.str());
}

void ProgressBar::log(State & state, Verbosity lvl, std::string_view s)
{
    if (state.paused == 0) eraseProgressDisplay(state);
    writeLogsToStderr(filterANSIEscapes(s + ANSI_NORMAL "\n", !isTTY));
    restoreProgressDisplay(state);
}

void ProgressBar::startActivity(
    ActivityId act,
    Verbosity lvl,
    ActivityType type,
    const std::string & s,
    const Fields & fields,
    ActivityId parent
)
{
    auto state(state_.lock());

    if (lvl <= verbosity && !s.empty() && type != actBuildWaiting)
        log(*state, lvl, s + "...");

    state->activities.emplace_back(ActInfo {
        .s = s,
        .type = type,
        .parent = parent,
        .startTime = std::chrono::steady_clock::now()
    });
    auto i = std::prev(state->activities.end());
    state->its.emplace(act, i);
    state->activitiesByType[type].its.emplace(act, i);

    if (type == actBuild) {
        std::string name(storePathToName(getS(fields, 0)));
        if (name.ends_with(".drv"))
            name = name.substr(0, name.size() - 4);
        i->s = fmt("building " ANSI_BOLD "%s" ANSI_NORMAL, name);
        auto machineName = getS(fields, 1);
        if (machineName != "")
            i->s += fmt(" on " ANSI_BOLD "%s" ANSI_NORMAL, machineName);

        // Used to be curRound and nrRounds, but the
        // implementation was broken for a long time.
        if (getI(fields, 2) != 1 || getI(fields, 3) != 1) {
            throw Error("log message indicated repeating builds, but this is not currently implemented");
        }
        i->name = DrvName(name).name;
    }

    if (type == actSubstitute) {
        auto name = storePathToName(getS(fields, 0));
        auto sub = getS(fields, 1);
        i->s = fmt(
            sub.starts_with("local")
            ? "copying " ANSI_BOLD "%s" ANSI_NORMAL " from %s"
            : "fetching " ANSI_BOLD "%s" ANSI_NORMAL " from %s",
            name, sub);
    }

    if (type == actPostBuildHook) {
        auto name = storePathToName(getS(fields, 0));
        if (name.ends_with(".drv"))
            name = name.substr(0, name.size() - 4);
        i->s = fmt("post-build " ANSI_BOLD "%s" ANSI_NORMAL, name);
        i->name = DrvName(name).name;
    }

    if (type == actQueryPathInfo) {
        auto name = storePathToName(getS(fields, 0));
        i->s = fmt("querying " ANSI_BOLD "%s" ANSI_NORMAL " on %s", name, getS(fields, 1));
    }

    if ((type == actFileTransfer && hasAncestor(*state, actCopyPath, parent))
        || (type == actFileTransfer && hasAncestor(*state, actQueryPathInfo, parent))
        || (type == actCopyPath && hasAncestor(*state, actSubstitute, parent)))
        i->visible = false;

    update(*state);
}

/* Check whether an activity has an ancestore with the specified
   type. */
bool ProgressBar::hasAncestor(State & state, ActivityType type, ActivityId act)
{
    while (act != 0) {
        auto i = state.its.find(act);
        if (i == state.its.end()) break;
        if (i->second->type == type) return true;
        act = i->second->parent;
    }
    return false;
}

void ProgressBar::stopActivity(ActivityId act)
{
    auto state(state_.lock());

    auto i = state->its.find(act);
    if (i != state->its.end()) {

        auto & actByType = state->activitiesByType[i->second->type];
        actByType.done += i->second->done;
        actByType.failed += i->second->failed;

        for (auto & j : i->second->expectedByType)
            state->activitiesByType[j.first].expected -= j.second;

        actByType.its.erase(act);
        state->activities.erase(i->second);
        state->its.erase(i);
    }

    update(*state);
}

void ProgressBar::result(ActivityId act, ResultType type, const std::vector<Field> & fields)
{
    auto state(state_.lock());

    if (type == resFileLinked) {
        state->filesLinked++;
        state->bytesLinked += getI(fields, 0);
        update(*state);
    }

    else if (type == resBuildLogLine || type == resPostBuildLogLine) {
        auto lastLine = chomp(getS(fields, 0));
        if (!lastLine.empty()) {
            auto i = state->its.find(act);
            assert(i != state->its.end());
            ActInfo info = *i->second;
            if (printBuildLogs || type == resPostBuildLogLine) {
                auto suffix = "> ";
                if (type == resPostBuildLogLine) {
                    suffix = " (post)> ";
                }
                log(*state, lvlInfo, ANSI_FAINT + info.name.value_or("unnamed") + suffix + ANSI_NORMAL + lastLine);
            } else {
                if (!printMultiline) {
                    state->activities.erase(i->second);
                    info.lastLine = lastLine;
                    state->activities.emplace_back(info);
                    i->second = std::prev(state->activities.end());
                } else {
                    i->second->lastLine = lastLine;
                }
                update(*state);
            }
        }
    }

    else if (type == resUntrustedPath) {
        state->untrustedPaths++;
        update(*state);
    }

    else if (type == resCorruptedPath) {
        state->corruptedPaths++;
        update(*state);
    }

    else if (type == resSetPhase) {
        auto i = state->its.find(act);
        assert(i != state->its.end());
        i->second->phase = getS(fields, 0);
        update(*state);
    }

    else if (type == resProgress) {
        auto i = state->its.find(act);
        assert(i != state->its.end());
        ActInfo & actInfo = *i->second;
        actInfo.done = getI(fields, 0);
        actInfo.expected = getI(fields, 1);
        actInfo.running = getI(fields, 2);
        actInfo.failed = getI(fields, 3);
        update(*state);
    }

    else if (type == resSetExpected) {
        auto i = state->its.find(act);
        assert(i != state->its.end());
        ActInfo & actInfo = *i->second;
        auto type = (ActivityType) getI(fields, 0);
        auto & j = actInfo.expectedByType[type];
        state->activitiesByType[type].expected -= j;
        j = getI(fields, 1);
        state->activitiesByType[type].expected += j;
        update(*state);
    }
}

void ProgressBar::update(State & state)
{
    state.haveUpdate = true;
    updateCV.notify_one();
}

void ProgressBar::eraseProgressDisplay(State & state)
{
    if (state.paused == 0) {
        writeLogsToStderr("\e[?2026h"); // begin synchronized update
    }
    if (printMultiline && (state.lastLines >= 1)) {
        // FIXME: make sure this works on windows
        writeLogsToStderr(fmt("\e[G\e[%dF\e[J", state.lastLines));
    } else {
        writeLogsToStderr("\r\e[K");
    }
}

std::chrono::milliseconds ProgressBar::restoreProgressDisplay(State & state)
{
    auto nextWakeup = A_LONG_TIME;

    state.haveUpdate = false;
    if (state.paused > 0) return nextWakeup; // when paused, the progress display should not actually be shown

    auto windowSize = getWindowSize();
    auto width = windowSize.second;
    if (width <= 0) {
        width = std::numeric_limits<decltype(width)>::max();
    }

    state.lastLines = 0;

    std::string line;
    std::string status = getStatus(state);
    if (!status.empty()) {
        line += '[';
        line += status;
        line += "]";
    }
    if (printMultiline && !line.empty()) {
        writeLogsToStderr(filterANSIEscapes(line, false, width) + ANSI_NORMAL "\n");
        state.lastLines++;
    }

    auto height = windowSize.first > 0 ? windowSize.first : 25;
    auto moreActivities = 0;
    auto now = std::chrono::steady_clock::now();

    std::string activity_line;
    if (!state.activities.empty()) {
        for (auto i = state.activities.begin(); i != state.activities.end(); ++i) {
            if (!(i->visible && (!i->s.empty() || !i->lastLine.empty()))) {
                continue;
            }
            /* Don't show activities until some time has
               passed, to avoid displaying very short
               activities. */
            auto delay = std::chrono::milliseconds(10);
            if (i->startTime + delay >= now) {
                nextWakeup = std::min(
                    nextWakeup,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        delay - (now - i->startTime)
                        )
                    );
                continue;
            }

            activity_line = i->s;

            if (!i->phase.empty()) {
                activity_line += " (";
                activity_line += i->phase;
                activity_line += ")";
            }
            if (!i->lastLine.empty()) {
                if (!i->s.empty())
                    activity_line += ": ";
                activity_line += i->lastLine;
            }

            if (printMultiline) {
                if (state.lastLines < (height -1)) {
                    writeLogsToStderr(filterANSIEscapes(activity_line, false, width) + ANSI_NORMAL "\n");
                    state.lastLines++;
                } else moreActivities++;
            }
        }
    }

    if (printMultiline && moreActivities)
        writeLogsToStderr(fmt("And %d more...", moreActivities));

    if (!printMultiline) {
        if (!line.empty()) {
            line += " ";
        }
        line += activity_line;
        if (!line.empty()) {
            writeLogsToStderr(filterANSIEscapes(line, false, width) + ANSI_NORMAL);
        }
    }

    writeLogsToStderr("\e[?2026l"); // end synchronized update

    return nextWakeup;
}

std::string ProgressBar::getStatus(State & state)
{
    constexpr auto MiB = 1024.0 * 1024.0;

    std::string res;

    auto renderActivity = [&](ActivityType type, const std::string & itemFmt, const std::string & numberFmt = "%d", double unit = 1) {
        auto & act = state.activitiesByType[type];
        uint64_t done = act.done, expected = act.done, running = 0, failed = act.failed;
        for (auto & [actId, infoIt] : act.its) {
            done += infoIt->done;
            expected += infoIt->expected;
            running += infoIt->running;
            failed += infoIt->failed;
        }

        expected = std::max(expected, act.expected);

        std::string rendered;

        if (running || done || expected || failed) {
            if (running) {
                if (expected != 0) {
                    auto const runningPart = fmt(numberFmt, running / unit);
                    auto const donePart = fmt(numberFmt, done / unit);
                    auto const expectedPart = fmt(numberFmt, expected / unit);
                    rendered = fmt(
                        ANSI_BLUE "%s" ANSI_NORMAL "/" ANSI_GREEN "%s" ANSI_NORMAL "/%s",
                        runningPart,
                        donePart,
                        expectedPart
                    );
                } else {
                    auto const runningPart = fmt(numberFmt, running / unit);
                    auto const donePart = fmt(numberFmt, done / unit);
                    rendered = fmt(
                        ANSI_BLUE "%s" ANSI_NORMAL "/" ANSI_GREEN "%s" ANSI_NORMAL,
                        runningPart,
                        donePart
                    );
                }
            } else if (expected != done) {
                if (expected != 0) {
                    auto const donePart = fmt(numberFmt, done / unit);
                    auto const expectedPart = fmt(numberFmt, expected / unit);
                    rendered = fmt(
                        ANSI_GREEN "%s" ANSI_NORMAL "/%s",
                        donePart,
                        expectedPart
                    );
                } else {
                    auto const donePart = fmt(numberFmt, done / unit);
                    rendered = fmt(ANSI_GREEN "%s" ANSI_NORMAL, donePart);
                }
            } else {
                auto const donePart = fmt(numberFmt, done / unit);

                // We only color if `done` is non-zero.
                if (done) {
                    rendered = concatStrings(ANSI_GREEN, donePart, ANSI_NORMAL);
                } else {
                    rendered = donePart;
                }
            }
            rendered = fmt(itemFmt, rendered);

            if (failed)
                rendered += fmt(" (" ANSI_RED "%d failed" ANSI_NORMAL ")", failed / unit);
        }

        return rendered;
    };

    auto showActivity = [&](ActivityType type, const std::string & itemFmt, const std::string & numberFmt = "%d", double unit = 1) {
        auto s = renderActivity(type, itemFmt, numberFmt, unit);
        if (s.empty()) return;
        if (!res.empty()) res += ", ";
        res += s;
    };

    showActivity(actBuilds, "%s built");

    auto s1 = renderActivity(actCopyPaths, "%s copied");
    auto s2 = renderActivity(actCopyPath, "%s MiB", "%.1f", MiB);

    if (!s1.empty() || !s2.empty()) {
        if (!res.empty()) res += ", ";
        if (s1.empty()) res += "0 copied"; else res += s1;
        if (!s2.empty()) { res += " ("; res += s2; res += ')'; }
    }

    showActivity(actFileTransfer, "%s MiB DL", "%.1f", MiB);

    {
        auto s = renderActivity(actOptimiseStore, "%s paths optimised");
        if (s != "") {
            s += fmt(", %.1f MiB / %d inodes freed", state.bytesLinked / MiB, state.filesLinked);
            if (!res.empty()) res += ", ";
            res += s;
        }
    }

    // FIXME: don't show "done" paths in green.
    showActivity(actVerifyPaths, "%s paths verified");

    if (state.corruptedPaths) {
        if (!res.empty()) res += ", ";
        res += fmt(ANSI_RED "%d corrupted" ANSI_NORMAL, state.corruptedPaths);
    }

    if (state.untrustedPaths) {
        if (!res.empty()) res += ", ";
        res += fmt(ANSI_RED "%d untrusted" ANSI_NORMAL, state.untrustedPaths);
    }

    return res;
}

void ProgressBar::writeToStdout(std::string_view s)
{
    auto state(state_.lock());
    if (state->paused == 0) eraseProgressDisplay(*state);
    Logger::writeToStdout(s);
    restoreProgressDisplay(*state);
}

std::optional<char> ProgressBar::ask(std::string_view msg)
{
    auto state(state_.lock());
    if (state->paused > 0 || !isatty(STDIN_FILENO)) return {};
    eraseProgressDisplay(*state);
    writeLogsToStderr("\e[?2026l"); // end synchronized update
    std::cerr << msg;
    auto s = trim(readLine(STDIN_FILENO));
    writeLogsToStderr("\e[?2026h"); // begin synchronized update
    if (s.size() != 1) return {};
    restoreProgressDisplay(*state);
    return s[0];
}

void ProgressBar::setPrintBuildLogs(bool printBuildLogs)
{
    this->printBuildLogs = printBuildLogs;
}

void ProgressBar::setPrintMultiline(bool printMultiline)
{
    this->printMultiline = printMultiline;
}

Logger * makeProgressBar()
{
    return new ProgressBar(shouldANSI());
}

}
