#include "lix/libstore/build/child.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/build/hook-instance.hh"
#include "lix/libutil/strings.hh"

namespace nix {

kj::Promise<Result<std::unique_ptr<HookInstance>>> HookInstance::create()
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

    /* Create a pipe to get the output of the child. */
    Pipe fromHook_;
    fromHook_.create();

    /* Create the communication pipes. */
    Pipe toHook_;
    toHook_.create();

    auto [selfRPC, hookRPC] = SocketPair::stream();

    printMsg(lvlChatty, "running build hook: %s", concatMapStringsSep(" ", args, shellEscape));

    /* Fork the hook. */
    auto pid = startProcess([&]() {
        if (dup2(fromHook_.writeSide.get(), STDERR_FILENO) == -1)
            throw SysError("cannot pipe standard error into log file");

        commonExecveingChildInit();

        if (chdir("/") == -1) throw SysError("changing into /");

        /* Dup the communication pipes. */
        if (dup2(toHook_.readSide.get(), STDIN_FILENO) == -1) {
            throw SysError("dupping to-hook read side");
        }
        if (dup2(hookRPC.get(), STDOUT_FILENO) == -1) {
            throw SysError("dupping to-hook read side");
        }

        execv(buildHook.c_str(), stringsToCharPtrs(args).data());

        throw SysError("executing '%s'", buildHook);
    });

    pid.setSeparatePG(true);

    std::map<std::string, Config::SettingInfo> settings;
    globalConfig.getSettings(settings, true);
    FdSink sink(toHook_.writeSide.get());
    for (auto & setting : settings) {
        sink << 1 << setting.first << setting.second.value;
    }
    sink << 0;
    sink.flush();

    co_return std::make_unique<HookInstance>(
        std::move(fromHook_.readSide), std::move(selfRPC), std::move(pid)
    );
} catch (...) {
    co_return result::current_exception();
}

HookInstance::~HookInstance()
{
    try {
        if (pid) pid.kill();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

}
