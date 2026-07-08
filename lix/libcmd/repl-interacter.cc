#include "libutil/fmt.hh"
#include "libutil/terminal.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/logging.hh"
#include "lix/lix-rs/main.gen.hh"
#include "lix/lix-rs/utils.hh"
#include <iostream>
#include <memory>
#include <string>

#include "lix/libcmd/repl-interacter.hh"

namespace rust {
Vec<String> Impl<lix::repl::CxxCompleter, lix::repl::ReplCompleter>::complete(
    Ref<lix::repl::CxxCompleter> self, Ref<Str> input
)
try {
    auto s = to_std_string(input);

    auto result = std::vec::Vec<std::string::String>::new_();
    for (auto & possible : self.cpp().completePrefix(s)) {
        result.push(rust::to_string(possible));
    }
    return result;
} catch (...) {
    // the completer should have logged anything interesting.
    return std::vec::Vec<std::string::String>::new_();
}
}

namespace nix {

ReadlineLikeInteracter::ReadlineLikeInteracter(std::string historyFile) : historyFile(historyFile) {}

void ReadlineLikeInteracter::init(detail::ReplCompleterMixin * repl)
{
    try {
        createDirs(dirOf(historyFile));
    } catch (SysError & e) {
        logWarning(e.info());
    }

    auto rl = repl::Rustyline::new_(rust::to_string(historyFile).as_str(), *repl);
    match_result(
        std::move(rl),
        [&](repl::Rustyline ok) { this->rl = std::make_unique<repl::Rustyline>(std::move(ok)); },
        [&](rust::Box<rust::Dyn<rust::std::error::Error>> err) {
            throw Error("%s", Uncolored(to_std_string(err.to_string())));
        }
    );
}

static rust::Ref<rust::Str> promptForType(ReplPromptType promptType)
{
    switch (promptType) {
    case ReplPromptType::ReplPrompt:
        return "nix-repl> "_rs;
    case ReplPromptType::ContinuationPrompt:
        return "          "_rs;
    }
    assert(false);
}

bool ReadlineLikeInteracter::getLine(std::string & input, ReplPromptType promptType)
{
    auto s = rl->ask(promptForType(promptType));

    // rustyline temporarily sets a SIGWINCH handler
    KJ_DEFER(invalidateWindowSize());

    return match_result(
        std::move(s),
        [&](rust::String ok) {
            input += to_std_string(ok);
            input += '\n';
            return true;
        },
        [&](rust::rustyline::error::ReadlineError err) {
            if (err.matches_Interrupted()) {
                input.clear();
                return true;
            }

            if (err.matches_Eof()) {
                return false;
            }

            throw Error("%s", Uncolored(to_std_string(err.into().to_string())));
        }
    );
}

void ReadlineLikeInteracter::writeHistory()
{
    if (rl) {
        rl->write_history();
    }
}

ReadlineLikeInteracter::~ReadlineLikeInteracter()
{
    this->writeHistory();
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
