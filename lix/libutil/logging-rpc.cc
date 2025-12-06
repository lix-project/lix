#include "logging-rpc.hh"
#include "types-rpc.hh" // IWYU pragma: keep
#include "sync.hh"
#include "types.hh"
#include <cstdlib>
#include <kj/exception.h>

namespace nix {
namespace {
struct RpcLogger : Logger
{
    struct Log
    {
        Verbosity level;
        std::string msg;
    };
    struct LogEI
    {
        ErrorInfo ei;
    };
    struct StartActivity
    {
        Verbosity level;
        uint64_t id;
        ActivityType type;
        std::string text;
        uint64_t parent;
        Logger::Fields fields;
    };
    struct StopActivity
    {
        uint64_t id;
    };
    struct ActivityResult
    {
        uint64_t id;
        ResultType type;
        Logger::Fields fields;
    };

    using Event = std::variant<Log, LogEI, StartActivity, StopActivity, ActivityResult>;

    struct Buffer
    {
        std::vector<Event> items;
        /// *very* rough approximation of how much memory our buffer uses. this
        /// should not be an exact byte count to keep accounting simple, but it
        /// should still be roughly representative of reality. a small constant
        /// error factor during average execution is acceptable, expected even.
        size_t sizeEstimate;

        /// non-null if a remote log operation failed. we'll rethrow it blindly
        /// every time the buffers are flushed, but not during message enqueue.
        /// this hopefully avoids crashing due to recursive exception handling.
        std::exception_ptr failure;

        auto take()
        {
            if (failure) {
                std::rethrow_exception(failure);
            }
            sizeEstimate = 0;
            return std::move(items);
        }
    };

    rpc::log::LogStream::Client remote;
    Sync<Buffer> buffer;
    Sync<std::list<kj::Own<kj::PromiseFulfiller<void>>>, AsyncMutex> flushReq;
    kj::Promise<void> flusher;

    RpcLogger(rpc::log::LogStream::Client remote)
        : remote(remote)
        , flusher(flushLoop().eagerlyEvaluate([](kj::Exception && e) {
            if (e.getType() == kj::Exception::Type::DISCONNECTED) {
                std::cerr << "peer disconnected, exiting with haste\n";
                std::exit(90);
            } else {
                std::cerr << "log flusher failed catastrophically!\n";
                // rethrow the exception and terminate to cause a core dump and stack trace
                try {
                    kj::throwFatalException(std::move(e));
                } catch (...) {
                    std::terminate();
                }
            }
        }))
    {
    }

    ~RpcLogger() noexcept = default;

    static size_t fieldSize(const Logger::Fields & fields)
    {
        size_t size = 0;
        for (auto & f : fields) {
            size += sizeof(f) + f.s.size();
        }
        return size;
    }

    BufferState push(size_t extraSize, Event e)
    {
        auto buffer = this->buffer.lock();
        if (buffer->failure) {
            return BufferState::HasSpace;
        }
        buffer->sizeEstimate += sizeof(e) + extraSize;
        buffer->items.emplace_back(std::move(e));
        return buffer->sizeEstimate >= static_cast<size_t>(1024 * 1024) ? BufferState::NeedsFlush
                                                                        : BufferState::HasSpace;
    }

    BufferState log(Verbosity lvl, std::string_view s) override
    {
        return push(s.size(), Log{lvl, std::string(s)});
    }

    BufferState logEI(const ErrorInfo & ei) override
    {
        // size is just a guess. errors are usually rare and small
        return push(1024, LogEI{ei});
    }

    BufferState startActivityImpl(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        const Fields & fields,
        ActivityId parent
    ) override
    {
        return push(fieldSize(fields), StartActivity{lvl, act, type, s, parent, fields});
    }

    BufferState stopActivityImpl(ActivityId act) override
    {
        return push(0, StopActivity{act});
    }

    BufferState resultImpl(ActivityId act, ResultType type, const Fields & fields) override
    {
        return push(fieldSize(fields), ActivityResult{act, type, fields});
    }

