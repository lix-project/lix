#include "launch-builder.hh"
#include "lix/libstore/build/request.capnp.h"
#include "lix/libutil/rpc.hh"
#include <cassert>
#include <csignal>
#include <filesystem>
#include <format>
#include <kj/io.h>
#include <net/if.h>
#include <netinet/in.h>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/personality.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#if HAVE_SECCOMP
#include <linux/filter.h>
#include <sys/syscall.h>
#include <seccomp.h>
#endif

namespace fs = std::filesystem;

namespace nix {

// TODO dedup with libutil
static void setPersonality(std::string_view system)
{
    /* Change the personality to 32-bit if we're doing an
       i686-linux build on an x86_64-linux machine. */
    struct utsname utsbuf;
    uname(&utsbuf);
    if ((system == "i686-linux"
         && (std::string_view(SYSTEM) == "x86_64-linux"
             || (!strcmp(utsbuf.sysname, "Linux") && !strcmp(utsbuf.machine, "x86_64"))))
        || system == "armv7l-linux" || system == "armv6l-linux" || system == "armv5tel-linux")
    {
        if (personality(PER_LINUX32) == -1) {
            throw SysError("cannot set 32-bit personality");
        }
    }

    /* Disable address space randomization for improved
       determinism. */
    int cur = personality(0xffffffff);
    if (cur != -1) {
        personality(cur | ADDR_NO_RANDOMIZE);
    }
}

bool pathExists(const fs::path & path)
{
    return fs::exists(fs::symlink_status(path));
}

void bindPath(const fs::path & source, const fs::path & target, bool optional = false)
{
    debug("bind mounting %1% to %2%", source, target);

    auto bindMount = [&]() {
        if (mount(source.c_str(), target.c_str(), "", MS_BIND | MS_REC, 0) == -1) {
            throw SysError("bind mount from %1% to %2% failed", source, target);
        }
    };

    auto st = fs::symlink_status(source);
    if (st.type() == fs::file_type::not_found) {
        if (optional) {
            return;
        } else {
            throw SysError("getting attributes of path %1%", source);
        }
    }

    if (st.type() == fs::file_type::directory) {
        fs::create_directories(target);
        bindMount();
    } else if (st.type() == fs::file_type::symlink) {
        // Symlinks can (apparently) not be bind-mounted, so just copy it
        fs::create_directories(target.parent_path());
        fs::copy_symlink(source, target);
    } else {
        fs::create_directories(target.parent_path());
        if (kj::AutoCloseFd file{open(target.c_str(), O_RDWR | O_CREAT, 0644)}; file == nullptr) {
            throw SysError("could not create %s", target);
        }
        bindMount();
    }
}

bool prepareChildSetup(build::Request::Reader request)
{
    auto config = request.getPlatform().getLinux();

    // Set the NO_NEW_PRIVS prctl flag.
    // This both makes loading seccomp filters work for unprivileged users,
    // and is an additional security measure in its own right.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L) == -1) {
        throw SysError("PR_SET_NO_NEW_PRIVS failed");
    }
#if HAVE_SECCOMP
    if (config.hasSeccompFilters()) {
        const auto seccompBPF = config.getSeccompFilters();
        const auto entries = seccompBPF.size() / sizeof(struct sock_filter);
        assert(entries <= std::numeric_limits<unsigned short>::max());
        struct sock_fprog fprog = {
            .len = static_cast<unsigned short>(entries),
            // the kernel does not actually write to the filter, and doesn't care about alignment
            .filter = const_cast<struct sock_filter *>(
                reinterpret_cast<const struct sock_filter *>(seccompBPF.begin())
            ),
        };
        if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &fprog) != 0) {
            throw SysError("unable to load seccomp BPF program");
        }
    }
