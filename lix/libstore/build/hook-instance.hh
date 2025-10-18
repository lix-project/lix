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
        , pidOrStatus(std::move(pid))
    {
    }
    ~HookInstance();

    int wait()
    {
        return childStatusOr<&Pid::wait>();
    }

    int kill()
    {
        return childStatusOr<&Pid::kill>();
    }

private:
    /**
     * The process ID of the hook if it's running, or its exit status if not.
     */
    std::variant<Pid, int> pidOrStatus;

    template<int (Pid::*fn)()>
    int childStatusOr()
    {
        return std::visit(
            overloaded{
                [&](Pid & pid) {
                    int status = (pid.*fn)();
                    pidOrStatus = status;
                    return status;
                },
                [](int status) { return status; },
            },
            pidOrStatus
        );
    }
};
}
