#pragma once
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
