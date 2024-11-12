#include <iostream>
#include <span>
#include <unistd.h>

#include "test-session.hh"
#include "lix/libutil/escape-char.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/strings.hh"

namespace nix {

static constexpr const bool DEBUG_REPL_PARSER = false;

RunningProcess RunningProcess::start(std::string executable, Strings args)
{
    args.push_front(executable);

    Pipe procStdin{};
    Pipe procStdout{};

    procStdin.create();
    procStdout.create();

    // This is separate from runProgram2 because we have different IO requirements
    auto pid = startProcess([&]() {
        if (dup2(procStdout.writeSide.get(), STDOUT_FILENO) == -1) {
            throw SysError("dupping stdout");
        }
        if (dup2(procStdin.readSide.get(), STDIN_FILENO) == -1) {
            throw SysError("dupping stdin");
        }
        procStdin.writeSide.close();
        procStdout.readSide.close();
        if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1) {
            throw SysError("dupping stderr");
        }
        execv(executable.c_str(), stringsToCharPtrs(args).data());
        throw SysError("exec did not happen");
    });

    procStdout.writeSide.close();
    procStdin.readSide.close();

    return RunningProcess{
        .pid = std::move(pid),
        .procStdin = std::move(procStdin),
        .procStdout = std::move(procStdout),
    };
}

[[gnu::unused]]
std::ostream &
operator<<(std::ostream & os, ReplOutputParser::State s)
{
    switch (s) {
    case ReplOutputParser::State::Prompt:
        os << "prompt";
        break;
    case ReplOutputParser::State::Context:
        os << "context";
        break;
    }
    return os;
}

void ReplOutputParser::transition(State new_state, char responsible_char, bool wasPrompt)
{
    if constexpr (DEBUG_REPL_PARSER) {
        std::cerr << "transition " << new_state << " for " << MaybeHexEscapedChar{responsible_char}
                  << (wasPrompt ? " [prompt]" : "") << "\n";
    }
    state = new_state;
    pos_in_prompt = 0;
}

bool ReplOutputParser::feed(char c)
{
    if (c == '\n') {
        transition(State::Prompt, c);
        return false;
    }
    switch (state) {
    case State::Context:
        break;
    case State::Prompt:
        if (pos_in_prompt == prompt.length() - 1 && prompt[pos_in_prompt] == c) {
            transition(State::Context, c, true);
            return true;
        }
        if (pos_in_prompt >= prompt.length() - 1 || prompt[pos_in_prompt] != c) {
            transition(State::Context, c);
            break;
        }
        pos_in_prompt++;
        break;
    }
    return false;
}

bool TestSession::readOutThen(ReadOutThenCallback cb)
{
    std::vector<char> buf(1024);

    for (;;) {
        ssize_t res = read(proc.procStdout.readSide.get(), buf.data(), buf.size());

        if (res < 0) {
            throw SysError("read");
        }
        if (res == 0) {
            return false;
        }

        switch (cb(std::span(buf.data(), res))) {
        case ReadOutThenCallbackResult::Stop:
            return true;
        case ReadOutThenCallbackResult::Continue:
            continue;
        }
    }
}

bool TestSession::waitForPrompt()
{
    bool notEof = readOutThen([&](std::span<const char> s) -> ReadOutThenCallbackResult {
        bool foundPrompt = false;

        for (auto ch : s) {
            // foundPrompt = foundPrompt || outputParser.feed(buf[i]);
            bool wasEaten = true;
            eater.feed(ch, [&](char c) {
                wasEaten = false;
                foundPrompt = outputParser.feed(ch) || foundPrompt;

                outLog.push_back(c);
            });

            if constexpr (DEBUG_REPL_PARSER) {
                std::cerr << "raw " << MaybeHexEscapedChar{ch} << (wasEaten ? " [eaten]" : "") << "\n";
            }
        }

        return foundPrompt ? ReadOutThenCallbackResult::Stop : ReadOutThenCallbackResult::Continue;
    });

    return notEof;
}

void TestSession::wait()
{
    readOutThen([&](std::span<const char> s) {
        for (auto ch : s) {
            eater.feed(ch, [&](char c) {
                outputParser.feed(c);
                outLog.push_back(c);
            });
        }
        // just keep reading till we hit eof
        return ReadOutThenCallbackResult::Continue;
    });
}

void TestSession::close()
{
    proc.procStdin.close();
    wait();
    proc.procStdout.close();
}

void TestSession::runCommand(std::string command)
{
    if constexpr (DEBUG_REPL_PARSER) {
        std::cerr << "runCommand " << command << "\n";
    }
    command += "\n";
    // We have to feed a newline into the output parser, since Lix might not
    // give us a newline before a prompt in all cases (it might clear line
    // first, e.g.)
    outputParser.feed('\n');
    // Echo is disabled, so we have to make our own
    outLog.append(command);
    writeFull(proc.procStdin.writeSide.get(), command, false);
}

};
