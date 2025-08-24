#include "error.hh"
#include "file-descriptor.hh"
#include "logging.hh"
#include <fcntl.h>
#include <sys/poll.h>
#if __linux__

#include "regex.hh"

#include "lix/libutil/cgroup.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/strings.hh"

#include <chrono>
#include <cmath>
#include <regex>

#include <dirent.h>
#include <mntent.h>
#include <sys/xattr.h>

namespace nix {

static bool isCgroupDelegated(const Path & path)
{
    char delegate_xattr;

    if (getxattr(path.c_str(), "user.delegate", &delegate_xattr, sizeof(delegate_xattr)) >= 1) {
        if (delegate_xattr != '1') {
            throw Error("Unexpected `user.delegate` xattr: '%c'", delegate_xattr);
        }

        return true;
    }

    return false;
}

static std::map<std::string, std::string> getCgroups(const Path & cgroupFile)
{
    std::map<std::string, std::string> cgroups;

    for (auto & line : tokenizeString<std::vector<std::string>>(readFile(cgroupFile), "\n")) {
        static std::regex regex = nix::regex::parse("([0-9]+):([^:]*):(.*)");
        std::smatch match;
        if (!std::regex_match(line, match, regex))
            throw Error("invalid line '%s' in '%s'", line, cgroupFile);

        std::string name = std::string(match[2]).starts_with("name=") ? std::string(match[2], 5) : match[2];
        cgroups.insert_or_assign(name, match[3]);
    }

    return cgroups;
}

static Result<CgroupStats> readStatistics(const std::filesystem::path & cgroup)
try {
    CgroupStats stats;
    auto cpustatPath = cgroup / "cpu.stat";

    if (pathExists(cpustatPath)) {
        for (auto & line : tokenizeString<std::vector<std::string>>(readFile(cpustatPath), "\n")) {
            std::string_view userPrefix = "user_usec ";
            if (line.starts_with(userPrefix)) {
                auto n = string2Int<uint64_t>(line.substr(userPrefix.size()));
                if (n) {
                    stats.cpuUser = std::chrono::microseconds(*n);
                }
            }

            std::string_view systemPrefix = "system_usec ";
            if (line.starts_with(systemPrefix)) {
                auto n = string2Int<uint64_t>(line.substr(systemPrefix.size()));
                if (n) {
                    stats.cpuSystem = std::chrono::microseconds(*n);
                }
            }
        }
    }

    return stats;
} catch (...) {
    return result::current_exception();
}

static void killCgroup(const std::string & name, const std::filesystem::path & cgroup)
{
    auto killFile = cgroup / "cgroup.kill";
    if (pathExists(killFile))
        writeFileUninterruptible(killFile, "1");
    else {
        throw SysError(
            "cgroup '%s' at '%s' does not possess `cgroup.kill` ; are you running Lix on a kernel "
            "older than 5.14 with cgroups?",
            name,
            cgroup
        );
    }
}

static std::optional<CgroupStats>
destroyCgroup(const std::string & name, const std::filesystem::path & aliveCgroup)
{
    debug("destroying cgroup '%s' at '%s'", name, aliveCgroup);
    if (!pathExists(aliveCgroup)) {
        debug("destroying cgroup '%s' already destroyed", name);
        return {};
    }

    auto procsFile = aliveCgroup / "cgroup.procs";

    if (!pathExists(procsFile)) {
        throw SysError(
            "cgroup '%s' at '%s' has an invalid cgroup hierarchy (missing `cgroup.procs`)",
            name,
            aliveCgroup
        );
    }

    killCgroup(name, aliveCgroup);

    // FIXME this should be done asynchronously, but we can't do that without
    // a proper cgroup management entity at a higher level (eg in Worker). we
    // can either synchronously kill the cgroup and have simple RAII classes,
    // or we can have a management entity that can asynchronously destroy our
    // cgroups since asynchronous destructors unfortunately don't exist here.
    // FIXME we should also check that no new processes are added, but we can
    // only do this without *even more* code duplication after checkInterrupt
    // has been excised. until then a timeout on group death will have to do.
    {
        auto eventsFile = aliveCgroup / "cgroup.events";
        AutoCloseFD events(open(eventsFile.c_str(), O_RDONLY));
        if (!events) {
            throw SysError("failed to open %s", eventsFile);
        }

        constexpr int WAIT_MS = 1000;
        constexpr std::chrono::seconds TIMEOUT{120};

        const auto started = std::chrono::steady_clock::now();

        do {
            // there's only two keys today, this should be fine for a while
            std::array<char, 1024> buf = {};
            const auto got = pread(events.get(), buf.data(), buf.size(), 0);
            if (got < 0) {
                throw SysError("reading %s", eventsFile);
            }
            auto lines = tokenizeString<Strings>(std::string_view{buf.data(), size_t(got)}, "\n");
            for (const auto & line : lines) {
                auto tokens = tokenizeString<Strings>(line);
                if (tokens.front() == "populated" && tokens.back() == "0") {
                    goto done;
                }
            }

            debug("cgroup %s isn't empty yet, waiting for a while", aliveCgroup);
            pollfd pfd{.fd = events.get(), .events = POLLPRI | POLLERR};
            if (poll(&pfd, 1, WAIT_MS) < 0) {
                throw SysError("polling %s", eventsFile);
            }
        } while ((std::chrono::steady_clock::now() - started) < TIMEOUT);

    done:
        // if the cgroup is still populated after waiting we just continue.
        // cleanup will fail, but we can't do any better than this for now.
    }

    Result<CgroupStats> stats = readStatistics(aliveCgroup);

    if (rmdir(aliveCgroup.c_str()) == -1) {
        throw SysError("deleting cgroup '%s' at '%s'", name, aliveCgroup);
    }

    debug("cgroup '%s' destroyed", name);

    /* Even if this contains an exception, this is fine. */
    return stats.value();
}

CgroupHierarchy getLocalHierarchy(const std::filesystem::path & cgroupFilesystem)
{
    CgroupHierarchy hierarchy;

    auto ourCgroups = getCgroups("/proc/self/cgroup");
    auto ourCgroup = ourCgroups[""];

    if (ourCgroup == "") {
        throw Error("cannot determine cgroup name from '/proc/self/cgroup'");
    }

    if (ourCgroup[0] == '/') {
        ourCgroup.erase(0, 1);
    }

    auto ourCgroupPath = (cgroupFilesystem / ourCgroup).lexically_normal();

    if (!pathExists(ourCgroupPath)) {
        throw Error("expected cgroup directory '%s'", ourCgroupPath);
    }

    hierarchy.ourCgroupPath = ourCgroupPath;

    return hierarchy;
}

CgroupAvailableFeatureSet operator|(CgroupAvailableFeatureSet lhs, CgroupAvailableFeatureSet rhs)
{
    return static_cast<CgroupAvailableFeatureSet>(
        static_cast<std::underlying_type<CgroupAvailableFeatureSet>::type>(lhs)
        | static_cast<std::underlying_type<CgroupAvailableFeatureSet>::type>(rhs)
    );
}

CgroupAvailableFeatureSet &
operator|=(CgroupAvailableFeatureSet & lhs, CgroupAvailableFeatureSet rhs)
{
    return lhs = lhs | rhs;
}

CgroupAvailableFeatureSet operator&(CgroupAvailableFeatureSet lhs, CgroupAvailableFeatureSet rhs)
{
    return static_cast<CgroupAvailableFeatureSet>(
        static_cast<std::underlying_type<CgroupAvailableFeatureSet>::type>(lhs)
        & static_cast<std::underlying_type<CgroupAvailableFeatureSet>::type>(rhs)
    );
}

bool hasCgroupFeature(CgroupAvailableFeatureSet featureSet, CgroupAvailableFeatureSet testedFeature)
{
    return static_cast<std::underlying_type<CgroupAvailableFeatureSet>::type>(
               featureSet & testedFeature
           )
        != 0;
}

CgroupAvailableFeatureSet detectAvailableCgroupFeatures()
{
    CgroupAvailableFeatureSet features = {};

    auto fs = getCgroupFS();

    if (fs && !fs->empty()) {
        features |= CgroupAvailableFeatureSet::CGROUPV2;

        auto localHierarchy = getLocalHierarchy(*fs);
        if (pathExists(localHierarchy.ourCgroupPath / "cgroup.kill")) {
            features |= CgroupAvailableFeatureSet::CGROUPV2_KILL;
        }

        if (isCgroupDelegated(localHierarchy.ourCgroupPath)) {
            features |= CgroupAvailableFeatureSet::CGROUPV2_SELF_DELEGATED;
        }

        auto parentCgroupPath = localHierarchy.parentCgroupPath();
        if (parentCgroupPath && isCgroupDelegated(*parentCgroupPath)) {
            features |= CgroupAvailableFeatureSet::CGROUPV2_PARENT_DELEGATED;
        }
    }

    return features;
}

static std::vector<std::string> readControllers(const std::filesystem::path & cgroupPath)
{
    return tokenizeString<std::vector<std::string>>(
        readFile(cgroupPath / "cgroup.controllers"), " "
    );
}

AutoDestroyCgroup::AutoDestroyCgroup(
    const std::filesystem::path & cgroupRecordsDir, std::string const & name
)
    : name_(name)
{
    auto cgroupFilesystem = getCgroupFS();
    if (!cgroupFilesystem || cgroupFilesystem->empty()) {
        throw Error("cannot determine the path to the cgroupv2 filesystem");
    }

    auto hierarchy = getLocalHierarchy(*cgroupFilesystem);
    auto parentCgroupPath = hierarchy.parentCgroupPath();
    assert(parentCgroupPath && "AutoDestroyCgroup cannot be used on the root cgroup");
    /* We assert that the parent cgroup is delegated at this point.
     * This is a responsibility of the caller. */
    assert(isCgroupDelegated(*parentCgroupPath) && "parent cgroup was supposed to be delegated");

    /* All available controllers on the parent cgroup path will be delegated.
     * TODO(raito): implementing a filtering mechanism is for the future. */
    controllers_ = readControllers(*parentCgroupPath);

    /* Enable all the controllers */
    writeFile(
        *parentCgroupPath / "cgroup.subtree_control",
        concatMapStringsSep(
            " ",
            controllers_,
            [](const std::string & controller) -> std::string { return fmt("+%s", controller); }
        )
    );

    cgroup_ = *parentCgroupPath / name;

    /*
     * In case we get interrupted without cleaning up the cgroup we just created,
     * we look at Nix's state directory where we record all cgroups being used
     * and destroy it before reusing it. */
    cleansePreviousInstancesAndRecordOurself(cgroupRecordsDir);
}

AutoDestroyCgroup::AutoDestroyCgroup(
    const std::filesystem::path & cgroupRecordsDir, std::string const & name, uid_t uid, gid_t gid
)
    : AutoDestroyCgroup(cgroupRecordsDir, name)
{
    auto path = std::get<std::filesystem::path>(cgroup_);

    if (mkdir(path.c_str(), 0755) == -1) {
        throw SysError(
            "cannot create the top-level directory at '%s' for cgroup '%s'", path, name_
        );
    }

    if (chown(path.c_str(), uid, gid) == -1) {
        throw SysError(
            "cannot delegate the top-level directory '%s' from cgroup '%s' to user uid=%d,gid=%d",
            path,
            name_,
            uid,
            gid
        );
    }

    AutoCloseFD cgroupFd{open(path.c_str(), O_PATH | O_NOFOLLOW)};
    for (auto node : {"procs", "threads", "subtree_control"}) {
        if (fchownat(cgroupFd.get(), fmt("cgroup.%s", node).c_str(), uid, gid, 0) == -1) {
            throw SysError(
                "cannot delegate '%s' from cgroup '%s' to user uid=%d,gid=%d", node, name_, uid, gid
            );
        }
    }

    delegation_ = {.uid = uid, .gid = gid};
}

void AutoDestroyCgroup::destroy()
{
    std::visit(
        overloaded{
            [&, this](const Path & aliveCgroup) {
                auto maybeStats = destroyCgroup(name_, aliveCgroup);
                if (!maybeStats) {
                    printTaggedWarning(
                        "cgroup '%s' was destroyed unexpectedly (something else removed the "
                        "cgroup).",
                        aliveCgroup
                    );
                } else {
                    cgroup_ = *maybeStats;
                }
            },
            [&](const CgroupStats & stats) {}
        },
        cgroup_
    );

    stateRecord.reset();
}

AutoDestroyCgroup::~AutoDestroyCgroup()
{
    try {
        destroy();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void AutoDestroyCgroup::cleansePreviousInstancesAndRecordOurself(
    const std::filesystem::path & cgroupRecordsDir
)
{
    assert(
        !stateRecord && "`stateRecord` cannot be created before the cleansing process takes place"
    );
    createDirs(cgroupRecordsDir);

    auto cgroupFile = cgroupRecordsDir / name_;

    if (pathExists(cgroupFile)) {
        auto prevCgroup = readFile(cgroupFile);
        printTaggedWarning("destroying past cgroup '%s' found in the state directory", name_);
        destroyCgroup(fmt("past %s", name_), prevCgroup);
    }

    writeFile(cgroupFile, std::get<std::filesystem::path>(cgroup_).string());
    stateRecord.emplace(cgroupFile, false);
}

void AutoDestroyCgroup::adoptProcess(int pid)
{
    auto path = std::get_if<std::filesystem::path>(&cgroup_);
    if (!path) {
        throw SysError("cgroup '%s' went away while adopting process '%d'", name_, pid);
    }

    writeFile(*path / "cgroup.procs", fmt("%d", pid));
}

void AutoDestroyCgroup::kill()
{
    auto path = std::get_if<std::filesystem::path>(&cgroup_);
    if (!path) {
        /* If the cgroup already disappeared,
         * processes already got killed.
         */
        return;
    }

    killCgroup(name_, *path);
}

CgroupStats AutoDestroyCgroup::getStatistics() const
{
    /* Either:
     * - we need to pull statistics from an alive cgroup.
     * - we need to get historical statistics from a dead cgroup.
     */
    return std::visit(
        nix::overloaded{
            [&](const Path & aliveCgroup) { return readStatistics(aliveCgroup).value(); },
            [&](const CgroupStats & stats) { return stats; }
        },
        cgroup_
    );
}

std::optional<std::filesystem::path> getCgroupFS()
{
    static auto res = [&]() -> std::optional<std::filesystem::path> {
        auto fp = fopen("/proc/mounts", "r");
        if (!fp) {
            return {};
        }
        Finally delFP = [&]() { fclose(fp); };
        while (auto ent = getmntent(fp)) {
            if (std::string_view(ent->mnt_type) == "cgroup2") {
                return {ent->mnt_dir};
            }
        }

        return {};
    }();
    return res;
}
}

#endif
