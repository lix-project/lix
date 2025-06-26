#include <gtest/gtest.h>

#include <boost/algorithm/string/replace.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>

#include "test-session.hh"
#include "tests/characterization.hh"
#include "tests/cli-literate-parser.hh"
#include "lix/libutil/strings.hh"

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

    void runReplTest(const std::string content, std::vector<std::string> extraArgs = {}) const
    {
        auto parsed = cli_literate_parser::parse(
            content, cli_literate_parser::Config{.prompt = std::string(REPL_PROMPT), .indent = 2}
        );
        parsed.interpolatePwd(unitTestData);

        // FIXME: why does this need two --quiets
        // show-trace is on by default due to test configuration, but is not a
        // standard
        Strings args{
            "--quiet",
            "repl",
            "--quiet",
            "--option",
            "show-trace",
            "false",
            "--offline",
            "--extra-experimental-features",
            "repl-automation",
        };
        args.insert(args.end(), extraArgs.begin(), extraArgs.end());
        args.insert(args.end(), parsed.args.begin(), parsed.args.end());

        auto nixBin = canonPath(getEnvNonEmpty("NIX_BIN_DIR").value_or(NIX_BIN_DIR));

        auto process = RunningProcess::start(nixBin + "/nix", args);
        auto session = TestSession(std::string(AUTOMATION_PROMPT), std::move(process));

        for (auto & event : parsed.syntax) {
            std::visit(
                overloaded{
                    [&](const cli_literate_parser::Command & e) {
                        ASSERT_TRUE(session.waitForPrompt());
                        if (e.text == ":quit") {
                            // If we quit the repl explicitly, we won't have a
                            // prompt when we're done.
                            parsed.shouldStart = false;
                        }
                        session.runCommand(e.text);
                    },
                    [&](const auto & e) {},
                },
                event
            );
        }
        if (parsed.shouldStart) {
            ASSERT_TRUE(session.waitForPrompt());
        }
        session.close();

        // Remove references to the checkout path
        auto replacedOutLog =
            boost::algorithm::replace_all_copy(session.outLog, unitTestData, "$TEST_DATA");
        // Remove references to the current version
        replacedOutLog =
            boost::algorithm::replace_all_copy(replacedOutLog, PACKAGE_VERSION, "$VERSION");
        auto cleanedOutLog = trimOutLog(replacedOutLog);

        auto parsedOutLog = cli_literate_parser::parse(
            std::string(cleanedOutLog),
            cli_literate_parser::Config{.prompt = std::string(AUTOMATION_PROMPT), .indent = 0}
        );

        auto expected = parsed.tidyOutputForComparison();
        auto actual = parsedOutLog.tidyOutputForComparison();

        ASSERT_EQ(expected, actual);
    }

    void runReplTestPath(const std::string_view & nameBase, std::vector<std::string> extraArgs)
    {
        auto nixPath = goldenMaster(nameBase + ".nix");
        if (pathExists(nixPath)) {
            extraArgs.push_back("-f");
            extraArgs.push_back(nixPath);
        }
        readTest(nameBase + ".test", [this, extraArgs](std::string input) {
            runReplTest(input, extraArgs);
        });
    }

    void runReplTestPath(const std::string_view & nameBase)
    {
        runReplTestPath(nameBase, {});
    }
};

TEST_F(ReplSessionTest, round_trip)
{
    writeTest("basic.test", [this]() {
        const std::string content = readFile(goldenMaster("basic.test"));
        auto parsed = cli_literate_parser::parse(
            content, cli_literate_parser::Config{.prompt = std::string(REPL_PROMPT)}
        );

        std::ostringstream out{};
        for (auto & node : parsed.syntax) {
            cli_literate_parser::unparseNode(out, node, true);
        }
        return out.str();
    });
}

TEST_F(ReplSessionTest, tidy)
{
    writeTest("basic.ast", [this]() {
        const std::string content = readFile(goldenMaster("basic.test"));
        auto parsed = cli_literate_parser::parse(
            content, cli_literate_parser::Config{.prompt = std::string(REPL_PROMPT)}
        );
        std::ostringstream out{};
        for (auto & node : parsed.syntax) {
            out << debugNode(node) << "\n";
        }
        return out.str();
    });
    writeTest("basic_tidied.ast", [this]() {
        const std::string content = readFile(goldenMaster("basic.test"));
        auto parsed = cli_literate_parser::parse(
            content, cli_literate_parser::Config{.prompt = std::string(REPL_PROMPT)}
        );
        auto tidied = parsed.tidyOutputForComparison();
        std::ostringstream out{};
        for (auto & node : tidied) {
            out << debugNode(node) << "\n";
        }
        return out.str();
    });
}

#define REPL_TEST(name)           \
    TEST_F(ReplSessionTest, name) \
    {                             \
        runReplTestPath(#name);   \
    }

REPL_TEST(basic_repl);
REPL_TEST(no_nested_debuggers);
REPL_TEST(regression_9917);
REPL_TEST(regression_9918);
REPL_TEST(regression_l145);
REPL_TEST(regression_l592);
REPL_TEST(repl_input);
REPL_TEST(repl_overlays);
REPL_TEST(repl_overlays_regression_l777);
REPL_TEST(repl_overlays_compose);
REPL_TEST(repl_overlays_destructure_without_dotdotdot_errors);
REPL_TEST(repl_overlays_destructure_without_formals_ok);
REPL_TEST(repl_overlays_error);
REPL_TEST(repl_printing);
REPL_TEST(stack_vars);
REPL_TEST(errors);
REPL_TEST(idempotent);
REPL_TEST(debug_frames);
REPL_TEST(debug_ignore_try);
REPL_TEST(debug_ignore_try_defaults);

}; // namespace nix
