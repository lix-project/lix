#pragma once
///@file

#include "lix/libstore/build/request.capnp.h"
#include <boost/format.hpp>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace nix {
bool prepareChildSetup(nix::build::Request::Reader request);
void finishChildSetup(nix::build::Request::Reader request);

[[noreturn]]
void execBuilder(nix::build::Request::Reader request);

// silence the foreign exception lint for this helper
class BaseException : public std::exception
{};

class SysError : public BaseException
{
private:
    std::shared_ptr<std::string> msg;

public:
    explicit SysError(auto fmt, const auto &... args) : SysError(errno, fmt, args...) {}
    SysError(int error, auto fmt, const auto &... args)
    {
        const auto errstr = strerror(error);
        auto format = boost::format(fmt);
        ((format % args), ...);
        msg = std::make_shared<std::string>(format.str() + ": " + errstr);
    }

    const char * what() const noexcept override
    {
        return msg->c_str();
    }
};

struct ExecRequest
{
    std::string builder;
    std::vector<std::string> argsStorage, envsStorage;
    std::vector<char *> args, envs;

    ExecRequest(nix::build::Request::Reader request);
};

void writeFull(int fd, std::string_view data);

extern bool printDebugLogs;

inline void printDebugLog(auto fmt, const auto &... args)
{
    auto format = boost::format(fmt);
    ((format % args), ...);
    writeFull(STDERR_FILENO, format.str());
}

#define debug(msg, ...)                           \
    do {                                          \
        if (::nix::printDebugLogs) {              \
            printDebugLog(msg "\n", __VA_ARGS__); \
        }                                         \
    } while (0)
}
