#include "lix/libutil/error.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/logging.hh"
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>
#include <cerrno>

// editline < 1.15.2 don't wrap their API for C++ usage
// (added in https://github.com/troglobit/editline/commit/91398ceb3427b730995357e9d120539fb9bb7461).
// This results in linker errors due to to name-mangling of editline C symbols.
// For compatibility with these versions, we wrap the API here
// (wrapping multiple times on newer versions is no problem).
extern "C" {
#include <editline.h>
}

#include "lix/libutil/finally.hh"
#include "lix/libutil/strings.hh"
#include "lix/libcmd/repl-interacter.hh"

namespace nix {

namespace {
// Used to communicate to NixRepl::getLine whether a signal occurred in ::readline.
volatile sig_atomic_t g_signal_received = 0;

void sigintHandler(int signo)
{
    g_signal_received = signo;
}

static detail::ReplCompleterMixin * curRepl; // ugly

/**
 *  @return a null-terminated list of completions as expected by `el_print_columns`
 */
char ** copyCompletions(const StringSet& possible)
{
    int ac = 0;
    char ** vp = nullptr;

    auto check = [&](auto * p) {
        if (!p) {
            if (vp) {
                while (--ac >= 0)
                    free(vp[ac]);
                free(vp);
            }
            throw Error("allocation failure");
        }
        return p;
    };
    vp = check(static_cast<char **>(malloc(possible.size() * sizeof(char *))));
    for (auto & p : possible)
        vp[ac++] = check(strdup(p.c_str()));

    return vp;
}

// Instead of using the readline-provided prefix, do our own tokenization
// to avoid the default behavior of treating dots/quotes as word boundaries.
// See the definition of SEPS for what it treats as a boundary:
// https://github.com/troglobit/editline/blob/caf4b3c0ce3b0785791198b11de6f3134e9f05d8/src/editline.c
std::string getLastTokenBeforeCursor()
{
    std::string_view line{rl_line_buffer, static_cast<size_t>(rl_point)};

    auto tokens = tokenizeString<std::vector<std::string>>(
        line,
        // Same as editline's SEPS, except for double and single quotes:
        "#$&()*:;<=>?[\\]^`{|}~\n\t "
    );

    if (tokens.empty()) {
        return "";
    }

    return tokens.back();
}

// Sometimes inserting text or listing possible completions has a side effect
// of hiding the text after the cursor (even though it remains in the buffer).
// This helper just refreshes the display while keeping the cursor in place.
//
// Inserting text also sometimes moves the whole buffer down one line, usually
// if the cursor is inside a quoted attr name. I'm not sure why (vs unquoted)
// but it still seems to work pretty well and is just a visual artifact.
el_status_t redisplay()
{
    int cursorPos = rl_point;
    rl_refresh_line(0, 0);
    rl_point = cursorPos;
    return (rl_point == rl_end) ? CSstay : CSmove;
}

};


static el_status_t doCompletion() {
    auto s = getLastTokenBeforeCursor();
    auto possible = curRepl->completePrefix(s);

    if (possible.empty()) {
        return el_ring_bell();
    }

    if (possible.size() == 1) {
        const auto completion = *possible.cbegin();
        if (completion.size() > s.size()) {
            rl_insert_text(completion.c_str() + s.size());
            return redisplay();
        }

        return el_ring_bell();
    }

    auto checkAllHaveSameAt = [&](size_t pos) {
        auto & first = *possible.begin();
        for (auto & p : possible) {
            if (p.size() <= pos || p[pos] != first[pos]) {
                return false;
            }
        }
        return true;
    };

    size_t start = s.size();
    size_t len = 0;
    while (checkAllHaveSameAt(start + len)) {
        ++len;
    }
    if (len > 0) {
        auto commonPrefix = possible.begin()->substr(start, len);
        rl_insert_text(commonPrefix.c_str());
        el_ring_bell();
        return redisplay();
    }

    char** columns = copyCompletions(possible);
    el_print_columns(possible.size(), columns);
    return redisplay();
}

ReadlineLikeInteracter::Guard ReadlineLikeInteracter::init(detail::ReplCompleterMixin * repl)
{
    // Allow nix-repl specific settings in .inputrc
    rl_readline_name = "nix-repl";
    try {
        createDirs(dirOf(historyFile));
    } catch (SysError & e) {
        logWarning(e.info());
    }
    el_hist_size = 1000;
    read_history(historyFile.c_str());
    auto oldRepl = curRepl;
    curRepl = repl;
    Guard restoreRepl([oldRepl] { curRepl = oldRepl; });
    // editline does its own escaping of completions, so we rebind tab
    // to our own completion function to skip that and do nix escaping
    // instead of shell escaping.
    el_bind_key(CTL('I'), doCompletion);
    return restoreRepl;
}

static constexpr const char * promptForType(ReplPromptType promptType)
{
    switch (promptType) {
    case ReplPromptType::ReplPrompt:
        return "nix-repl> ";
    case ReplPromptType::ContinuationPrompt:
        return "          ";
    }
    assert(false);
}

bool ReadlineLikeInteracter::getLine(std::string & input, ReplPromptType promptType)
{
    struct sigaction act, old;
    sigset_t savedSignalMask, set;

    auto setupSignals = [&]() {
        act.sa_handler = sigintHandler;
        sigfillset(&act.sa_mask);
        act.sa_flags = 0;
        if (sigaction(SIGINT, &act, &old))
            throw SysError("installing handler for SIGINT");

        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        if (sigprocmask(SIG_UNBLOCK, &set, &savedSignalMask))
            throw SysError("unblocking SIGINT");
    };
    auto restoreSignals = [&]() {
        if (sigprocmask(SIG_SETMASK, &savedSignalMask, nullptr))
            throw SysError("restoring signals");

        if (sigaction(SIGINT, &old, 0))
            throw SysError("restoring handler for SIGINT");
    };

    setupSignals();
    char * s = readline(promptForType(promptType));
    Finally doFree([&]() { free(s); });
    restoreSignals();

    if (g_signal_received) {
        g_signal_received = 0;
        input.clear();
        return true;
    }

    if (!s)
        return false;

    this->writeHistory();
    input += s;
    input += '\n';
    return true;
}

void ReadlineLikeInteracter::writeHistory()
{
    int ret = write_history(historyFile.c_str());
    int writeHistErr = errno;

    if (ret == 0) {
        return;
    }

    // If the open fails, editline returns EOF. If the close fails, editline
    // forwards the return value of fclose(), which is EOF on error.
    // readline however, returns the errno.
    // So if we didn't get exactly EOF, then consider the return value the error
    // code; otherwise use the errno we saved above.
    // https://github.com/troglobit/editline/issues/66
    if (ret != EOF) {
        writeHistErr = ret;
    }

    // In any of these cases, we should explicitly ignore the error, but log
    // them so the user isn't confused why their history is getting eaten.

    std::string_view const errMsg(std::strerror(writeHistErr));
    printTaggedWarning("ignoring error writing repl history to %s: %s", this->historyFile, errMsg);
}

ReadlineLikeInteracter::~ReadlineLikeInteracter()
{
    this->writeHistory();
}

AutomationInteracter::Guard AutomationInteracter::init(detail::ReplCompleterMixin *)
{
    return Guard([] {});
}

// ASCII ENQ character
constexpr const char * automationPrompt = "\x05";

bool AutomationInteracter::getLine(std::string & input, ReplPromptType promptType)
{
    std::cout << std::unitbuf;
    std::cout << automationPrompt;
    if (!std::getline(std::cin, input)) {
        // reset failure bits on EOF
        std::cin.clear();
        return false;
    }
    return true;
}

};
