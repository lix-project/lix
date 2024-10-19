#include "child.hh"
#include "error.hh"
#include "file-system.hh"
#include "globals.hh"
#include "hook-instance.hh"
#include "strings.hh"

namespace nix {

HookInstance::HookInstance()
{
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

    /* Create a pipe to get the output of the builder. */
    Pipe builderOut_;
    builderOut_.create();

    /* Fork the hook. */
    pid = startProcess([&]() {

        if (dup2(fromHook_.writeSide.get(), STDERR_FILENO) == -1)
            throw SysError("cannot pipe standard error into log file");

        commonExecveingChildInit();

        if (chdir("/") == -1) throw SysError("changing into /");

        /* Dup the communication pipes. */
        if (dup2(toHook_.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("dupping to-hook read side");

        /* Use fd 4 for the builder's stdout/stderr. */
        if (dup2(builderOut_.writeSide.get(), 4) == -1)
            throw SysError("dupping builder's stdout/stderr");

        /* Hack: pass the read side of that fd to allow build-remote
           to read SSH error messages. */
        if (dup2(builderOut_.readSide.get(), 5) == -1)
            throw SysError("dupping builder's stdout/stderr");

        execv(buildHook.c_str(), stringsToCharPtrs(args).data());

        throw SysError("executing '%s'", buildHook);
    });

    pid.setSeparatePG(true);
    fromHook = std::move(fromHook_.readSide);
    toHook = std::move(toHook_.writeSide);
    builderOut = std::move(builderOut_.readSide);

    sink = FdSink(toHook.get());
    std::map<std::string, Config::SettingInfo> settings;
    globalConfig.getSettings(settings);
    for (auto & setting : settings)
        sink << 1 << setting.first << setting.second.value;
    sink << 0;
}


HookInstance::~HookInstance()
{
    try {
        toHook.reset();
        if (pid) pid.kill();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

}
