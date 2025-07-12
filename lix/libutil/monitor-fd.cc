#include "monitor-fd.hh"
#include "error.hh"

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/event.h>
#endif

namespace nix {

#ifdef __APPLE__
/**
 * This custom kqueue usage exists because Apple's poll implementation is
 * broken and loses event subscriptions if EVFILT_READ fires without matching
 * the requested `events` in the pollfd.
 *
 * We use EVFILT_READ, which causes some spurious wakeups (at most one per write
 * from the client, in addition to the socket lifecycle events), because the
 * alternate API, EVFILT_SOCK, doesn't work on pipes, which this is also used
 * to monitor in certain situations.
 *
 * See (EVFILT_SOCK):
 * https://github.com/netty/netty/blob/64bd2f4eb62c2fb906bc443a2aabf894c8b7dce9/transport-classes-kqueue/src/main/java/io/netty/channel/kqueue/AbstractKQueueChannel.java#L434
 *
 * See: https://git.lix.systems/lix-project/lix/issues/729
 * Apple bug in poll(2): FB17447257, available at https://openradar.appspot.com/FB17447257
 */
void MonitorFdHup::runThread(int watchFd, int terminateFd)
{
    int kqResult = kqueue();
    if (kqResult < 0) {
        throw SysError("MonitorFdHup kqueue");
    }
    AutoCloseFD kq{kqResult};

    std::array<struct kevent, 2> kevs;

    // kj uses EVFILT_WRITE for this, but it seems that it causes more spurious
    // wakeups in our case of doing blocking IO from another thread compared to
    // EVFILT_READ.
    //
    // EVFILT_WRITE and EVFILT_READ (for sockets at least, where I am familiar
    // with the internals) both go through a common filter which catches EOFs
    // and generates spurious wakeups for either readable/writable events.
    EV_SET(&kevs[0], watchFd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
    EV_SET(&kevs[1], terminateFd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);

    int result = kevent(kq.get(), kevs.data(), kevs.size(), nullptr, 0, nullptr);
    if (result < 0) {
        throw SysError("MonitorFdHup kevent add");
    }

    while (!quit) {
        std::array<struct kevent, 2> newEvents;
        int numEvents = kevent(kq.get(), nullptr, 0, newEvents.data(), newEvents.size(), nullptr);
        if (numEvents < 0) {
            throw SysError("MonitorFdHup kevent watch");
        }

        assert(size_t(numEvents) <= newEvents.size());
        for (int i = 0; i < numEvents; ++i) {
            auto & event = newEvents[i];

            if (event.ident == uintptr_t(watchFd)) {
                if ((event.flags & EV_EOF) != 0) {
                    callback();
                    return;
                }
            }
        }
    }
}
#else
void MonitorFdHup::runThread(int watchFd, int terminateFd)
{
    while (!quit) {
        /* Wait indefinitely until a POLLHUP occurs. */
        struct pollfd fds[2];
        fds[0].fd = watchFd;
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
        if (count == 0) {
            continue;
        }
        if (fds[0].revents & POLLHUP) {
            callback();
            break;
        }
        // No reason to actually look at the pipe FD if that's what
        // woke us, the only thing that actually matters is the quit
        // flag.
        if (quit) {
            break;
        }
        // On macOS, it is possible (although not observed on macOS
        // 14.5) that in some limited cases on buggy kernel versions,
        // all the non-POLLHUP events for the socket get delivered.
        // Sleeping avoids pointlessly spinning a thread on those.
        //
        // N.B. excessive delay on this can cause the daemon connection
        // thread to live longer than the client and lead to
        // synchronization problems if clients assume that the server
        // thread has released its temporary gc roots, etc.
        // See https://github.com/NixOS/nix/pull/12714#discussion_r2009265904
        usleep(1'000);
    }
}
#endif

MonitorFdHup::MonitorFdHup(int fd, std::function<void()> callback) : callback(callback)
{
    terminatePipe.create();
    int terminateFd = terminatePipe.readSide.get();
    thread = std::thread([this, fd, terminateFd]() {
        setCurrentThreadName("MonitorFdHup");
        this->runThread(fd, terminateFd);
    });
};

MonitorFdHup::~MonitorFdHup()
{
    quit = true;
    // Poke the thread out of its poll wait
    writeFull(terminatePipe.writeSide.get(), "*", false);
    if (thread.joinable()) {
        thread.join();
    }
}
}
