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

    HookInstance(kj::Own<rpc::build_remote::HookInstance::Client> rpc, ProcessGroup pg)
        : rpc(std::move(rpc))
        , pgOrStatus(std::move(pg))
    {
    }
    ~HookInstance();

    int wait()
    {
        return childStatusOr<&ProcessGroup::wait>();
    }

    int kill()
    {
        return childStatusOr<&ProcessGroup::kill>();
    }

private:
    /**
     * The process group of the hook if it's running, or its exit status if not.
     */
    std::variant<ProcessGroup, int> pgOrStatus;

    template<int (ProcessGroup::*fn)()>
    int childStatusOr()
    {
        return std::visit(
            overloaded{
                [&](ProcessGroup & pg) {
                    int status = (pg.*fn)();
                    pgOrStatus = status;
                    return status;
                },
                [](int status) { return status; },
            },
            pgOrStatus
        );
    }
};
}
