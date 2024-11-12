#pragma once
///@file

#include "lix/libstore/build/local-derivation-goal.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libstore/local-store.hh"

namespace nix {

/**
 * Linux-specific implementation of LocalStore
 */
class LinuxLocalStore : public LocalStore
{
public:
    LinuxLocalStore(const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , LocalStoreConfig(params)
        , Store(params)
        , LocalFSStore(params)
        , LocalStore(params)
    {
    }
    LinuxLocalStore(const std::string scheme, std::string path, const Params & params)
        : LinuxLocalStore(params)
    {
        throw UnimplementedError("LinuxLocalStore");
    }

private:

    void findPlatformRoots(UncheckedRoots & unchecked) override;
};

/**
 * Linux-specific implementation of LocalDerivationGoal
 */
class LinuxLocalDerivationGoal : public LocalDerivationGoal
{
public:
    using LocalDerivationGoal::LocalDerivationGoal;

private:
    /**
     * Create and populate chroot
     */
    void prepareSandbox() override;

    /**
     * Start child process in new namespaces and cgroup,
     * create /etc/passwd and /etc/group based on discovered uid/gid
     */
    Pid startChild(std::function<void()> openSlave) override;

    /**
     * Kill all processes by build user, possibly using a reused
     * cgroup if we have one
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

};

}
