#pragma once
///@file

#include <functional>
#include <sched.h>
#include <span>
#include <string>

#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/processes.hh"
#include "tests/terminal-code-eater.hh"

namespace nix {

struct RunningProcess
{
    Pid pid;
    Pipe procStdin;
    Pipe procStdout;

    static RunningProcess start(std::string executable, Strings args);
};

/** DFA that catches repl prompts */
class ReplOutputParser
{
public:
    ReplOutputParser(std::string prompt) : prompt(prompt)
    {
        assert(!prompt.empty());
    }
    /** Feeds in a character and returns whether this is an open prompt */
    bool feed(char c);

    enum class State {
        Prompt,
        Context,
    };

private:
    State state = State::Prompt;
    size_t pos_in_prompt = 0;
    std::string const prompt;

    void transition(State state, char responsible_char, bool wasPrompt = false);
};

struct TestSession
{
    RunningProcess proc;
    ReplOutputParser outputParser;
    TerminalCodeEater eater;
    std::string outLog;
    std::string prompt;

    TestSession(std::string prompt, RunningProcess && proc)
        : proc(std::move(proc))
        , outputParser(prompt)
        , eater{}
        , outLog{}
        , prompt(prompt)
    {
    }

    /** Waits for the prompt and then returns if a prompt was found */
    bool waitForPrompt();

    /** Feeds a line of input into the command */
    void runCommand(std::string command);

    /** Closes the session, closing standard input and waiting for standard
     * output to close, capturing any remaining output. */
    void close();

private:
    /** Waits until the command closes its output */
    void wait();

    enum class ReadOutThenCallbackResult { Stop, Continue };
    using ReadOutThenCallback = std::function<ReadOutThenCallbackResult(std::span<const char>)>;
    /** Reads some chunks of output, calling the callback provided for each
     * chunk and stopping if it returns Stop.
     *
     * @returns false if EOF, true if the callback requested we stop first.
     * */
    bool readOutThen(ReadOutThenCallback cb);
};
};
