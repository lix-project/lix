#pragma once
/** @file Signal handling in Lix
 *
 * Processes are expected to be either:
 * - Advanced processes which call into Lix's logic, like the daemon processes.
 * - Basic processes that are just going to execve.
 *
 * Processes should be set up accordingly following a fork:
 * In the first case, such processes should have a signal handler thread that
 * catches SIGINT and dispatches it to the rest of the system so they should
 * call startSignalHandlerThread(). In the second case, processes should call
 * restoreProcessContext(), possibly with `false` (depends on whether mounts
 * should be restored), which will unmask SIGINT and other signals that were
 * previously masked in an advanced process such as the one that started
 * them, so the process can be interrupted.
 *
 * It is generally a mistake to fork a process without at least calling
 * restoreSignals() or restoreProcessContext().
 */


#include "error.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <atomic>
#include <functional>

namespace nix {

/* User interruption. */

class Interrupted;

extern std::atomic<bool> _isInterrupted;

extern thread_local std::function<bool()> interruptCheck;

Interrupted makeInterrupted();
void _interrupted();

void inline checkInterrupt()
{
    if (_isInterrupted || (interruptCheck && interruptCheck()))
        _interrupted();
}

MakeError(Interrupted, BaseError);

void restoreSignals();


/**
 * Whether to save the signal mask when starting the signal handler thread.
 *
 * The signal mask shouldn't be saved if the current signal mask is the one for
 * processes with a signal handler thread.
 */
enum class DoSignalSave
{
    Save,
    DontSaveBecauseAdvancedProcess,
};

/**
 * Start a thread that handles various signals. Also block those signals
 * on the current thread (and thus any threads created by it).
 *
 * Optionally saves the signal mask before changing the mask to block those
 * signals. See saveSignalMask().
 *
 * This should also be executed after certain forks from Lix processes that
 * expect to be "advanced" (see file doc comment), since the signal thread will
 * die on fork. Of course this whole situation is kind of unsound since we
 * definitely violate async-signal-safety requirements, but, well.
 */
void startSignalHandlerThread(DoSignalSave doSave = DoSignalSave::Save);

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

void triggerInterrupt();

/**
 * A RAII class that causes the current thread to receive SIGUSR1 when
 * the signal handler thread receives SIGINT. That is, this allows
 * SIGINT to be multiplexed to multiple threads.
 */
struct ReceiveInterrupts
{
    pthread_t target;
    std::unique_ptr<InterruptCallback> callback;

    ReceiveInterrupts()
        : target(pthread_self())
        , callback(createInterruptCallback([&]() { pthread_kill(target, SIGUSR1); }))
    { }
};

};
