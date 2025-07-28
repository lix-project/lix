#include <gtest/gtest.h>
#include <exception>
#include "lix/libmain/crash-handler.hh"

namespace nix {

class OopsException : public std::exception
{
    const char * msg;

public:
    OopsException(const char * msg) : msg(msg) {}
    const char * what() const noexcept override
    {
        return msg;
    }
};

void causeCrashForTesting(std::function<void()> fixture)
{
    registerCrashHandler();
    std::cerr << "time to crash\n";
    try {
        fixture();
    } catch (...) {
        std::terminate();
    }
}

TEST(CrashHandler, exceptionName)
{
    ASSERT_DEATH(
        causeCrashForTesting([]() {
            throw OopsException{"lol oops"}; // NOLINT(lix-foreign-exceptions)
        }),
        "time to crash\nLix crashed.*OopsException: lol oops"
    );
}

TEST(CrashHandler, unknownTerminate)
{
    ASSERT_DEATH(
        causeCrashForTesting([]() { std::terminate(); }),
        "time to crash\nLix crashed.*std::terminate\\(\\) called without exception"
    );
}

TEST(CrashHandler, nonStdException)
{
    ASSERT_DEATH(
        causeCrashForTesting([]() {
            // NOLINTNEXTLINE(hicpp-exception-baseclass, lix-foreign-exceptions): intentional
            throw 4;
        }),
        "time to crash\nLix crashed.*Unknown exception! Spooky\\."
    );
}

}
