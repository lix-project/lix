#include "util.hh"
#include "processes.hh"
#include "current-process.hh"

#include "sync.hh"
#include "finally.hh"
#include "serialise.hh"
#include "cgroup.hh"
#include "signals.hh"
#include "environment-variables.hh"
#include "file-system.hh"

#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/syscall.h>
#include <mach-o/dyld.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include <cmath>
#endif


#ifdef NDEBUG
#error "Lix may not be built with assertions disabled (i.e. with -DNDEBUG)."
#endif

namespace nix {


std::string getUserName()
{
    auto pw = getpwuid(geteuid());
    std::string name = pw ? pw->pw_name : getEnv("USER").value_or("");
    if (name.empty())
        throw Error("cannot figure out user name");
    return name;
}

Path getHomeOf(uid_t userId)
{
    std::vector<char> buf(16384);
    struct passwd pwbuf;
    struct passwd * pw;
    if (getpwuid_r(userId, &pwbuf, buf.data(), buf.size(), &pw) != 0
        || !pw || !pw->pw_dir || !pw->pw_dir[0])
        throw Error("cannot determine user's home directory");
    return pw->pw_dir;
}

Path getHome()
{
    static Path homeDir = []()
    {
        std::optional<std::string> unownedUserHomeDir = {};
        auto homeDir = getEnv("HOME");
        if (homeDir) {
            // Only use $HOME if doesn't exist or is owned by the current user.
            struct stat st;
            int result = stat(homeDir->c_str(), &st);
            if (result != 0) {
                if (errno != ENOENT) {
                    warn("couldn't stat $HOME ('%s') for reason other than not existing ('%d'), falling back to the one defined in the 'passwd' file", *homeDir, errno);
                    homeDir.reset();
                }
            } else if (st.st_uid != geteuid()) {
                unownedUserHomeDir.swap(homeDir);
            }
        }
        if (!homeDir) {
            homeDir = getHomeOf(geteuid());
            if (unownedUserHomeDir.has_value() && unownedUserHomeDir != homeDir) {
                warn("$HOME ('%s') is not owned by you, falling back to the one defined in the 'passwd' file ('%s')", *unownedUserHomeDir, *homeDir);
            }
        }
        return *homeDir;
    }();
    return homeDir;
}


Path getCacheDir()
{
    auto cacheDir = getEnv("XDG_CACHE_HOME");
    return cacheDir ? *cacheDir : getHome() + "/.cache";
}


Path getConfigDir()
{
    auto configDir = getEnv("XDG_CONFIG_HOME");
    return configDir ? *configDir : getHome() + "/.config";
}

std::vector<Path> getConfigDirs()
{
    Path configHome = getConfigDir();
    auto configDirs = getEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
    std::vector<Path> result = tokenizeString<std::vector<std::string>>(configDirs, ":");
    result.insert(result.begin(), configHome);
    return result;
}


Path getDataDir()
{
    auto dataDir = getEnv("XDG_DATA_HOME");
    return dataDir ? *dataDir : getHome() + "/.local/share";
}

Path getStateDir()
{
    auto stateDir = getEnv("XDG_STATE_HOME");
    return stateDir ? *stateDir : getHome() + "/.local/state";
}

Path createNixStateDir()
{
    Path dir = getStateDir() + "/nix";
    createDirs(dir);
    return dir;
}





//////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////




//////////////////////////////////////////////////////////////////////



std::vector<char *> stringsToCharPtrs(const Strings & ss)
{
    std::vector<char *> res;
    for (auto & s : ss) res.push_back((char *) s.c_str());
    res.push_back(0);
    return res;
}


//////////////////////////////////////////////////////////////////////


template<class C> C tokenizeString(std::string_view s, std::string_view separators)
{
    C result;
    auto pos = s.find_first_not_of(separators, 0);
    while (pos != std::string_view::npos) {
        auto end = s.find_first_of(separators, pos + 1);
        if (end == std::string_view::npos) end = s.size();
        result.insert(result.end(), std::string(s, pos, end - pos));
        pos = s.find_first_not_of(separators, end);
    }
    return result;
}

template Strings tokenizeString(std::string_view s, std::string_view separators);
template StringSet tokenizeString(std::string_view s, std::string_view separators);
template std::vector<std::string> tokenizeString(std::string_view s, std::string_view separators);


std::string chomp(std::string_view s)
{
    size_t i = s.find_last_not_of(" \n\r\t");
    return i == std::string_view::npos ? "" : std::string(s, 0, i + 1);
}


std::string trim(std::string_view s, std::string_view whitespace)
{
    auto i = s.find_first_not_of(whitespace);
    if (i == s.npos) return "";
    auto j = s.find_last_not_of(whitespace);
    return std::string(s, i, j == s.npos ? j : j - i + 1);
}


std::string replaceStrings(
    std::string res,
    std::string_view from,
    std::string_view to)
{
    if (from.empty()) return res;
    size_t pos = 0;
    while ((pos = res.find(from, pos)) != std::string::npos) {
        res.replace(pos, from.size(), to);
        pos += to.size();
    }
    return res;
}


Rewriter::Rewriter(std::map<std::string, std::string> rewrites)
    : rewrites(std::move(rewrites))
{
    for (const auto & [k, v] : this->rewrites) {
        assert(!k.empty());
        initials.push_back(k[0]);
    }
    std::ranges::sort(initials);
    auto [firstDupe, end] = std::ranges::unique(initials);
    initials.erase(firstDupe, end);
}

std::string Rewriter::operator()(std::string s)
{
    size_t j = 0;
    while ((j = s.find_first_of(initials, j)) != std::string::npos) {
        size_t skip = 1;
        for (auto & [from, to] : rewrites) {
            if (s.compare(j, from.size(), from) == 0) {
                s.replace(j, from.size(), to);
                skip = to.size();
                break;
            }
        }
        j += skip;
    }
    return s;
}


std::string toLower(const std::string & s)
{
    std::string r(s);
    for (auto & c : r)
        c = std::tolower(c);
    return r;
}


std::string shellEscape(const std::string_view s)
{
    std::string r;
    r.reserve(s.size() + 2);
    r += "'";
    for (auto & i : s)
        if (i == '\'') r += "'\\''"; else r += i;
    r += '\'';
    return r;
}


void ignoreException(Verbosity lvl)
{
    /* Make sure no exceptions leave this function.
       printError() also throws when remote is closed. */
    try {
        try {
            throw;
        } catch (std::exception & e) {
            printMsg(lvl, "error (ignored): %1%", e.what());
        }
    } catch (...) { }
}

constexpr char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(std::string_view s)
{
    std::string res;
    res.reserve((s.size() + 2) / 3 * 4);
    int data = 0, nbits = 0;

    for (char c : s) {
        data = data << 8 | (unsigned char) c;
        nbits += 8;
        while (nbits >= 6) {
            nbits -= 6;
            res.push_back(base64Chars[data >> nbits & 0x3f]);
        }
    }

    if (nbits) res.push_back(base64Chars[data << (6 - nbits) & 0x3f]);
    while (res.size() % 4) res.push_back('=');

    return res;
}


std::string base64Decode(std::string_view s)
{
    constexpr char npos = -1;
    constexpr std::array<char, 256> base64DecodeChars = [&]() {
        std::array<char, 256>  result{};
        for (auto& c : result)
            c = npos;
        for (int i = 0; i < 64; i++)
            result[base64Chars[i]] = i;
        return result;
    }();

    std::string res;
    // Some sequences are missing the padding consisting of up to two '='.
    //                    vvv
    res.reserve((s.size() + 2) / 4 * 3);
    unsigned int d = 0, bits = 0;

    for (char c : s) {
        if (c == '=') break;
        if (c == '\n') continue;

        char digit = base64DecodeChars[(unsigned char) c];
        if (digit == npos)
            throw Error("invalid character in Base64 string: '%c'", c);

        bits += 6;
        d = d << 6 | digit;
        if (bits >= 8) {
            res.push_back(d >> (bits - 8) & 0xff);
            bits -= 8;
        }
    }

    return res;
}


std::string stripIndentation(std::string_view s)
{
    size_t minIndent = 10000;
    size_t curIndent = 0;
    bool atStartOfLine = true;

    for (auto & c : s) {
        if (atStartOfLine && c == ' ')
            curIndent++;
        else if (c == '\n') {
            if (atStartOfLine)
                minIndent = std::max(minIndent, curIndent);
            curIndent = 0;
            atStartOfLine = true;
        } else {
            if (atStartOfLine) {
                minIndent = std::min(minIndent, curIndent);
                atStartOfLine = false;
            }
        }
    }

    std::string res;

    size_t pos = 0;
    while (pos < s.size()) {
        auto eol = s.find('\n', pos);
        if (eol == s.npos) eol = s.size();
        if (eol - pos > minIndent)
            res.append(s.substr(pos + minIndent, eol - pos - minIndent));
        res.push_back('\n');
        pos = eol + 1;
    }

    return res;
}


std::pair<std::string_view, std::string_view> getLine(std::string_view s)
{
    auto newline = s.find('\n');

    if (newline == s.npos) {
        return {s, ""};
    } else {
        auto line = s.substr(0, newline);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line = line.substr(0, line.size() - 1);
        return {line, s.substr(newline + 1)};
    }
}


//////////////////////////////////////////////////////////////////////



#if __linux__
static AutoCloseFD fdSavedMountNamespace;
static AutoCloseFD fdSavedRoot;
#endif

void saveMountNamespace()
{
#if __linux__
    static std::once_flag done;
    std::call_once(done, []() {
        fdSavedMountNamespace = AutoCloseFD{open("/proc/self/ns/mnt", O_RDONLY)};
        if (!fdSavedMountNamespace)
            throw SysError("saving parent mount namespace");

        fdSavedRoot = AutoCloseFD{open("/proc/self/root", O_RDONLY)};
    });
#endif
}

void restoreMountNamespace()
{
#if __linux__
    try {
        auto savedCwd = absPath(".");

        if (fdSavedMountNamespace && setns(fdSavedMountNamespace.get(), CLONE_NEWNS) == -1)
            throw SysError("restoring parent mount namespace");

        if (fdSavedRoot) {
            if (fchdir(fdSavedRoot.get()))
                throw SysError("chdir into saved root");
            if (chroot("."))
                throw SysError("chroot into saved root");
        }

        if (chdir(savedCwd.c_str()) == -1)
            throw SysError("restoring cwd");
    } catch (Error & e) {
        debug(e.msg());
    }
#endif
}

void unshareFilesystem()
{
#ifdef __linux__
    if (unshare(CLONE_FS) != 0 && errno != EPERM)
        throw SysError("unsharing filesystem state in download thread");
#endif
}

AutoCloseFD createUnixDomainSocket()
{
    AutoCloseFD fdSocket{socket(PF_UNIX, SOCK_STREAM
        #ifdef SOCK_CLOEXEC
        | SOCK_CLOEXEC
        #endif
        , 0)};
    if (!fdSocket)
        throw SysError("cannot create Unix domain socket");
    closeOnExec(fdSocket.get());
    return fdSocket;
}


AutoCloseFD createUnixDomainSocket(const Path & path, mode_t mode)
{
    auto fdSocket = nix::createUnixDomainSocket();

    bind(fdSocket.get(), path);

    chmodPath(path.c_str(), mode);

    if (listen(fdSocket.get(), 100) == -1)
        throw SysError("cannot listen on socket '%1%'", path);

    return fdSocket;
}


static void bindConnectProcHelper(
    std::string_view operationName, auto && operation,
    int fd, const std::string & path)
{
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    // Casting between types like these legacy C library interfaces
    // require is forbidden in C++. To maintain backwards
    // compatibility, the implementation of the bind/connect functions
    // contains some hints to the compiler that allow for this
    // special case.
    auto * psaddr = reinterpret_cast<struct sockaddr *>(&addr);

    if (path.size() + 1 >= sizeof(addr.sun_path)) {
        Pipe pipe;
        pipe.create();
        Pid pid = startProcess([&] {
            try {
                pipe.readSide.close();
                Path dir = dirOf(path);
                if (chdir(dir.c_str()) == -1)
                    throw SysError("chdir to '%s' failed", dir);
                std::string base(baseNameOf(path));
                if (base.size() + 1 >= sizeof(addr.sun_path))
                    throw Error("socket path '%s' is too long", base);
                memcpy(addr.sun_path, base.c_str(), base.size() + 1);
                if (operation(fd, psaddr, sizeof(addr)) == -1)
                    throw SysError("cannot %s to socket at '%s'", operationName, path);
                writeFull(pipe.writeSide.get(), "0\n");
            } catch (SysError & e) {
                writeFull(pipe.writeSide.get(), fmt("%d\n", e.errNo));
            } catch (...) {
                writeFull(pipe.writeSide.get(), "-1\n");
            }
        });
        pipe.writeSide.close();
        auto errNo = string2Int<int>(chomp(drainFD(pipe.readSide.get())));
        if (!errNo || *errNo == -1)
            throw Error("cannot %s to socket at '%s'", operationName, path);
        else if (*errNo > 0) {
            errno = *errNo;
            throw SysError("cannot %s to socket at '%s'", operationName, path);
        }
    } else {
        memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (operation(fd, psaddr, sizeof(addr)) == -1)
            throw SysError("cannot %s to socket at '%s'", operationName, path);
    }
}


void bind(int fd, const std::string & path)
{
    unlink(path.c_str());

    bindConnectProcHelper("bind", ::bind, fd, path);
}


void connect(int fd, const std::string & path)
{
    bindConnectProcHelper("connect", ::connect, fd, path);
}


std::string showBytes(uint64_t bytes)
{
    return fmt("%.2f MiB", bytes / (1024.0 * 1024.0));
}

}
