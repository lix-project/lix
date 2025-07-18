#include "file-descriptor.hh"
#include "lix/libutil/charptr-cast.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/signals.hh"

#include <cerrno>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace nix {

std::string readFile(int fd)
{
    struct stat st;
    if (fstat(fd, &st) == -1)
        throw SysError("statting file");

    // st_size is off_t, which is signed for some reason, and there doesn't
    // seem to be any rule stating that it _can't_ return a negative value.
    //
    // So, when a filesystem returns a small negative value for whatever reason,
    // we cast it to unsigned and then try to preallocate ALL THE MEMORY,
    // which, of course, explodes horribly and very user-unfriendly-ly.
    //
    // This should really just be a `saturate_cast`, which does exactly
    // what we want (clamp to 0..TargetT::MAX if out of range),
    // but that's C++26, so we can't have nice things yet, and are
    // forced to roll out own, bad one.
    //
    // FIXME(C++26): use saturate_cast.
    return drainFD(fd, true, (size_t) std::max((off_t) 0, st.st_size));
}


std::string readLine(int fd)
{
    std::string s;
    while (1) {
        checkInterrupt();
        char ch;
        // FIXME: inefficient
        ssize_t rd = read(fd, &ch, 1);
        if (rd == -1) {
            if (errno != EINTR)
                throw SysError("reading a line");
        } else if (rd == 0)
            throw EndOfFile("unexpected EOF reading a line");
        else {
            if (ch == '\n') return s;
            s += ch;
        }
    }
}


void writeLine(int fd, std::string s)
{
    s += '\n';
    writeFull(fd, s);
}


void readFull(int fd, char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        ssize_t res = read(fd, buf, count);
        if (res == -1) {
            if (errno == EINTR) continue;
            throw SysError("reading from file");
        }
        if (res == 0) throw EndOfFile("unexpected end-of-file");
        count -= res;
        buf += res;
    }
}


void writeFull(int fd, std::string_view s, bool allowInterrupts)
{
    while (!s.empty()) {
        if (allowInterrupts) checkInterrupt();
        ssize_t res = write(fd, s.data(), s.size());
        if (res == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd pfd = {.fd = fd, .events = POLLOUT};
                if (poll(&pfd, 1, -1) < 0) {
                    throw SysError("polling for writing to file");
                }
            } else if (errno != EINTR) {
                throw SysError("writing to file");
            }
        }
        if (res > 0)
            s.remove_prefix(res);
    }
}


std::string drainFD(int fd, bool block, const size_t reserveSize)
{
    StringSink sink(reserveSize);
    sink << drainFDSource(fd, block);
    return std::move(sink.s);
}


