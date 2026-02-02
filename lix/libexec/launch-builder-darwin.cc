#include "launch-builder.hh"
#include "lix/libstore/build/request.capnp.h"
#include "lix/libutil/rpc.hh"
#include <cstdlib>
#include <spawn.h>
#include <string_view>
#include <sys/sysctl.h>
#include <unistd.h>

namespace nix {

/* This definition is undocumented but depended upon by all major browsers. */
extern "C" int sandbox_init_with_parameters(
    const char * profile, uint64_t flags, const char * const parameters[], char ** errorbuf
);

bool prepareChildSetup(build::Request::Reader request)
{
    return true;
}

void finishChildSetup(build::Request::Reader request)
{
    const auto config = request.getPlatform().getDarwin();

    /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try
       different mechanisms to find temporary directories, so we want to open up a broader place for them
       to put their files, if needed. */
    auto globalTmpDir = rpc::to<std::string>(config.getGlobalTempDir());

    /* They don't like trailing slashes on subpath directives */
    if (globalTmpDir.back() == '/') {
        globalTmpDir.pop_back();
    }

    if (auto env = getenv("_NIX_TEST_NO_SANDBOX"); env && env != std::string_view("1")) {
        std::vector<const char *> sandboxArgs;
        sandboxArgs.push_back("_NIX_BUILD_TOP");
        sandboxArgs.push_back(config.getTempDir().cStr());
        sandboxArgs.push_back("_GLOBAL_TMP_DIR");
        sandboxArgs.push_back(globalTmpDir.c_str());
        if (config.getAllowLocalNetworking()) {
            sandboxArgs.push_back("_ALLOW_LOCAL_NETWORKING");
            sandboxArgs.push_back("1");
        }
        sandboxArgs.push_back(nullptr);
        // NOLINTNEXTLINE(lix-unsafe-c-calls): all of these are env names or paths
        if (sandbox_init_with_parameters(config.getSandboxProfile().cStr(), 0, sandboxArgs.data(), nullptr)) {
            writeFull(STDERR_FILENO, "failed to configure sandbox\n");
            _exit(1);
        }
    }
}

[[noreturn]]
void execBuilder(build::Request::Reader request)
{
    const auto config = request.getPlatform().getDarwin();

    posix_spawnattr_t attrp;

    if (posix_spawnattr_init(&attrp)) {
        throw SysError("failed to initialize builder");
    }

    if (posix_spawnattr_setflags(&attrp, POSIX_SPAWN_SETEXEC)) {
        throw SysError("failed to initialize builder");
    }

    const auto platform = rpc::to<std::string_view>(config.getPlatform());

    if (platform == "aarch64-darwin") {
        // Unset kern.curproc_arch_affinity so we can escape Rosetta
        int affinity = 0;
        sysctlbyname("kern.curproc_arch_affinity", nullptr, nullptr, &affinity, sizeof(affinity));

        cpu_type_t cpu = CPU_TYPE_ARM64;
        posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, nullptr);
    } else if (platform == "x86_64-darwin") {
        cpu_type_t cpu = CPU_TYPE_X86_64;
        posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, nullptr);
    }

    ExecRequest req{request};

    posix_spawn(nullptr, req.builder.c_str(), nullptr, &attrp, req.args.data(), req.envs.data());
    throw SysError(errno, std::format("running {}", req.builder));
}

}
