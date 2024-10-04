#pragma once
/// @file

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
 * Start a thread that handles various signals. Also block those signals
 * on the current thread (and thus any threads created by it).
 * Saves the signal mask before changing the mask to block those signals.
 * See saveSignalMask().
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
