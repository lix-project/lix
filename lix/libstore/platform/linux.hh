#pragma once
///@file

#include "lix/libstore/build/local-derivation-goal.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libutil/processes.hh"

namespace nix {

/**
 * Linux-specific implementation of LocalStore
 */
class LinuxLocalStore : public LocalStore
{
public:
    LinuxLocalStore(LocalStoreConfig config) : Store(config), LocalStore(config) {}
    LinuxLocalStore(const std::string scheme, std::string path, LocalStoreConfig config)
        : LinuxLocalStore(config)
    {
        throw UnimplementedError("LinuxLocalStore");
    }

private:

    kj::Promise<Result<void>> findPlatformRoots(UncheckedRoots & unchecked) override;
};

/**
 * Linux-specific implementation of LocalDerivationGoal
 */
class LinuxLocalDerivationGoal : public LocalDerivationGoal
{
public:
    using LocalDerivationGoal::LocalDerivationGoal;

    ~LinuxLocalDerivationGoal();

    // NOTE these are all C strings because macos doesn't have constexpr std::string
    // constructors, and std::string_view is a pain to turn into std::strings again.
    static constexpr const char * PASTA_NS_IFNAME = "eth0";
    static constexpr const char * PASTA_HOST_IPV4 = "169.254.1.1";
    static constexpr const char * PASTA_CHILD_IPV4 = "169.254.1.2";
    static constexpr const char * PASTA_IPV4_NETMASK = "16";
    // randomly chosen 6to4 prefix, mapping the same ipv4ll as above.
    // even if this id is used on the daemon host there should not be
    // any collisions since ipv4ll should never be addressed by ipv6.
    static constexpr const char * PASTA_HOST_IPV6 = "64:ff9b:1:4b8e:472e:a5c8:a9fe:0101";
    static constexpr const char * PASTA_CHILD_IPV6 = "64:ff9b:1:4b8e:472e:a5c8:a9fe:0102";

private:
    /*
     * Destroy the cgroup otherwise another build
     * may grab the current UID which is used in the cgroup name
     * and then mess with a cgroup we might be reading statistics from.
     */
    void cleanupHookFinally() override;

    RunningProgram pastaPid;

    /**
     * Create a special accessor that can access paths that were built within the sandbox's
     * chroot.
     */
    std::optional<ref<FSAccessor>> getChrootDirAwareFSAccessor() override;

    /**
     * Create and populate chroot
     */
    void prepareSandbox() override;

    /**
     * Start child process in new namespaces,
     * create /etc/passwd and /etc/group based on discovered uid/gid
     */
    Pid startChild(std::function<void()> openSlave) override;

    /**
     * Kill all processes by build user.
     */
    void killSandbox(bool getStatus) override;

    /**
     * Set up system call filtering using seccomp, unless disabled at build time.
     * This also sets the NO_NEW_PRIVS flag.
     */
    void setupSyscallFilter() override;

    bool supportsUidRange() override
    {
        return true;
    }

    std::string rewriteResolvConf(std::string fromHost) override;
};

}
