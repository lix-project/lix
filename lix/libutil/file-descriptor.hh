#pragma once
///@file

#include "lix/libutil/error.hh"
#include "lix/libutil/generator.hh"

namespace nix {

struct Sink;
struct Source;

/**
 * Read a line from a file descriptor.
 */
std::string readLine(int fd);

/**
 * Write a line to a file descriptor.
 */
void writeLine(int fd, std::string s);

/**
 * Read the contents of a file into a string.
 */
std::string readFile(int fd);

/**
 * Wrappers arount read()/write() that read/write exactly the
 * requested number of bytes.
 */
void readFull(int fd, char * buf, size_t count);
void writeFull(int fd, std::string_view s, bool allowInterrupts = true);

/**
 * Read a file descriptor until EOF occurs.
 */
std::string drainFD(int fd, bool block = true, const size_t reserveSize=0);


/*
 * Will attempt to guess *A* path associated that might lead to the same file as used by this
 * file descriptor.
 *
 * The returned string should NEVER be used as a valid path.
 */
std::string guessOrInventPathFromFD(int fd);

Generator<Bytes> drainFDSource(int fd, bool block = true);

class AutoCloseFD
{
    int fd;
public:
    AutoCloseFD();
    explicit AutoCloseFD(int fd);
    AutoCloseFD(const AutoCloseFD & fd) = delete;
    AutoCloseFD(AutoCloseFD&& fd);
    ~AutoCloseFD();
    AutoCloseFD& operator =(const AutoCloseFD & fd) = delete;
    AutoCloseFD& operator =(AutoCloseFD&& fd) noexcept(false);
    int get() const;

    /*
     * Will attempt to guess *A* path associated that might lead to the same file as used by this
     * file descriptor.
     *
     * The returned string should NEVER be used as a valid path.
     */
    std::string guessOrInventPath() const { return guessOrInventPathFromFD(fd); }

    explicit operator bool() const;
    int release();
    void close();
    void fsync();
    void reset() { *this = {}; }
};

class Pipe
{
public:
    AutoCloseFD readSide, writeSide;
    void create();
    void close();
};

struct SocketPair
{
    /** The two sides of the socket pair. */
    AutoCloseFD a, b;

    /** Create a unix stream socket pair with the `O_CLOEXEC` set on both ends. */
    static SocketPair stream();

private:
    SocketPair(AutoCloseFD a, AutoCloseFD b) : a(std::move(a)), b(std::move(b)) {}
};

/**
 * Close all file descriptors except stdio fds (ie 0, 1, 2).
 * Good practice in child processes.
 */
void closeExtraFDs();

/**
 * Set the close-on-exec flag for the given file descriptor.
 */
void closeOnExec(int fd);

enum class FdBlockingState : int {};

/**
 * Make the given file descriptor non-blocking. Returns the old flag set; this
 * can be passed to resetBlockingState to return the fd to its original state.
 */
FdBlockingState makeNonBlocking(int fd);
/**
 * Make the given file descriptor blocking. Returns the old flag set; it can
 * be passed to `resetBlockingState` to return the fd to its original state.
 */
FdBlockingState makeBlocking(int fd);
/**
 * Undo a `makeNonBlocking` or `makeBlocking` call.
 */
void resetBlockingState(int fd, FdBlockingState prevState);

MakeError(EndOfFile, Error);

}
