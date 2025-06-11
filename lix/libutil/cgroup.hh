#pragma once
///@file

#include "file-system.hh"
#if __linux__

#include <chrono>
#include <optional>
#include <filesystem>
#include <kj/common.h>

#include "lix/libutil/types.hh"

namespace nix {

/* This represent a cgroup hierarchy.
 * When doing cgroup delegation, it is necessary to ensure that all processes
 * lives in the leaves of the cgroup tree.
 *
 * When Nix runs a build, the cgroup tree should be arranged so that:
 *
 *           Nix parent process cgroup (e.g. `system.slice` on systemd)
 *                          |
 *                          |
 *           nix-daemon.service cgroup
 *                         / \
 *                        /   \
 *                       /     \
 *                      /       \
 *                     /         \
 *                    /           \
 * nix-build-uid-418238 cgroup     supervisor cgroup
 *            |                       |
 *  derivation build processes        |
 *                              nix-daemon subdaemons
 *
 *  Notice that if you put the nix-daemon subdaemons directly inside the `nix-daemon.service`
 * cgroup, then there would be processes in _inner cgroups_, which is a violation of cgroup best
 * practices (it opens up the path for children to compete for cgroup control).
 *
 *  If you run systemd with `Delegate` and `DelegateSubgroup` options, the hierarchy is setup
 *  like the above automatically.
 *
 *  If you are not in these conditions, you will need to write code to move the various daemon-like
 * processes inside a sibling cgroup.
 *
 */
struct CgroupHierarchy
{
    std::filesystem::path ourCgroupPath;
    std::optional<std::filesystem::path> parentCgroupPath() const
    {
        if (ourCgroupPath.has_parent_path()) {
            return ourCgroupPath.parent_path();
        } else {
            return {};
        }
    }
};

/* Return the current process's view of the cgroup hierarchy, i.e.
 * the parent process's cgroup and this current process's cgroup.
 * */
CgroupHierarchy getLocalHierarchy(std::filesystem::path const & cgroupFilesystem);

/* Return a path to the cgroupv2 filesystem path, if it exist */
std::optional<std::filesystem::path> getCgroupFS();

/* Help detect the list of feature sets available
 * for the running kernel. */
enum class CgroupAvailableFeatureSet : uint8_t {
    /* cgroupvs2 were detected. */
    CGROUPV2 = (1 << 0),
    /* cgroupsv2 kill capability detected. The absence of this capability is unsupported.
     * It appeared in kernel 5.14. */
    CGROUPV2_KILL = (1 << 1),
    /* Current process is delegated à la systemd-style, e.g.
     * `user.delegate=1` was written as an xattr of the cgroup directory. */
    CGROUPV2_SELF_DELEGATED = (1 << 2),
    /* Current process' parent cgroup was delegated à la systemd-style. */
    CGROUPV2_PARENT_DELEGATED = (1 << 3),
};

CgroupAvailableFeatureSet operator|(CgroupAvailableFeatureSet lhs, CgroupAvailableFeatureSet rhs);
CgroupAvailableFeatureSet &
operator|=(CgroupAvailableFeatureSet & lhs, CgroupAvailableFeatureSet rhs);
CgroupAvailableFeatureSet operator&(CgroupAvailableFeatureSet lhs, CgroupAvailableFeatureSet rhs);

/* Whether the `featureSet` given do possess the `testedFeature` in the set? */
bool hasCgroupFeature(
    CgroupAvailableFeatureSet featureSet, CgroupAvailableFeatureSet testedFeature
);

/* Lix cgroup support relies on certain modern features to be available to avoid implementing many
 * legacy code paths. */
CgroupAvailableFeatureSet detectAvailableCgroupFeatures();

struct CgroupStats
{
    std::optional<std::chrono::microseconds> cpuUser, cpuSystem;
};

/**
 * RAII class to hold an owned cgroup
 * which will kill all processes under its hierarchy at destruction time.
 */
class AutoDestroyCgroup
{
private:
    struct Delegation
    {
        /*
         * Delegatee UID
         */
        uid_t uid;
        /*
         * Delegatee GID
         */
        gid_t gid;
    };

    /* Friendly name of this cgroup */
    std::string name_;

    /* Enabled controllers of this cgroup */
    std::vector<std::string> controllers_;

    /* A cgroup can be delegated to a pair of UID/GID */
    std::optional<Delegation> delegation_;

    /*
     * Either the cgroup exist and is alive,
     * either the cgroup was destroyed and we collected its statistics.
     */
    std::variant<std::filesystem::path, CgroupStats> cgroup_;

    /*
     * Path to the state record of this cgroup's existence.
     * This is used when the deletion process is interrupted
     * for the next run.
     *
     * This might be deleted earlier, in which case, this is left to none.
     */
    std::optional<AutoDelete> stateRecord;

    /*
     * Cleanse all previous instances of this cgroup where the deletion process
     * might have been interrupted and record ourself in stead.
     * If this cgroup deletion process does not run, on the next run, this will be
     * reaped when the same name will be reused.
     */
    void cleansePreviousInstancesAndRecordOurself(const std::filesystem::path & cgroupRecordsDir);
public:
    KJ_DISALLOW_COPY(AutoDestroyCgroup);

    explicit AutoDestroyCgroup(
        const std::filesystem::path & cgroupRecordsDir, std::string const & name
    );

    /* Delegate this cgroup to the given UID and GID. */
    explicit AutoDestroyCgroup(
        const std::filesystem::path & cgroupRecordsDir,
        std::string const & name,
        uid_t uid,
        gid_t gid
    );
    ~AutoDestroyCgroup();

    /* Kill all processes under its hierarchy and tear down the cgroup */
    void destroy();

    std::optional<Path> path() const
    {
        return std::visit(
            nix::overloaded{
                [&](const Path & path) -> std::optional<Path> { return {path}; },
                [&](const CgroupStats &) -> std::optional<Path> { return {}; }
            },
            cgroup_
        );
    }

    const std::string & name() const
    {
        return name_;
    }

    const std::vector<std::string> & controllers() const
    {
        return controllers_;
    }

    std::optional<Delegation> delegation() const
    {
        return delegation_;
    }

    /* Adopt a process in this cgroup. */
    void adoptProcess(int pid);

    /* Kill all processes under the control group. */
    void kill();

    /* Return all statistics inside of this cgroup. */
    CgroupStats getStatistics() const;
};
}

#endif