#endif

    KJ_DEFER(setPersonality(rpc::to<std::string_view>(config.getPlatform())));

    if (!config.hasSandbox()) {
        return true;
    }

    auto sandbox = config.getSandbox();

    // NOLINTBEGIN(lix-unsafe-c-calls): we trust the parent that all sandbox config is correct.
    // no strings in the linux sandbox config can be set by normal users or derivation authors,
    // except (in single-user instances) storeDir and chrootRootDir, which must be valid paths.
    //
    // NOLINTBEGIN(lix-foreign-exceptions): they're all properly caught by the builder main fn.

    const fs::path chrootRootDir{rpc::to<std::string_view>(sandbox.getChrootRootDir())};

    if (sandbox.getPrivateNetwork()) {
        /* Initialise the loopback interface. */
        kj::AutoCloseFd fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
        if (fd == nullptr) {
            throw SysError("cannot open IP socket");
        }

        struct ifreq ifr;
        strcpy(ifr.ifr_name, "lo");
        ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
        if (ioctl(fd.get(), SIOCSIFFLAGS, &ifr) == -1) {
            throw SysError("cannot set loopback interface flags");
        }
    }

    /* Set the hostname etc. to fixed values. */
    char hostname[] = "localhost";
    if (sethostname(hostname, sizeof(hostname)) == -1) {
        throw SysError("cannot set host name");
    }
    char domainname[] = "(none)"; // kernel default
    if (setdomainname(domainname, sizeof(domainname)) == -1) {
        throw SysError("cannot set domain name");
    }

    /* Make all filesystems private.  This is necessary
       because subtrees may have been mounted as "shared"
       (MS_SHARED).  (Systemd does this, for instance.)  Even
       though we have a private mount namespace, mounting
       filesystems on top of a shared subtree still propagates
       outside of the namespace.  Making a subtree private is
       local to the namespace, though, so setting MS_PRIVATE
       does not affect the outside world. */
    const fs::path storeDir{rpc::to<std::string>(sandbox.getStoreDir())};
    const auto chrootStoreDir = chrootRootDir / storeDir.relative_path();

    if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1) {
        throw SysError("unable to make '/' private");
    }

    /* Bind-mount chroot directory to itself, to treat it as a
       different filesystem from /, as needed for pivot_root. */
    if (mount(chrootRootDir.c_str(), chrootRootDir.c_str(), "", MS_BIND, 0) == -1) {
        throw SysError("unable to bind mount %1%", chrootRootDir);
    }

    /* Bind-mount the sandbox's Nix store onto itself so that
       we can mark it as a "shared" subtree, allowing bind
       mounts made in *this* mount namespace to be propagated
       into the child namespace created by the
       unshare(CLONE_NEWNS) call below.

       Marking chrootRootDir as MS_SHARED causes pivot_root()
       to fail with EINVAL. Don't know why. */
    if (mount(chrootStoreDir.c_str(), chrootStoreDir.c_str(), "", MS_BIND, 0) == -1) {
        throw SysError("unable to bind mount the Nix store");
    }

    if (mount("", chrootStoreDir.c_str(), "", MS_SHARED, 0) == -1) {
        throw SysError("unable to make %s shared", chrootStoreDir);
    }

    bool devMounted = false;
    bool devPtsMounted = false;

    /* Bind-mount all the directories from the "host"
       filesystem that we want in the chroot
       environment. */
    for (auto path : sandbox.getPaths()) {
        const fs::path source{rpc::to<std::string_view>(path.getSource())};
        const fs::path target{rpc::to<std::string_view>(path.getTarget())};
        devMounted |= target == "/dev";
        devPtsMounted |= target == "/dev/pts";
        if (source == "/proc") {
            continue; // backwards compatibility
        }

#if HAVE_EMBEDDED_SANDBOX_SHELL
        if (source == "__embedded_sandbox_shell__") {
            static unsigned char sh[] = {
#include "embedded-sandbox-shell.gen.hh"
                    };
            const fs::path dst = chrootRootDir / target.relative_path();
            fs::create_directories(dst.parent_path());
            writeFile(dst, std::string_view((const char *) sh, sizeof(sh)));
            fs::permissions(dst, fs::perms(0555));
        } else
#endif
            bindPath(source, chrootRootDir / target.relative_path(), path.getOptional());
    }

    /* Set up a nearly empty /dev, unless the user asked to
       bind-mount the host /dev. */
    if (!devMounted) {
        const auto bind = [&](fs::path item) { bindPath(item, chrootRootDir / item.relative_path()); };

        fs::create_directories(chrootRootDir / "dev/shm");
        fs::create_directories(chrootRootDir / "dev/pts");
        bind("/dev/full");
        if (sandbox.getWantsKvm() && pathExists("/dev/kvm")) {
            bind("/dev/kvm");
        }
        bind("/dev/null");
        bind("/dev/random");
        bind("/dev/tty");
        bind("/dev/urandom");
        bind("/dev/zero");
        fs::create_symlink("/proc/self/fd", chrootRootDir / "dev/fd");
        fs::create_symlink("/proc/self/fd/0", chrootRootDir / "dev/stdin");
        fs::create_symlink("/proc/self/fd/1", chrootRootDir / "dev/stdout");
        fs::create_symlink("/proc/self/fd/2", chrootRootDir / "dev/stderr");
    }

    /* Bind a new instance of procfs on /proc. */
    fs::create_directories(chrootRootDir / "proc");
    if (mount("none", (chrootRootDir / "proc").c_str(), "proc", 0, 0) == -1) {
        throw SysError("mounting /proc");
    }

    /* Mount sysfs on /sys. */
    if (request.hasCredentials() && request.getCredentials().getUidCount() != 1) {
        fs::create_directories(chrootRootDir / "sys");
        if (mount("none", (chrootRootDir / "sys").c_str(), "sysfs", 0, 0) == -1) {
            throw SysError("mounting /sys");
        }
    }

    /* Mount a new tmpfs on /dev/shm to ensure that whatever
       the builder puts in /dev/shm is cleaned up automatically. */
    if (pathExists("/dev/shm")
        && mount("none", (chrootRootDir / "dev/shm").c_str(), "tmpfs", 0, sandbox.getSandboxShmFlags().cStr())
            == -1)
    {
        throw SysError("mounting /dev/shm");
    }

    /* Mount a new devpts on /dev/pts.  Note that this
       requires the kernel to be compiled with
       CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
       if /dev/ptx/ptmx exists). */
    if (pathExists("/dev/pts/ptmx") && !pathExists(chrootRootDir / "dev/ptmx") && !devPtsMounted) {
        if (mount("none", (chrootRootDir / "dev/pts").c_str(), "devpts", 0, "newinstance,mode=0620") == 0) {
            fs::create_symlink("/dev/pts/ptmx", chrootRootDir / "dev/ptmx");

            /* Make sure /dev/pts/ptmx is world-writable.  With some
               Linux versions, it is created with permissions 0.  */
            fs::permissions(chrootRootDir / "dev/pts/ptmx", fs::perms(0666));
        } else {
            if (errno != EINVAL) {
                throw SysError("mounting /dev/pts");
            }
            bindPath("/dev/pts", chrootRootDir / "dev/pts");
            bindPath("/dev/ptmx", chrootRootDir / "dev/ptmx");
        }
    }

    /* Make /etc unwritable */
    if (!sandbox.getUseUidRange()) {
        fs::permissions(chrootRootDir / "etc", fs::perms(0555));
    }

    /* The comment below is now outdated. Recursive Nix has been removed.
     * So there's no need to make path appear in the sandbox.
     * TODO(Raito): cleanup before a merge.
     */
    /* Unshare this mount namespace. This is necessary because
       pivot_root() below changes the root of the mount
       namespace. This means that the call to setns() in
       addDependency() would hide the host's filesystem,
       making it impossible to bind-mount paths from the host
       Nix store into the sandbox. Therefore, we save the
       pre-pivot_root namespace in
       sandboxMountNamespace. Since we made /nix/store a
       shared subtree above, this allows addDependency() to
       make paths appear in the sandbox. */
    if (unshare(CLONE_NEWNS) == -1) {
        throw SysError("unsharing mount namespace");
    }

    /* Creating a new cgroup namespace is independent of whether we enabled the cgroup experimental feature.
     * We always create a new cgroup namespace from a sandboxing perspective. */
    /* Unshare the cgroup namespace. This means
       /proc/self/cgroup will show the child's cgroup as '/'
       rather than whatever it is in the parent. */
    if (unshare(CLONE_NEWCGROUP) == -1) {
        throw SysError("unsharing cgroup namespace");
    }

    /* Do the chroot(). */
    if (chdir(chrootRootDir.c_str()) == -1) {
        throw SysError("cannot change directory to %1%", chrootRootDir);
    }

    if (mkdir("real-root", 0) == -1) {
        throw SysError("cannot create real-root directory");
    }

    if (syscall(SYS_pivot_root, ".", "real-root") == -1) {
        throw SysError("cannot pivot old root directory onto %1%", chrootRootDir / "real-root");
    }

    if (chroot(".") == -1) {
        throw SysError("cannot change root directory to %1%", chrootRootDir);
    }

    if (umount2("real-root", MNT_DETACH) == -1) {
        throw SysError("cannot unmount real root filesystem");
    }

    if (rmdir("real-root") == -1) {
        throw SysError("cannot remove real-root directory");
    }

    /* Switch to the sandbox uid/gid in the user namespace,
       which corresponds to the build user or calling user in
       the parent namespace. */
    if (setgid(sandbox.getGid()) == -1) {
        throw SysError("setgid failed");
    }
    if (setuid(sandbox.getUid()) == -1) {
        throw SysError("setuid failed");
    }

    if (sandbox.hasWaitForInterface()) {
        // wait for the pasta interface to appear. pasta can't signal us when
        // it's done setting up the namespace, so we have to wait for a while
        kj::AutoCloseFd fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
        if (fd == nullptr) {
            throw SysError("cannot open IP socket");
        }

        struct ifreq ifr;
        strncpy(ifr.ifr_name, sandbox.getWaitForInterface().cStr(), sizeof(ifr.ifr_name));
        // wait two minutes for the interface to appear. if it does not do so
        // we are either grossly overloaded, or pasta startup failed somehow.
        static constexpr int SINGLE_WAIT_US = 1000;
        static constexpr int TOTAL_WAIT_US = 120'000'000;
        for (unsigned tries = 0;; tries++) {
            if (tries > TOTAL_WAIT_US / SINGLE_WAIT_US) {
                throw std::runtime_error(
                    "sandbox network setup timed out, please check daemon logs for possible error output."
                );
            } else if (ioctl(fd.get(), SIOCGIFFLAGS, &ifr) == 0) {
                if ((ifr.ifr_ifru.ifru_flags & IFF_UP) != 0) {
                    break;
                }
            } else if (errno == ENODEV) {
                usleep(SINGLE_WAIT_US);
            } else {
                throw SysError("cannot get loopback interface flags");
            }
        }
    }

    // NOLINTEND(lix-foreign-exceptions)
    // NOLINTEND(lix-unsafe-c-calls)

    return false;
}

