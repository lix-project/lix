#include "lix/libutil/signals.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/sync.hh"
#include "lix/libutil/terminal.hh"
#include "lix/libutil/thread-name.hh"
#include "logging.hh"
#include <atomic>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <unistd.h>

namespace nix {

std::atomic_unsigned_lock_free _interruptSequence{0};
static std::atomic_unsigned_lock_free printMessageForSeq{0}, allowInterruptsAfter{0};
thread_local std::atomic_unsigned_lock_free::value_type threadInterruptSeq{_interruptSequence.load()
};

thread_local std::function<bool()> interruptCheck;

Interrupted makeInterrupted()
{
    return Interrupted("interrupted by the user");
}

bool isInterrupted()
{
    const auto seq = _interruptSequence.load(std::memory_order::relaxed);
    return seq > threadInterruptSeq && seq > allowInterruptsAfter.load(std::memory_order::relaxed);
}

void _interrupted()
{
    // don't throw for inhibited interrupt, ie those that were explicitly unset
    if (_interruptSequence.load() <= allowInterruptsAfter.load()) {
        return;
    }
    /* Block user interrupts while an exception is being handled.
       Throwing an exception while another exception is being handled
       kills the program! */
    if (!std::uncaught_exceptions()) {
        throw makeInterrupted();
    }
}

void unsetUserInterruptRequest()
{
    // inhibit handling of pending interruptions in other threads
    allowInterruptsAfter = _interruptSequence.load();
    // tell the signal handler thread to skip the please-try-again message
    printMessageForSeq = 0;
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

static void signalHandlerThread(sigset_t set)
{
    // sleep for one second in a dedicated thread. this is needed beacuse darwin
    // does not let us receive process signals in a non-main thread. what fun 🫠
    const auto scheduleSigintMessage = [] {
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            // we only print to a terminal, and only by bypassing the logger, to
            // ensure that it's both a *user* who is sending us this signal, and
            // that the user will get a notification that isn't mixed with logs.
            if (_interruptSequence.load() == printMessageForSeq.load() && isatty(STDERR_FILENO)) {
                writeLogsToStderr(
                    "Still shutting down. Press ^C again to abort all operations immediately.\n"
                );
            }
        }).detach();
    };

    setCurrentThreadName("signal handler");
    while (true) {
        int signal = 0;
        sigwait(&set, &signal);

        // treat SIGINT specially. SIGINT is usually sent interactively, SIGTERM only to daemons
        if (signal == SIGINT) {
            if (_interruptSequence.load() > allowInterruptsAfter.load()) {
                // unblock and re-kill the entire process if sigint was sent twice in this
                // round of interruption processing. this is apparently the easiest way to
                // make sure the process terminates on double ^C without breaking anything
                sigset_t unblock;
                sigemptyset(&unblock);
                sigaddset(&unblock, signal);
                pthread_sigmask(SIG_UNBLOCK, &unblock, nullptr);
                kill(getpid(), SIGINT);
            } else {
                // this is intentionally racy. triggerInterrupt increments the counter, if
                // another interrupt is triggered in close proximity we do not want to see
                // a message. this can happen if the repl or from the MonitorFdHup thread.
                printMessageForSeq = _interruptSequence.load() + 1;
                scheduleSigintMessage();
                triggerInterrupt();
            }
        } else if (signal == SIGTERM || signal == SIGHUP) {
            triggerInterrupt();
        } else if (signal == SIGWINCH) {
            updateWindowSize();
        }
    }
}

void triggerInterrupt()
{
    _interruptSequence++;

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

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGWINCH);
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr))
        throw SysError("blocking signals");

    std::thread(signalHandlerThread, set).detach();
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
