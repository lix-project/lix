#pragma once
///@file

#include <cassert>
#include <exception>

/**
 * A trivial class to run a function at the end of a scope.
 */
template<typename Fn>
class Finally
{
private:
    Fn fun;
    bool movedFrom = false;

public:
    Finally(Fn fun) : fun(std::move(fun)) { }
    // Copying Finallys is definitely not a good idea and will cause them to be
    // called twice.
    Finally(Finally &other) = delete;
    Finally(Finally &&other) : fun(std::move(other.fun)) {
        other.movedFrom = true;
    }
    // NOLINTNEXTLINE(bugprone-exception-escape): the noexcept is declared properly here, the analysis seems broken
    ~Finally() noexcept(noexcept(fun()))
    {
        try {
            if (!movedFrom)
                fun();
        } catch (...) {
            // finally may only throw an exception if exception handling is not already
            // in progress. if handling *is* in progress we have to return cleanly here
            // but are still prohibited from doing so since eating the exception would,
            // in almost all cases, mess up error handling even more. the only good way
            // to handle this is to abort entirely and leave a message, so we'll assert
            // (and rethrow anyway, just as a defense against possible NASSERT builds.)
            if (std::uncaught_exceptions()) {
                assert(false &&
                    "Finally function threw an exception during exception handling. "
                    "this is not what you want, please use some other methods (like "
                    "std::promise or async) instead.");
            }
            throw;
        }
    }
};