void finishChildSetup(build::Request::Reader request)
{
    // clear all capabilities when not running as root in the sandbox.
    // we always clear ambient capabilities because they survive exec.
    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0L, 0L, 0L) == -1) {
        throw SysError("clearing ambient caps");
    }
    if (!request.getPlatform().getLinux().getSandbox().getUseUidRange()) {
        static constexpr uint32_t LINUX_CAPABILITY_VERSION_3 = 0x20080522;
        static constexpr uint32_t LINUX_CAPABILITY_U32S_3 = 2;
        struct user_cap_header_struct
        {
            uint32_t version;
            int pid;
        } hdr = {LINUX_CAPABILITY_VERSION_3, 0};
        struct user_cap_data_struct
        {
            uint32_t effective;
            uint32_t permitted;
            uint32_t inheritable;
        } data[LINUX_CAPABILITY_U32S_3] = {};
        if (syscall(SYS_capset, &hdr, data)) {
            throw SysError("couldn't set capabilities");
        }
    }

    if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1) {
        throw SysError("setting death signal");
    }
    if (getppid() != request.getPlatform().getLinux().getParentPid()) {
        raise(SIGKILL);
    }
}

[[noreturn]]
void execBuilder(build::Request::Reader request)
{
    ExecRequest req{request};

    execve(req.builder.data(), req.args.data(), req.envs.data());
    throw SysError("running %s", req.builder);
}

}
