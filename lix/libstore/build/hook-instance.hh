#pragma once
///@file

#include "hook-instance.capnp.h"
#include "lix/libutil/logging.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/serialise.hh"
#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <memory>
#include <utility>

namespace nix {

struct HookInstance
{
    /**
     * Pipe for the hook's standard output/error.
     */
    AutoCloseFD fromHook;

    kj::Own<kj::AsyncCapabilityStream> conn;
    std::unique_ptr<capnp::TwoPartyClient> client;
    rpc::build_remote::HookInstance::Client rpc;

    /**
     * The process ID of the hook.
     */
    Pid pid;

    std::map<ActivityId, Activity> activities;

    static kj::Promise<Result<std::unique_ptr<HookInstance>>> create();

    HookInstance(
        AutoCloseFD fromHook,
        kj::Own<kj::AsyncCapabilityStream> conn,
        std::unique_ptr<capnp::TwoPartyClient> client,
        rpc::build_remote::HookInstance::Client rpc,
        Pid pid
    )
        : fromHook(std::move(fromHook))
        , conn(std::move(conn))
        , client(std::move(client))
        , rpc(std::move(rpc))
        , pid(std::move(pid))
    {
    }
    ~HookInstance();
};
}
