#pragma once
///@file

#include "hook-instance.capnp.h"
#include "lix/libutil/logging.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/serialise.hh"
#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <utility>

namespace nix {

struct HookInstance
{
    /**
     * Pipe for the hook's standard output/error.
     */
    AutoCloseFD fromHook;

    kj::Own<kj::AsyncCapabilityStream> conn;
    std::optional<capnp::TwoPartyClient> client;
    rpc::build_remote::HookInstance::Client rpc;

    /**
     * The process ID of the hook.
     */
    Pid pid;

    std::map<ActivityId, Activity> activities;

    static kj::Promise<Result<std::unique_ptr<HookInstance>>> create();

    HookInstance(AutoCloseFD fromHook, AutoCloseFD rpc, Pid pid)
        : fromHook(std::move(fromHook))
        , conn(AIO().lowLevelProvider.wrapUnixSocketFd(kj::AutoCloseFd(rpc.release())))
        , client(std::in_place, *this->conn, 1)
        , rpc(client->bootstrap().castAs<rpc::build_remote::HookInstance>())
        , pid(std::move(pid))
    {
    }
    ~HookInstance();
};
}