Generator<Bytes> drainFDSource(int fd, bool block)
{
    // silence GCC maybe-uninitialized warning in finally
    FdBlockingState saved{};

    if (!block) {
        saved = makeNonBlocking(fd);
    }

    Finally finally([&]() {
        if (!block) {
            resetBlockingState(fd, saved);
        }
    });

    std::array<unsigned char, 64 * 1024> buf;
    while (1) {
        checkInterrupt();
        ssize_t rd = read(fd, buf.data(), buf.size());
        if (rd == -1) {
            if (!block && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            if (errno != EINTR)
                throw SysError("reading from file");
        }
        else if (rd == 0) break;
        else co_yield std::span{charptr_cast<char *>(buf.data()), (size_t) rd};
    }
}

AutoCloseFD::AutoCloseFD() : fd{-1} {}


AutoCloseFD::AutoCloseFD(int fd) : fd{fd} {}


AutoCloseFD::AutoCloseFD(AutoCloseFD && that) : fd{that.fd}
{
    that.fd = -1;
}


AutoCloseFD & AutoCloseFD::operator =(AutoCloseFD && that) noexcept(false)
{
    close();
    fd = that.fd;
    that.fd = -1;
    return *this;
}


AutoCloseFD::~AutoCloseFD()
{
    try {
        close();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}


int AutoCloseFD::get() const
{
    return fd;
}

std::string guessOrInventPathFromFD(int fd)
{
    assert(fd >= 0);
    /* On Linux, there's no F_GETPATH available.
     * But we can read /proc/ */
#if __linux__
    try {
        return readLink(fmt("/proc/self/fd/%1%", fd).c_str());
    } catch (...) {
    }
#elif defined (HAVE_F_GETPATH) && HAVE_F_GETPATH
    std::string fdName(PATH_MAX, '\0');
    if (fcntl(fd, F_GETPATH, fdName.data()) != -1) {
        fdName.resize(strlen(fdName.c_str()));
        return fdName;
    }
#else
#error "No implementation for retrieving file descriptors path."
#endif

    return fmt("<fd %i>", fd);
}


void AutoCloseFD::close()
{
    if (fd != -1) {
        if (::close(fd) == -1)
            /* This should never happen. */
            throw SysError("closing file descriptor %1%", fd);
        fd = -1;
    }
}

void AutoCloseFD::fsync()
{
  if (fd != -1) {
      int result;
#if __APPLE__
      result = ::fcntl(fd, F_FULLFSYNC);
#else
      result = ::fsync(fd);
#endif
      if (result == -1)
          throw SysError("fsync file descriptor %1%", fd);
  }
}


AutoCloseFD::operator bool() const
{
    return fd != -1;
}


int AutoCloseFD::release()
{
    int oldFD = fd;
    fd = -1;
    return oldFD;
}


void Pipe::create()
{
    int fds[2];
#if HAVE_PIPE2
    if (pipe2(fds, O_CLOEXEC) != 0) throw SysError("creating pipe");
#else
    if (pipe(fds) != 0) throw SysError("creating pipe");
    closeOnExec(fds[0]);
    closeOnExec(fds[1]);
#endif
    readSide = AutoCloseFD{fds[0]};
    writeSide = AutoCloseFD{fds[1]};
}


void Pipe::close()
{
    readSide.close();
    writeSide.close();
}


void closeExtraFDs()
{
    constexpr int MAX_KEPT_FD = 2;
    static_assert(std::max({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) == MAX_KEPT_FD);

    // Both Linux and FreeBSD support close_range.
#if __linux__ || __FreeBSD__
    auto closeRange = [](unsigned int first, unsigned int last, int flags) -> int {
      // musl does not have close_range as of 2024-08-10
      // patch: https://www.openwall.com/lists/musl/2024/08/01/9
#if HAVE_CLOSE_RANGE
        return close_range(first, last, flags);
#else
        return syscall(SYS_close_range, first, last, flags);
#endif
    };
    // first try to close_range everything we don't care about. if this
    // returns an error with these parameters we're running on a kernel
    // that does not implement close_range (i.e. pre 5.9) and fall back
    // to the old method. we should remove that though, in some future.
    if (closeRange(3, ~0U, 0) == 0) {
        return;
    }
#endif

#if __linux__
    try {
        for (auto & s : readDirectory("/proc/self/fd")) {
            auto fd = std::stoi(s.name);
            if (fd > MAX_KEPT_FD) {
                debug("closing leaked FD %d", fd);
                close(fd);
            }
        }
        return;
    } catch (SysError &) {
    }
#endif

    int maxFD = 0;
    maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = MAX_KEPT_FD + 1; fd < maxFD; ++fd)
        close(fd); /* ignore result */
}


void closeOnExec(int fd)
{
    int prev;
    if ((prev = fcntl(fd, F_GETFD, 0)) == -1 ||
        fcntl(fd, F_SETFD, prev | FD_CLOEXEC) == -1)
        throw SysError("setting close-on-exec flag");
}

FdBlockingState makeNonBlocking(int fd)
{
    const auto oldFlags = fcntl(fd, F_GETFL);
    if (oldFlags < 0 || fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK) == -1) {
        throw SysError("makeNonBlocking");
    }
    return FdBlockingState(oldFlags & O_NONBLOCK);
}

FdBlockingState makeBlocking(int fd)
{
    const auto oldFlags = fcntl(fd, F_GETFL);
    if (fcntl(fd, F_SETFL, oldFlags & ~O_NONBLOCK) == -1) {
        throw SysError("makeBlocking");
    }
    return FdBlockingState(oldFlags & O_NONBLOCK);
}

void resetBlockingState(int fd, FdBlockingState prevState)
{
    const auto oldFlags = fcntl(fd, F_GETFL);
    if (oldFlags < 0 || fcntl(fd, F_SETFL, (oldFlags & ~O_NONBLOCK) | int(prevState)) == -1) {
        throw SysError("resetBlockingState");
    }
}

SocketPair SocketPair::stream()
{
    int sp[2];
#ifdef SOCK_CLOEXEC
    constexpr int sock_type = SOCK_STREAM | SOCK_CLOEXEC;
#else
    constexpr int sock_type = SOCK_STREAM;
#endif
    if (socketpair(AF_UNIX, sock_type, 0, sp) < 0) {
        throw SysError("socketpair()");
    }

    AutoCloseFD a(sp[0]), b(sp[1]);
#ifndef SOCK_CLOEXEC
    closeOnExec(a.get());
    closeOnExec(b.get());
#endif

    return {std::move(a), std::move(b)};
}

}
