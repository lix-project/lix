#pragma once
/** @file Signal handling in Lix
 *
 * Processes are expected to be simple, mostly just calling execve.
 * All processes should call restoreProcessContext(), possibly with
 * `false` (depends on whether mounts should be restored), which will unmask
 * SIGINT and other signals that were previously masked in an advanced process
 * such as the one that started them, so the process can be interrupted.
 *
 * It is generally a mistake to fork a process without at least calling
 * restoreSignals() or restoreProcessContext().
 */

#include "lix/libutil/error.hh"
#include "lix/libutil/result.hh"

#include <kj/async.h>
#include <memory>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <atomic>
#include <functional>

namespace nix {

/// reserved signal used to notify threads of interruption requests, e.g. users
/// pressing Control-C on the terminal. we purposely do not use SIGINT handlers
/// provided by the OS to allow for more orderly cleanup of running operations.
static inline constexpr int INTERRUPT_NOTIFY_SIGNAL = SIGUSR1;
/// kj needs a signal for internal use. no system lix habitually runs on causes
/// kj to actually *use* this signal, but better safe than sorry—and since some
/// OSes (*cough* macos) don't support realtime signals we must use SIGUSR2 for
/// this, thus "consuming" both USR signals. at some point we will change this.
static inline constexpr int KJ_RESERVED_SIGNAL = SIGUSR2;

/* User interruption. */

class Interrupted;

// global counter of how many interrupt requests of any type we've received. we
// count SIGINT, SIGTERM and SIGHUP equally here, but this mainly exists to let
// us keep track of which SIGINT events we have processed and which we haven't.
extern std::atomic_unsigned_lock_free _interruptSequence;

extern thread_local std::function<bool()> interruptCheck;
// the largest `_interruptSequence` the current thread has seen and acted upon.
extern thread_local std::atomic_unsigned_lock_free::value_type threadInterruptSeq;

Interrupted makeInterrupted();
void _interrupted();

/**
 * Clear a pending `checkInterrupt()` request. Mainly useful for the REPL which
 * can safely continue after a user interruption of eg. some hung Nixlang code.
 */
void unsetUserInterruptRequest();

bool isInterrupted();

/**
 * check whether an interrupt request is pending and throw Interrupted if so. a
 * user hitting ^C is the main source of interrupts in interactive use, daemons
 * are interrupted mainly by SIGHUP from clients disconnecting unexpectedly, or
 * SIGTERM sent by the system service managers to tell the daemon to shut down.
 */
void inline checkInterrupt()
{
    const auto seq = _interruptSequence.load(std::memory_order::relaxed);
    if (seq > threadInterruptSeq || (interruptCheck && interruptCheck())) {
        threadInterruptSeq = seq;
        _interrupted();
    }
}

MakeError(Interrupted, BaseError);

void restoreSignals();

/**
 * Start a thread that handles various signals. Also block those signals
 * on the current thread (and thus any threads created by it).
 *
 * Also saves the signal mask before changing the mask to block those
 * signals. See saveSignalMask().
 */
void startSignalHandlerThread();

/**
 * Saves the signal mask, which is the signal mask that nix will restore
 * before creating child processes.
 * See setChildSignalMask() to set an arbitrary signal mask instead of the
 * current mask.
 */
void saveSignalMask();

/**
 * Sets the signal mask. Like saveSignalMask() but for a signal set that doesn't
 * necessarily match the current thread's mask.
 * See saveSignalMask() to set the saved mask to the current mask.
 */
void setChildSignalMask(sigset_t *sigs);

struct InterruptCallback
{
    virtual ~InterruptCallback() { };
};

/**
 * Register a function that gets called on SIGINT (in a non-signal
 * context).
 */
std::unique_ptr<InterruptCallback> createInterruptCallback(
    std::function<void()> callback);

template<typename T>
kj::Promise<Result<T>> makeInterruptible(kj::Promise<Result<T>> p)
{
    auto onInterrupt = kj::newPromiseAndCrossThreadFulfiller<Result<T>>();
    // the fulfiller must be a shared_ptr<Own<...>> since functions must
    // be copyable, and we don't have move_only_function on all stdlibs.
    auto fulfiller =
        std::make_shared<decltype(onInterrupt.fulfiller)>(std::move(onInterrupt.fulfiller));
    auto interruptCallback = createInterruptCallback([fulfiller] {
        (*fulfiller)->fulfill(result::failure(std::make_exception_ptr(makeInterrupted())));
    });
    return p.attach(std::move(interruptCallback)).exclusiveJoin(std::move(onInterrupt.promise));
}

void triggerInterrupt();

/**
 * A RAII class that causes the current thread to receive `INTERRUPT_NOTIFY_SIGNAL` when
 * the signal handler thread receives SIGINT. That is, this allows
 * SIGINT to be multiplexed to multiple threads.
 */
struct ReceiveInterrupts
{
    pthread_t target;
    std::unique_ptr<InterruptCallback> callback;

    ReceiveInterrupts()
        : target(pthread_self())
        , callback(createInterruptCallback([&] { pthread_kill(target, INTERRUPT_NOTIFY_SIGNAL); }))
    {
    }
};

};
