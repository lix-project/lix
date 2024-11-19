#pragma once
///@file

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
            setCurrentThreadName("MonitorFdHup");
            while (!quit_) {
                /* Wait indefinitely until a POLLHUP occurs. */
                struct pollfd fds[2];
                fds[0].fd = fd;
                // There is a POSIX violation on macOS: you have to listen for
                // at least POLLHUP to receive HUP events for a FD. POSIX says
                // this is not so, and you should just receive them regardless,
                // however, as of our testing on macOS 14.5, the events do not
                // get delivered in such a case.
                //
                // This is allegedly filed as rdar://37537852.
                //
                // Relevant code, which backs this up:
                // https://github.com/apple-oss-distributions/xnu/blob/94d3b452840153a99b38a3a9659680b2a006908e/bsd/kern/sys_generic.c#L1751-L1758
                fds[0].events = POLLHUP;
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
                // No reason to actually look at the pipe FD if that's what
                // woke us, the only thing that actually matters is the quit
                // flag.
                if (quit_) {
                    break;
                }
                // On macOS, it is possible (although not observed on macOS
                // 14.5) that in some limited cases on buggy kernel versions,
                // all the non-POLLHUP events for the socket get delivered.
                // Sleeping avoids pointlessly spinning a thread on those.
                sleep(1);
            }
        });
    };

    ~MonitorFdHup()
    {
        quit = true;
        // Poke the thread out of its poll wait
        writeFull(terminatePipe.writeSide.get(), "*", false);
        if (thread.joinable()) {
            thread.join();
        }
    }
};


}
