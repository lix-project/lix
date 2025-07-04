#include "lix/libutil/signals.hh"
#include "async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/sync.hh"
#include "lix/libutil/terminal.hh"
#include "lix/libutil/thread-name.hh"
#include "logging.hh"
#include <csignal>
#include <kj/time.h>

#include <kj/async-unix.h>
#include <kj/async.h>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <unistd.h>

namespace nix {

std::atomic<bool> _isInterrupted = false;

thread_local std::function<bool()> interruptCheck;

Interrupted makeInterrupted()
{
    return Interrupted("interrupted by the user");
}

void _interrupted()
{
    /* Block user interrupts while an exception is being handled.
       Throwing an exception while another exception is being handled
       kills the program! */
    if (!std::uncaught_exceptions()) {
        throw makeInterrupted();
    }
}

void unsetUserInterruptRequest()
{
    _isInterrupted = false;
    // recapture the signal as the signal handler thread will have released it
    AIO().unixEventPort.captureSignal(SIGINT);
}

//////////////////////////////////////////////////////////////////////

/* We keep track of interrupt callbacks using integer tokens, so we can iterate
   safely without having to lock the data structure while executing arbitrary
   functions.
 */
struct InterruptCallbacks {
    typedef int64_t Token;

    /* We use unique tokens so that we can't accidentally delete the wrong
       handler because of an erroneous double delete. */
    Token nextToken = 0;

    /* Used as a list, see InterruptCallbacks comment. */
    std::map<Token, std::function<void()>> callbacks;
};

static Sync<std::shared_ptr<Sync<InterruptCallbacks>>> _interruptCallbacks;

static void signalHandlerThread(const std::vector<int> set)
{
    setCurrentThreadName("signal handler");

    AsyncIoRoot aio;
    std::optional<kj::TimePoint> printInterruptMessageAt;

    for (auto sig : set) {
        AIO().unixEventPort.captureSignal(sig);
    }

    auto onSignal = [&] {
        kj::Promise<siginfo_t> promise(kj::NEVER_DONE);
        for (auto sig : set) {
            promise = promise.exclusiveJoin(AIO().unixEventPort.onSignal(sig));
        }
        if (printInterruptMessageAt) {
            return AIO().provider.getTimer().atTime(*printInterruptMessageAt).then([] {
                return siginfo_t{.si_signo = -1};
            });
        } else {
            return promise;
        }
    };

    while (true) {
        auto info = onSignal().wait(aio.kj.waitScope);
        int signal = info.si_signo;

        if (printInterruptMessageAt && *printInterruptMessageAt <= AIO().provider.getTimer().now()) {
            // we only print to a terminal, and only by bypassing the logger, to
            // ensure that it's both a *user* who is sending us this signal, and
            // that the user will get a notification that isn't mixed with logs.
            if (_isInterrupted && isatty(STDERR_FILENO)) {
                writeLogsToStderr(
                    "Still shutting down. Press ^C again to abort all operations immediately.\n"
                );
            }
            printInterruptMessageAt = std::nullopt;
        }

        // treat SIGINT specially. SIGINT is usually sent interactively, SIGTERM only to daemons
        if (signal == SIGINT) {
            sigset_t unblock;
            sigemptyset(&unblock);
            sigaddset(&unblock, signal);
            pthread_sigmask(SIG_UNBLOCK, &unblock, nullptr);
            ::signal(SIGINT, SIG_DFL);
            printInterruptMessageAt = AIO().provider.getTimer().now() + 1 * kj::SECONDS;
            triggerInterrupt();
        } else if (signal == SIGTERM || signal == SIGHUP) {
            triggerInterrupt();
        } else if (signal == SIGWINCH) {
            updateWindowSize();
        }
    }
}

void triggerInterrupt()
{
    _isInterrupted = true;

    auto callbacks = *_interruptCallbacks.lock();
    if (callbacks) {
        InterruptCallbacks::Token i = 0;
        while (true) {
            std::function<void()> callback;
            {
                auto interruptCallbacks(callbacks->lock());
                auto lb = interruptCallbacks->callbacks.lower_bound(i);
                if (lb == interruptCallbacks->callbacks.end())
                    break;

                callback = lb->second;
                i = lb->first + 1;
            }

            try {
                callback();
            } catch (...) {
                ignoreExceptionInDestructor();
            }
        }
    }
}

static sigset_t savedSignalMask;
static bool savedSignalMaskIsSet = false;

void setChildSignalMask(sigset_t * sigs)
{
    assert(sigs); // C style function, but think of sigs as a reference

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
    sigemptyset(&savedSignalMask);
    // There's no "assign" or "copy" function, so we rely on (math) idempotence
    // of the or operator: a or a = a.
    sigorset(&savedSignalMask, sigs, sigs);
#else
    // Without sigorset, our best bet is to assume that sigset_t is a type that
    // can be assigned directly, such as is the case for a sigset_t defined as
    // an integer type.
    savedSignalMask = *sigs;
#endif

    savedSignalMaskIsSet = true;
}

void saveSignalMask() {
    if (sigprocmask(SIG_BLOCK, nullptr, &savedSignalMask))
        throw SysError("querying signal mask");

    savedSignalMaskIsSet = true;
}

void startSignalHandlerThread()
{
    updateWindowSize();
    saveSignalMask();

    std::vector<int> signals{SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGWINCH};

    sigset_t set;
    sigemptyset(&set);
    for (auto sig : signals) {
        sigaddset(&set, sig);
    }
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr))
        throw SysError("blocking signals");

    std::thread(signalHandlerThread, signals).detach();
}

void restoreSignals()
{
    // If startSignalHandlerThread wasn't called, that means we're not running
    // in a proper libmain process, but a process that presumably manages its
    // own signal handlers. Such a process should call either
    //  - initNix(), to be a proper libmain process
    //  - startSignalHandlerThread(), to resemble libmain regarding signal
    //    handling only
    //  - saveSignalMask(), for processes that define their own signal handling
    //    thread
    // TODO: Warn about this? Have a default signal mask? The latter depends on
    //       whether we should generally inherit signal masks from the caller.
    //       I don't know what the larger unix ecosystem expects from us here.
    if (!savedSignalMaskIsSet)
        return;

    if (sigprocmask(SIG_SETMASK, &savedSignalMask, nullptr))
        throw SysError("restoring signals");
}

/* RAII helper to automatically deregister a callback. */
struct InterruptCallbackImpl : InterruptCallback
{
    std::shared_ptr<Sync<InterruptCallbacks>> parent;
    InterruptCallbacks::Token token;
    InterruptCallbackImpl(
        std::shared_ptr<Sync<InterruptCallbacks>> parent, InterruptCallbacks::Token token
    )
        : parent(parent)
        , token(token)
    {
    }
    ~InterruptCallbackImpl() override
    {
        auto interruptCallbacks(parent->lock());
        interruptCallbacks->callbacks.erase(token);
    }
};

std::unique_ptr<InterruptCallback> createInterruptCallback(std::function<void()> callback)
{
    auto callbacks = *_interruptCallbacks.lock();
    if (!callbacks) {
        auto lock = _interruptCallbacks.lock();
        if (!*lock) {
            *lock = std::make_shared<Sync<InterruptCallbacks>>();
        }
        callbacks = *lock;
    }

    auto interruptCallbacks(callbacks->lock());
    auto token = interruptCallbacks->nextToken++;
    interruptCallbacks->callbacks.emplace(token, callback);

    return std::make_unique<InterruptCallbackImpl>(callbacks, token);
}

};
