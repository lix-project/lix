#include "launch-builder.hh"
#include "lix/libstore/build/request.capnp.h"
#include <format>
#include <string>
#include <unistd.h>

namespace nix {

bool prepareChildSetup(build::Request::Reader config)
{
    return true;
}

void finishChildSetup(build::Request::Reader config) {}

void execBuilder(build::Request::Reader config)
{
    ExecRequest req{config};

    execve(req.builder.data(), req.args.data(), req.envs.data());
    throw SysError("running %s", req.builder);
}

}
