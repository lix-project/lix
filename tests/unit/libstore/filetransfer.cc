#include "filetransfer.hh"

#include <future>
#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace nix {

TEST(FileTransfer, exceptionAbortsDownload)
{
    struct Done
    {};

    auto ft = makeFileTransfer();

    LambdaSink broken([](auto block) { throw Done(); });

    ASSERT_THROW(ft->download(FileTransferRequest("file:///dev/zero"), broken), Done);

    // makeFileTransfer returns a ref<>, which cannot be cleared. since we also
    // can't default-construct it we'll have to overwrite it instead, but we'll
    // take the raw pointer out first so we can destroy it in a detached thread
    // (otherwise a failure will stall the process and have it killed by meson)
    auto reset = std::async(std::launch::async, [&]() { ft = makeFileTransfer(); });
    EXPECT_EQ(reset.wait_for(10s), std::future_status::ready);
    // if this did time out we have to leak `reset`.
    if (reset.wait_for(0s) == std::future_status::timeout) {
        (void) new auto(std::move(reset));
    }
}
}
