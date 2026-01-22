#pragma once
///@file

#include "hook-instance.capnp.h"
#include "lix/libutil/logging-rpc.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/serialise.hh"
#include "logging.capnp.h"
#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <memory>
#include <utility>

namespace nix {

struct HookInstance
{
    struct HookLogger : rpc::log::RpcLoggerServer
    {
        FinishSink * logSink;

        HookLogger(const Activity & act, FinishSink * logSink)
            : rpc::log::RpcLoggerServer(act)
            , logSink(logSink)
        {
        }

        void emitLog(rpc::log::Event::Result::Reader r);
        kj::Promise<void> push(PushContext context) override;
    };

    kj::Own<rpc::build_remote::HookInstance::Client> rpc;

    static kj::Promise<Result<std::unique_ptr<HookInstance>>> create(const Activity & act);

    HookInstance(kj::Own<rpc::build_remote::HookInstance::Client> rpc, RunningHelper hook)
        : rpc(std::move(rpc))
        , hookOrStatus(std::move(hook))
    {
    }
    ~HookInstance();

    int wait()
    {
        return childStatusOr([](auto & p) { return p.wait(); });
    }

    int kill()
    {
        return childStatusOr([](auto & p) { return p.killProcessGroup(); });
    }

private:
    /**
     * The process of the hook if it's running, or its exit status if not.
     */
    std::variant<RunningHelper, int> hookOrStatus;

    int childStatusOr(auto ifRunning)
    {
        return std::visit(
            overloaded{
                [&](RunningHelper & hook) {
                    int status = ifRunning(hook);
                    hookOrStatus = status;
                    return status;
                },
                [](int status) { return status; },
            },
            hookOrStatus
        );
    }
};
}
