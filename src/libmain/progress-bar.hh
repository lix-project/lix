#pragma once
///@file

#include <chrono>

#include "logging.hh"
#include "sync.hh"

namespace nix {

// 100 years ought to be enough for anyone (yet sufficiently smaller than max() to not cause signed integer overflow).
constexpr const auto A_LONG_TIME = std::chrono::duration_cast<std::chrono::milliseconds>(
    100 * 365 * std::chrono::seconds(86400)
);

struct ProgressBar : public Logger
{
    struct ActInfo
    {
        using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

        std::string s, lastLine, phase;
        ActivityType type = actUnknown;
        uint64_t done = 0;
        uint64_t expected = 0;
        uint64_t running = 0;
        uint64_t failed = 0;
        std::map<ActivityType, uint64_t> expectedByType;
        bool visible = true;
        ActivityId parent;
        std::optional<std::string> name;
        TimePoint startTime;
    };

    struct ActivitiesByType
    {
        std::map<ActivityId, std::list<ActInfo>::iterator> its;
        uint64_t done = 0;
        uint64_t expected = 0;
        uint64_t failed = 0;
    };

    struct State
    {
        std::list<ActInfo> activities;
        std::map<ActivityId, std::list<ActInfo>::iterator> its;

        std::map<ActivityType, ActivitiesByType> activitiesByType;

        int lastLines = 0;

        uint64_t filesLinked = 0, bytesLinked = 0;

        uint64_t corruptedPaths = 0, untrustedPaths = 0;

        bool active = true;
        bool paused = false;
        bool haveUpdate = true;
    };

    Sync<State> state_;

    std::thread updateThread;

    std::condition_variable quitCV, updateCV;

    bool printBuildLogs = false;
    bool printMultiline = false;
    bool isTTY;

    ProgressBar(bool isTTY)
        : isTTY(isTTY)
    {
        state_.lock()->active = isTTY;
        updateThread = std::thread([&]() {
            auto state(state_.lock());
            auto nextWakeup = A_LONG_TIME;
            while (state->active) {
                if (!state->haveUpdate)
                    state.wait_for(updateCV, nextWakeup);
                nextWakeup = draw(*state, {});
                state.wait_for(quitCV, std::chrono::milliseconds(50));
            }
        });
    }

    ~ProgressBar();

    void stop() override final;

    void pause() override;

    void resume() override;

    bool isVerbose() override;

    void log(Verbosity lvl, std::string_view s) override;

    void logEI(const ErrorInfo & ei) override;

    void log(State & state, Verbosity lvl, std::string_view s);

    void startActivity(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        const Fields & fields,
        ActivityId parent
    ) override;

    bool hasAncestor(State & state, ActivityType type, ActivityId act);

    void stopActivity(ActivityId act) override;

    void result(ActivityId act, ResultType type, const std::vector<Field> & fields) override;

    void update(State & state);

    std::chrono::milliseconds draw(State & state, const std::optional<std::string_view> & s);

    std::string getStatus(State & state);

    void writeToStdout(std::string_view s) override;

    std::optional<char> ask(std::string_view msg) override;

    void setPrintBuildLogs(bool printBuildLogs) override;

    void setPrintMultiline(bool printMultiline) override;
};

Logger * makeProgressBar();

void startProgressBar();

void stopProgressBar();

}