    kj::Promise<Result<void>> flush() override
    try {
        auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();
        (co_await flushReq.lock())->emplace_back(std::move(pfp.fulfiller));
        flushReq.notify();
        co_await pfp.promise;
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    static void fillEventArg(rpc::log::Event::Builder arg, const Event & e)
    {
        overloaded handlers{
            [&](const Log & l) {
                arg.initLog().setLevel(rpc::Verbosity(l.level));
                RPC_FILL(arg.getLog(), setMsg, l.msg);
            },
            [&](const LogEI & l) {
                auto ei = arg.initLogEI();
                RPC_FILL(ei, initInfo, l.ei);
            },
            [&](const StartActivity & s) {
                auto sa = arg.initStartActivity();
                sa.setLevel(rpc::Verbosity(s.level));
                sa.setId(s.id);
                sa.setType(rpc::log::to(s.type));
                RPC_FILL(sa, setText, s.text);
                sa.setParent(s.parent);
                RPC_FILL(sa, initFields, s.fields);
            },
            [&](const StopActivity & s) { arg.initStopActivity().setId(s.id); },
            [&](const ActivityResult & r) {
                auto ar = arg.initResult();
                ar.setId(r.id);
                ar.setType(rpc::log::to(r.type));
                RPC_FILL(ar, initFields, r.fields);
            },
        };
        std::visit(handlers, e);
    }

    kj::Promise<void> flushLoop()
    {
        auto req = co_await flushReq.lock();
        std::optional<kj::Exception> failure;

        while (true) {
            kj::Own<kj::PromiseFulfiller<void>> f;
            if (req->empty()) {
                co_await req.waitFor(100 * kj::MILLISECONDS);
            }
            if (!req->empty()) {
                f = std::move(req->front());
                req->pop_front();
            }

            try {
                auto buffer = this->buffer.lock()->take();
                for (auto & e : buffer) {
                    auto req = remote.pushRequest();
                    fillEventArg(req.initE(), e);
                    co_await req.send();
                }
                co_await remote.synchronizeRequest().send();
                if (f) {
                    f->fulfill();
                }
            } catch (...) {
                this->buffer.lock()->failure = std::current_exception();
                failure.emplace(kj::getCaughtExceptionAsKj());
                if (f) {
                    f->reject(auto(*failure));
                }
                break;
            }
        }

        while (true) {
            while (req->empty()) {
                co_await req.wait();
            }
            req->front()->reject(auto(*failure));
            req->pop_front();
        }
    }

    void waitForSpace(NeverAsync) override
    {
        struct Fulfiller final : kj::PromiseFulfiller<void>
        {
            std::shared_ptr<std::atomic_flag> flag;

            Fulfiller(std::shared_ptr<std::atomic_flag> flag) : flag(flag) {}

            void fulfill(kj::_::Void && value = {}) override
            {
                flag->clear();
                flag->notify_all();
            }

            void reject(kj::Exception && exception) override
            {
                fulfill();
            }

            bool isWaiting() override
            {
                return flag->test();
            }
        };

        auto flag = std::make_shared<std::atomic_flag>(true);

        flushReq.lockSync()->emplace_back(kj::heap<Fulfiller>(flag));
        flushReq.notify();
        flag->wait(true);
    }
};
}
}

namespace nix::rpc::log {
Logger * makeRpcLoggerClient(LogStream::Client remote)
{
    return new RpcLogger(remote);
}

kj::Promise<void> RpcLoggerServer::push(PushContext context)
try {
    auto state = Logger::BufferState::HasSpace;

    auto e = context.getParams().getE();
    if (e.isStartActivity()) {
        auto args = e.getStartActivity();
        activities.emplace(
            args.getId(),
            parent.addChild(
                nix::Verbosity(args.getLevel()),
                from(args.getType()).value_or(actUnknown),
                rpc::to<std::string>(args.getText()),
                rpc::to<Logger::Fields>(args.getFields())
            )
        );
    } else if (e.isStopActivity()) {
        activities.erase(e.getStopActivity().getId());
    } else if (e.isResult()) {
        auto args = e.getResult();
        if (auto type = rpc::log::from(args.getType())) {
            if (auto act = get(activities, args.getId())) {
                state = act->result(*type, rpc::to<Logger::Fields>(args.getFields()));
            }
        } else {
            debug("got unintellegible result message %s", args.toString().flatten().cStr());
        }
    } else if (e.isLog()) {
        state = logger->log(
            nix::Verbosity(e.getLog().getLevel()), rpc::to<std::string_view>(e.getLog().getMsg())
        );
    } else if (e.isLogEI()) {
        state = logger->logEI(from(e.getLogEI().getInfo()));
    } else {
        debug("got unintellegible log message %s", e.toString().flatten().cStr());
    }

    if (state == Logger::BufferState::NeedsFlush) {
        TRY_AWAIT(parent.getLogger().flush());
    }
} catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
    printError("error in log processor: %s", e.what());
    throw; // NOLINT(lix-foreign-exceptions)
}

kj::Promise<void> RpcLoggerServer::synchronize(SynchronizeContext context)
{
    return kj::READY_NOW;
}
}
