#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <optional>
#include <unistd.h>
#include <boost/algorithm/string/replace.hpp>

#include "test-session.hh"
#include "util.hh"
#include "tests/characterization.hh"
#include "tests/cli-literate-parser.hh"
#include "tests/terminal-code-eater.hh"

using namespace std::string_literals;

namespace nix {

static constexpr const std::string_view REPL_PROMPT = "nix-repl> ";

// ASCII ENQ character
static constexpr const std::string_view AUTOMATION_PROMPT = "\x05";

static std::string_view trimOutLog(std::string_view outLog)
{
    const std::string trailer = "\n"s + AUTOMATION_PROMPT;
    if (outLog.ends_with(trailer)) {
        outLog.remove_suffix(trailer.length());
    }
    return outLog;
}

class ReplSessionTest : public CharacterizationTest
{
    Path unitTestData = getUnitTestData();

public:
    Path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData + "/" + testStem;
    }

    void runReplTest(std::string_view const & content, std::vector<std::string> extraArgs = {}) const
    {
        auto syntax = CLILiterateParser::parse(std::string(REPL_PROMPT), content);

        // FIXME: why does this need two --quiets
        // show-trace is on by default due to test configuration, but is not a standard
        Strings args{"--quiet", "repl", "--quiet", "--option", "show-trace", "false", "--offline", "--extra-experimental-features", "repl-automation"};
        args.insert(args.end(), extraArgs.begin(), extraArgs.end());

        auto nixBin = canonPath(getEnvNonEmpty("NIX_BIN_DIR").value_or(NIX_BIN_DIR));

        auto process = RunningProcess::start(nixBin + "/nix", args);
        auto session = TestSession{std::string(AUTOMATION_PROMPT), std::move(process)};

        for (auto & bit : syntax) {
            if (bit.kind != CLILiterateParser::NodeKind::COMMAND) {
                continue;
            }

            if (!session.waitForPrompt()) {
                ASSERT_TRUE(false);
            }
            session.runCommand(bit.text);
        }
        if (!session.waitForPrompt()) {
            ASSERT_TRUE(false);
        }
        session.close();

        auto replacedOutLog = boost::algorithm::replace_all_copy(session.outLog, unitTestData, "TEST_DATA");
        auto cleanedOutLog = trimOutLog(replacedOutLog);

        auto parsedOutLog = CLILiterateParser::parse(std::string(AUTOMATION_PROMPT), cleanedOutLog, 0);

        parsedOutLog = CLILiterateParser::tidyOutputForComparison(std::move(parsedOutLog));
        syntax = CLILiterateParser::tidyOutputForComparison(std::move(syntax));

        ASSERT_EQ(parsedOutLog, syntax);
    }
};

TEST_F(ReplSessionTest, parses)
{
    writeTest("basic.ast", [this]() {
        const std::string content = readFile(goldenMaster("basic.test"));
        auto parser = CLILiterateParser{std::string(REPL_PROMPT)};
        parser.feed(content);

        std::ostringstream out{};
        for (auto & bit : parser.syntax()) {
            out << bit.print() << "\n";
        }
        return out.str();
    });

    writeTest("basic_tidied.ast", [this]() {
        const std::string content = readFile(goldenMaster("basic.test"));
        auto syntax = CLILiterateParser::parse(std::string(REPL_PROMPT), content);

        syntax = CLILiterateParser::tidyOutputForComparison(std::move(syntax));

        std::ostringstream out{};
        for (auto & bit : syntax) {
            out << bit.print() << "\n";
        }
        return out.str();
    });
}

TEST_F(ReplSessionTest, repl_basic)
{
    readTest("basic_repl.test", [this](std::string input) { runReplTest(input); });
}

#define DEBUGGER_TEST(name) \
    TEST_F(ReplSessionTest, name) \
    { \
        readTest(#name ".test", [this](std::string input) { \
            runReplTest(input, {"--debugger", "-f", goldenMaster(#name ".nix")}); \
        }); \
    }

DEBUGGER_TEST(regression_9918);
DEBUGGER_TEST(regression_9917);
DEBUGGER_TEST(regression_l145);
DEBUGGER_TEST(stack_vars);
DEBUGGER_TEST(no_nested_debuggers);

};
