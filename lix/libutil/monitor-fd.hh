#pragma once
///@file Observes a file descriptor for hang-up events and notifies a
/// callback.

#include <thread>
#include <atomic>

#include <poll.h>
#include <sys/types.h>
#include <unistd.h>

#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/thread-name.hh"

namespace nix {

/**
 * Observes a file descriptor for hang-up events and notifies a callback if any show up.
 *
 * The callback will be called at most once.
 */
class MonitorFdHup
{
private:
    std::thread thread;
    /**
     * Pipe used to interrupt the poll()ing in the monitoring thread.
     */
    Pipe terminatePipe;
    std::atomic_bool quit = false;
    std::function<void()> callback;

    void runThread(int watchFd, int terminateFd);

public:
    MonitorFdHup(int fd, std::function<void()> callback = nix::triggerInterrupt);

    ~MonitorFdHup();
};


}
