#include "lix/libstore/build/child.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/build/hook-instance.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/rpc.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/logging-rpc.hh" // IWYU pragma: keep
#include "lix/libutil/types-rpc.hh" // IWYU pragma: keep
#include <kj/memory.h>
#include <memory>
#include <string_view>

namespace nix {

void HookInstance::HookLogger::emitLog(rpc::log::Event::Result::Reader r)
{
    auto type = rpc::log::from(r.getType());
    auto fields = r.getFields();
    if (!type) {
        return;
    }

    // ensure that logs from a builder using `ssh-ng://` as protocol
    // are also available to `nix log`.
    if (type == resBuildLogLine) {
        if (fields.size() > 0 && fields[0].isS()) {
            (*logSink)(fmt("%s\n", rpc::to<std::string_view>(fields[0].getS())));
        } else {
            (*logSink)("\n");
        }
    } else if (type == resSetPhase && fields.size() > 0 && fields[0].isS()) {
        // nixpkgs' stdenv produces lines in the log to signal phase changes.
        // We want to get the same lines in case of remote builds.
        // The format is:
        //   @nix { "action": "setPhase", "phase": "$curPhase" }
        const auto phase = rpc::to<std::string_view>(fields[0].getS());
        const auto logLine = JSON::object({{"action", "setPhase"}, {"phase", phase}});
        (*logSink)("@nix " + logLine.dump(-1, ' ', false, JSON::error_handler_t::replace) + "\n");
    }
}

kj::Promise<void> HookInstance::HookLogger::push(PushContext context)
{
    try {
        auto e = context.getParams().getE();
        if (logSink && e.isResult()) {
            emitLog(e.getResult());
        }
    } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
        printError("error in log processor: %s", e.what());
        throw; // NOLINT(lix-foreign-exceptions)
    }

    return RpcLoggerServer::push(context);
}

kj::Promise<Result<std::unique_ptr<HookInstance>>> HookInstance::create(const Activity & act)
try {
    debug("starting build hook '%s'", concatStringsSep(" ", settings.buildHook.get()));

    auto buildHookArgs = settings.buildHook.get();

    if (buildHookArgs.empty())
        throw Error("'build-hook' setting is empty");

    auto buildHook = canonPath(buildHookArgs.front());
    buildHookArgs.pop_front();

    Strings args;
    args.push_back(std::string(baseNameOf(buildHook)));

    for (auto & arg : buildHookArgs)
        args.push_back(arg);

    args.push_back(std::to_string(verbosity));

    /* Create the communication pipes. */
    auto [selfRPC, hookRPC] = SocketPair::stream();

    printMsg(lvlChatty, "running build hook: %s", concatMapStringsSep(" ", args, shellEscape));

    /* Fork the hook. */
    auto pid = startProcess([&]() {
        commonExecveingChildInit();

        if (chdir("/") == -1) throw SysError("changing into /");

        /* Dup the communication pipes. */
        if (dup2(hookRPC.get(), STDOUT_FILENO) == -1) {
            throw SysError("dupping to-hook read side");
        }

        sys::execv(buildHook, args);

        throw SysError("executing '%s'", buildHook);
    });

    pid.setSeparatePG(true);

    std::map<std::string, Config::SettingInfo> settings;
    globalConfig.getSettings(settings, true);

    auto conn = AIO().lowLevelProvider.wrapUnixSocketFd(kj::AutoCloseFd(selfRPC.release()));
    auto client = std::make_unique<capnp::TwoPartyClient>(*conn, 1);
    auto rpc = client->bootstrap().castAs<rpc::build_remote::HookInstance>();

    {
        auto initReq = rpc.initRequest();
        initReq.setLogger(kj::heap<HookLogger>(act, nullptr));
        RPC_FILL(initReq, initSettings, settings);
        TRY_AWAIT_RPC(initReq.send());
    }

    co_return std::make_unique<HookInstance>(
        kj::heap(std::move(rpc)).attach(std::move(conn), std::move(client)), std::move(pid)
    );
} catch (...) {
    co_return result::current_exception();
}

HookInstance::~HookInstance()
{
    try {
        kill();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

}
