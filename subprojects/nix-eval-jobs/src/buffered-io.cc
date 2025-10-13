#include <string.h>
#include <unistd.h>
#include <lix/libutil/signals.hh>
#include <errno.h>
#include <stdlib.h>
#include <lix/libutil/error.hh>

#include "buffered-io.hh"

[[nodiscard]] int tryWriteLine(int fd, std::string s) {
    s += "\n";
    std::string_view sv{s};
    while (!sv.empty()) {
        nix::checkInterrupt();
        ssize_t res = write(fd, sv.data(), sv.size());
        if (res == -1 && errno != EINTR) {
            return -errno;
        }
        if (res > 0) {
            sv.remove_prefix(res);
        }
    }
    return 0;
}

LineReader::LineReader(int fd) {
    stream = fdopen(fd, "r");
    if (!stream) {
        throw nix::Error("fdopen failed: %s", strerror(errno));
    }
}

LineReader::~LineReader() {
    fclose(stream);
    free(buffer);
}

LineReader::LineReader(LineReader &&other) {
    stream = other.stream;
    other.stream = nullptr;
    buffer = other.buffer;
    other.buffer = nullptr;
    len = other.len;
    other.len = 0;
}

[[nodiscard]] std::string_view LineReader::readLine() {
    ssize_t read = getline(&buffer, &len, stream);

    if (read == -1) {
        return {}; // Return an empty string_view in case of error
    }

    nix::checkInterrupt();

    // Remove trailing newline
    return std::string_view(buffer, read - 1);
}

AsyncLineReader::AsyncLineReader(nix::AutoCloseFD fd)
    : stream{std::move(fd)}, readBuffer{kj::heapArray<char>(4096)} {}

kj::Promise<nix::Result<std::optional<std::string>>> AsyncLineReader::readLine()
try {
    auto pos = buffer.find('\n');
    if (pos != std::string::npos) {
        std::string result = buffer.substr(0, pos);
        memmove(buffer.data(), buffer.data() + pos + 1, buffer.size() - pos - 1);
        buffer.resize(buffer.size() - pos - 1);
        co_return result;
    }

    // No full line was buffered, read until we have one.
    while (true) {
        auto nRead = LIX_TRY_AWAIT(stream.read(readBuffer.begin(), readBuffer.size()));
        if (!nRead && !buffer.empty()) {
            // File has ended, but not everything has been read out of the buffer yet.
            co_return std::move(buffer);
        } else if (!nRead) {
            co_return std::nullopt;
        }
        std::string_view readStr{readBuffer.begin(), *nRead};

        auto pos = readStr.find('\n');
        if (pos != std::string_view::npos) {
            buffer.append(readStr.substr(0, pos));
            co_return std::exchange(buffer, readStr.substr(pos + 1));
        }
        buffer.append(readStr);
    }
} catch (...) {
    co_return nix::result::current_exception();
}
