#pragma once
///@file

#include <thread>
#include <atomic>

#include <cstdlib>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "error.hh"
#include "file-descriptor.hh"
#include "signals.hh"

namespace nix {


class MonitorFdHup
{
private:
    std::thread thread;
    /**
     * Pipe used to interrupt the poll()ing in the monitoring thread.
     */
    Pipe terminatePipe;
    std::atomic_bool quit = false;

public:
    MonitorFdHup(int fd)
    {
        terminatePipe.create();
        auto &quit_ = this->quit;
        int terminateFd = terminatePipe.readSide.get();
        thread = std::thread([fd, terminateFd, &quit_]() {
            while (!quit_) {
                /* Wait indefinitely until a POLLHUP occurs. */
                struct pollfd fds[2];
                fds[0].fd = fd;
                /* Polling for no specific events (i.e. just waiting
                   for an error/hangup) doesn't work on macOS
                   anymore. So wait for read events and ignore
                   them. */
                // FIXME(jade): we have looked at the XNU kernel code and as
                // far as we can tell, the above is bogus. It should be the
                // case that the previous version of this and the current
                // version are identical: waiting for POLLHUP and POLLRDNORM in
                // the kernel *should* be identical.
                // https://github.com/apple-oss-distributions/xnu/blob/94d3b452840153a99b38a3a9659680b2a006908e/bsd/kern/sys_generic.c#L1751-L1758
                //
                // So, this needs actual testing and we need to figure out if
                // this is actually bogus.
                fds[0].events =
                    #ifdef __APPLE__
                    POLLRDNORM
                    #else
                    0
                    #endif
                    ;
                fds[1].fd = terminateFd;
                fds[1].events = POLLIN;

                auto count = poll(fds, 2, -1);
                if (count == -1) {
                    if (errno == EINTR || errno == EAGAIN) {
                        // These are best dealt with by just trying again.
                        continue;
                    } else {
                        throw SysError("in MonitorFdHup poll()");
                    }
                }
                /* This shouldn't happen, but can on macOS due to a bug.
                   See rdar://37550628.

                   This may eventually need a delay or further
                   coordination with the main thread if spinning proves
                   too harmful.
                */
                if (count == 0) continue;
                if (fds[0].revents & POLLHUP) {
                    triggerInterrupt();
                    break;
                }
                // No reason to actually look at the pipe FD, we just need it
                // to be able to get woken.
                if (quit_) {
                    break;
                }
                /* This will only happen on macOS. We sleep a bit to
                   avoid waking up too often if the client is sending
                   input. */
                sleep(1);
            }
        });
    };

    ~MonitorFdHup()
    {
        quit = true;
        // Poke the thread out of its poll wait
        writeFull(terminatePipe.writeSide.get(), "*", false);
        thread.join();
    }
};


}
