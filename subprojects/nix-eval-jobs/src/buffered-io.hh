#pragma once
#include <lix/libutil/async-io.hh>
#include <lix/libutil/file-descriptor.hh>
#include <cstdio>
#include <string>
#include <string_view>

[[nodiscard]] int tryWriteLine(int fd, std::string s);

class LineReader {
  public:
    LineReader(int fd);
    ~LineReader();

    LineReader(LineReader &&other);
    [[nodiscard]] std::string_view readLine();

  private:
    FILE *stream = nullptr;
    char *buffer = nullptr;
    size_t len = 0;
};

class AsyncLineReader {
public:
    AsyncLineReader(nix::AutoCloseFD fd);

    kj::Promise<nix::Result<std::optional<std::string>>> readLine();

private:
    nix::AsyncFdIoStream stream;
    std::string buffer;
    kj::Array<char> readBuffer;
};
