#include <gtest/gtest.h>

#include "lix/libexpr/eval.hh"
#include "lix/libmain/progress-bar.hh"
#include "lix/libmain/loggers.hh"
#include "lix/libutil/logging.hh"
#include "lix/libmain/shared.hh"

constexpr std::string_view TEST_URL = "https://github.com/NixOS/nixpkgs/archive/master.tar.gz";
// Arbitrary number. We picked the size of a Nixpkgs tarball that we downloaded.
constexpr uint64_t TEST_EXPECTED = 43'370'307;
// Arbitrary number. We picked the progress made on a Nixpkgs tarball download we interrupted.
constexpr uint64_t TEST_DONE = 1'787'251;

constexpr std::string_view EXPECTED = ANSI_GREEN "1.7" ANSI_NORMAL "/41.4 MiB DL";
// Mostly here for informational purposes, but also if we change the way the escape codes
// are defined this test might break in some annoying to debug way.
constexpr std::string_view EXPECTED_RAW = "\x1b[32;1m1.7\x1b[0m/41.4 MiB DL";
static_assert(EXPECTED == EXPECTED_RAW, "Hey, hey, the ANSI escape code definitions prolly changed");

namespace nix
{
    TEST(ProgressBar, basicStatusRender) {
        initNix();
        initLibExpr();

        setLogFormat(LogFormat::bar);
        ASSERT_NE(dynamic_cast<ProgressBar *>(logger), nullptr);
        ProgressBar & progressBar = dynamic_cast<ProgressBar &>(*logger);

        Activity act(
            progressBar,
            lvlDebug,
            actFileTransfer,
            fmt("downloading '%s'", TEST_URL),
            { "https://github.com/NixOS/nixpkgs/archive/master.tar.gz" }
        );
        act.progress(TEST_DONE, TEST_EXPECTED);
        auto state = progressBar.state_.lock();
        std::string const renderedStatus = progressBar.getStatus(*state);

        ASSERT_EQ(renderedStatus, EXPECTED);
    }
}
