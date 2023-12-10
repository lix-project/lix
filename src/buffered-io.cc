#include "buffered-io.hh"
#include <string.h>
#include <unistd.h>
#include <nix/signals.hh>

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

    // Remove trailing newline
    return std::string_view(buffer, read - 1);
}
