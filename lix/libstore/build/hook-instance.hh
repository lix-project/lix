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

    HookInstance(kj::Own<rpc::build_remote::HookInstance::Client> rpc, Pid pid)
        : rpc(std::move(rpc))
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
