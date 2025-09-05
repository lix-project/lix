#include "lix/libutil/environment-variables.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/users.hh"
#include "lix/libutil/args.hh"
#include "lix/libutil/abstract-setting-to-json.hh"
#include "lix/libutil/compute-levels.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libutil/json.hh"

#include <algorithm>
#include <mutex>
#include <thread>
#include <dlfcn.h>
#include <sys/utsname.h>

#ifdef __GLIBC__
#include <gnu/lib-names.h>
#include <nss.h>
#include <dlfcn.h>
#endif

#include "lix/libutil/config-impl.hh"

#ifdef __APPLE__
#include "lix/libutil/processes.hh"
#include <curl/curl.h>
#include <sys/sysctl.h>
#endif

// All built-in store implementations.
#include "lix/libstore/dummy-store.hh"
#include "lix/libstore/http-binary-cache-store.hh"
#include "lix/libstore/legacy-ssh-store.hh"
#include "lix/libstore/local-binary-cache-store.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libstore/s3-binary-cache-store.hh"
#include "lix/libstore/ssh-store.hh"
#include "lix/libstore/uds-remote-store.hh"

namespace nix {


/* The default location of the daemon socket, relative to nixStateDir.
   The socket is in a directory to allow you to control access to the
   Nix daemon by setting the mode/ownership of the directory
   appropriately.  (This wouldn't work on the socket itself since it
   must be deleted and recreated on startup.) */
#define DEFAULT_SOCKET_PATH "/daemon-socket/socket"

Settings settings;

static GlobalConfig::Register rSettings(&settings);

Settings::Settings()
    : nixPrefix(NIX_PREFIX)
    , nixStore(canonPath(getEnvNonEmpty("NIX_STORE_DIR").value_or(getEnvNonEmpty("NIX_STORE").value_or(NIX_STORE_DIR))))
    , nixDataDir(canonPath(getEnvNonEmpty("NIX_DATA_DIR").value_or(NIX_DATA_DIR)))
    , nixLogDir(canonPath(getEnvNonEmpty("NIX_LOG_DIR").value_or(NIX_LOG_DIR)))
    , nixStateDir(canonPath(getEnvNonEmpty("NIX_STATE_DIR").value_or(NIX_STATE_DIR)))
    , nixConfDir(canonPath(getEnvNonEmpty("NIX_CONF_DIR").value_or(NIX_CONF_DIR)))
    , nixUserConfFiles(getUserConfigFiles())
    , nixBinDir(canonPath(getEnvNonEmpty("NIX_BIN_DIR").value_or(NIX_BIN_DIR)))
    , nixManDir(canonPath(NIX_MAN_DIR))
    , nixDaemonSocketFile(canonPath(getEnvNonEmpty("NIX_DAEMON_SOCKET_PATH").value_or(nixStateDir + DEFAULT_SOCKET_PATH)))
{
    buildUsersGroup.setDefault(getuid() == 0 ? "nixbld" : "");
    allowSymlinkedStore.setDefault(getEnv("NIX_IGNORE_SYMLINK_STORE") == "1");

    auto sslOverride = getEnv("NIX_SSL_CERT_FILE").value_or(getEnv("SSL_CERT_FILE").value_or(""));
    if (sslOverride != "")
        caFile.setDefault(sslOverride);

    /* Backwards compatibility. */
    auto s = getEnv("NIX_REMOTE_SYSTEMS");
    if (s) {
        Strings ss;
        for (auto & p : tokenizeString<Strings>(*s, ":"))
            ss.push_back("@" + p);
        builders.setDefault(concatStringsSep(" ", ss));
    }

#if defined(__linux__) && defined(SANDBOX_SHELL)
    sandboxPaths.setDefault(tokenizeString<StringSet>("/bin/sh=" SANDBOX_SHELL));
#endif
#if defined(__linux__) && defined(PASTA_PATH)
    pastaPath.setDefault(PASTA_PATH);
#endif

    /* chroot-like behavior from Apple's sandbox */
#if __APPLE__
    sandboxPaths.setDefault(tokenizeString<StringSet>("/System/Library/Frameworks /System/Library/PrivateFrameworks /bin/sh /bin/bash /private/tmp /private/var/tmp /usr/lib"));
    allowedImpureHostPrefixes.setDefault(tokenizeString<StringSet>("/System/Library /usr/lib /dev /bin/sh"));
#endif

    /* Set the build hook location

       For builds we perform a self-invocation, so Lix has to be self-aware.
       That is, it has to know where it is installed. We don't think it's sentient.

       Normally, nix is installed according to `nixBinDir`, which is set at compile time,
       but can be overridden. This makes for a great default that works even if this
       code is linked as a library into some other program whose main is not aware
       that it might need to be a build remote hook.

       However, it may not have been installed at all. For example, if it's a static build,
       there's a good chance that it has been moved out of its installation directory.
       That makes `nixBinDir` useless. Instead, we'll query the OS for the path to the
       current executable, using `getSelfExe()`.

       As a last resort, we resort to `PATH`. Hopefully we find a `nix` there that's compatible.
       If you're porting Lix to a new platform, that might be good enough for a while, but
       you'll want to improve `getSelfExe()` to work on your platform.
     */
    std::string nixExePath = nixBinDir + "/nix";
    if (!pathExists(nixExePath)) {
        nixExePath = getSelfExe().value_or("nix");
    }
    buildHook.setDefault(Strings {
        nixExePath,
        "__build-remote",
    });
}

void loadConfFile()
{
    auto applyConfigFile = [&](const ApplyConfigOptions & options) {
        try {
            std::string contents = readFile(*options.path);
            globalConfig.applyConfig(contents, options);
        } catch (SysError &) {
        }
    };

    applyConfigFile(ApplyConfigOptions{.path = settings.nixConfDir + "/nix.conf"});

    /* We only want to send overrides to the daemon, i.e. stuff from
       ~/.nix/nix.conf or the command line. */
    globalConfig.resetOverridden();

    auto files = settings.nixUserConfFiles;
    auto home = getHome();
    for (auto file = files.rbegin(); file != files.rend(); file++) {
        applyConfigFile(ApplyConfigOptions{.path = *file, .home = home});
    }

    auto nixConfEnv = getEnv("NIX_CONFIG");
    if (nixConfEnv.has_value()) {
        globalConfig.applyConfig(nixConfEnv.value(), ApplyConfigOptions{.fromEnvVar = true});
    }
}

std::vector<Path> getUserConfigFiles()
{
    // Use the paths specified in NIX_USER_CONF_FILES if it has been defined
    auto nixConfFiles = getEnv("NIX_USER_CONF_FILES");
    if (nixConfFiles.has_value()) {
        return tokenizeString<std::vector<std::string>>(nixConfFiles.value(), ":");
    }

    // Use the paths specified by the XDG spec
    std::vector<Path> files;
    auto dirs = getConfigDirs();
    for (auto & dir : dirs) {
        files.insert(files.end(), dir + "/nix/nix.conf");
    }
    return files;
}

unsigned int Settings::getDefaultCores()
{
    const unsigned int concurrency = std::max(1U, std::thread::hardware_concurrency());
    const unsigned int maxCPU = getMaxCPU();

    if (maxCPU > 0)
      return maxCPU;
    else
      return concurrency;
}

#if __APPLE__
static bool hasVirt() {

    int hasVMM;
    int hvSupport;
    size_t size;

    size = sizeof(hasVMM);
    if (sysctlbyname("kern.hv_vmm_present", &hasVMM, &size, nullptr, 0) == 0) {
        if (hasVMM)
            return false;
    }

    // whether the kernel and hardware supports virt
    size = sizeof(hvSupport);
    if (sysctlbyname("kern.hv_support", &hvSupport, &size, nullptr, 0) == 0) {
        return hvSupport == 1;
    } else {
        return false;
    }
}
#endif

StringSet Settings::getDefaultSystemFeatures()
{
    /* For backwards compatibility, accept some "features" that are
       used in Nixpkgs to route builds to certain machines but don't
       actually require anything special on the machines. */
    StringSet features{"nixos-test", "benchmark", "big-parallel"};

    #if __linux__
    features.insert("uid-range");
    #endif

    #if __linux__
    if (access("/dev/kvm", R_OK | W_OK) == 0)
        features.insert("kvm");
    #endif

    #if __APPLE__
    if (hasVirt())
        features.insert("apple-virt");
    #endif

    return features;
}

StringSet Settings::getDefaultExtraPlatforms()
{
    StringSet extraPlatforms;

    if (std::string{SYSTEM} == "x86_64-linux" && !isWSL1())
        extraPlatforms.insert("i686-linux");

#if __linux__
    StringSet levels = computeLevels();
    for (auto iter = levels.begin(); iter != levels.end(); ++iter)
        extraPlatforms.insert(*iter + "-linux");
#elif __APPLE__
    // Rosetta 2 emulation layer can run x86_64 binaries on aarch64
    // machines. Note that we can’t force processes from executing
    // x86_64 in aarch64 environments or vice versa since they can
    // always exec with their own binary preferences.
    if (std::string{SYSTEM} == "aarch64-darwin") {
        AutoCloseFD null(open("/dev/null", O_RDWR | O_CLOEXEC));
        if (!null) {
            throw Error("could not open /dev/null");
        }
        if (runProgram2(RunOptions{
                            .program = "arch",
                            .args = {"-arch", "x86_64", "/usr/bin/true"},
                            .redirections =
                                {{.dup = STDOUT_FILENO, .from = null.get()},
                                 {.dup = STDERR_FILENO, .from = null.get()}}
                        }
            ).wait()
            == 0)
        {
            extraPlatforms.insert("x86_64-darwin");
        }
    }
#endif

    return extraPlatforms;
}

bool Settings::isWSL1()
{
    struct utsname utsbuf;
    uname(&utsbuf);
    // WSL1 uses -Microsoft suffix
    // WSL2 uses -microsoft-standard suffix
    return std::string_view(utsbuf.release).ends_with("-Microsoft");
}

Path Settings::getDefaultSSLCertFile()
{
    for (auto & fn : {"/etc/ssl/certs/ca-certificates.crt", "/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"})
        if (pathAccessible(fn, true)) return fn;
    return "";
}

const std::string nixVersion = PACKAGE_VERSION;

void to_json(JSON & j, const SandboxMode & e)
{
    if (e == SandboxMode::smEnabled) {
        j = true;
    } else if (e == SandboxMode::smRelaxed) {
        j = "relaxed";
    } else if (e == SandboxMode::smDisabled) {
        j = false;
    } else {
        abort();
    }
}

void from_json(const JSON & j, SandboxMode & e)
{
    if (j == true) {
        e = SandboxMode::smEnabled;
    } else if (j == "relaxed") {
        e = SandboxMode::smRelaxed;
    } else if (j == false) {
        e = SandboxMode::smDisabled;
    } else {
        throw Error("Invalid sandbox mode '%s'", std::string(j));
    }
}

template<> SandboxMode BaseSetting<SandboxMode>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    if (str == "true") return smEnabled;
    else if (str == "relaxed") return smRelaxed;
    else if (str == "false") return smDisabled;
    else throw UsageError("option '%s' has invalid value '%s'", name, str);
}

template<> struct BaseSetting<SandboxMode>::trait
{
    static constexpr bool appendable = false;
};

template<> std::string BaseSetting<SandboxMode>::to_string() const
{
    if (value == smEnabled) return "true";
    else if (value == smRelaxed) return "relaxed";
    else if (value == smDisabled) return "false";
    else abort();
}

template<> void BaseSetting<SandboxMode>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .description = "Enable sandboxing.",
        .category = category,
        .handler = {[this]() { override(smEnabled); }}
    });
    args.addFlag({
        .longName = "no-" + name,
        .description = "Disable sandboxing.",
        .category = category,
        .handler = {[this]() { override(smDisabled); }}
    });
    args.addFlag({
        .longName = "relaxed-" + name,
        .description = "Enable sandboxing, but allow builds to disable it.",
        .category = category,
        .handler = {[this]() { override(smRelaxed); }}
    });
}

unsigned int MaxBuildJobsSetting::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    if (str == "auto") return std::max(1U, std::thread::hardware_concurrency());
    else {
        if (auto n = string2Int<decltype(value)>(str))
            return *n;
        else
            throw UsageError("configuration setting '%s' should be 'auto' or an integer", name);
        }
    }


Paths PluginFilesSetting::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    if (pluginsLoaded)
        throw UsageError("plugin-files set after plugins were loaded, you may need to move the flag before the subcommand");
    return BaseSetting<Paths>::parse(str, options);
}

// C++ syntax so weird that it breaks the tree-sitter highlighter!
// *Technically* the C linkage function pointer should be so annotated.
// Does it actually matter? Almost certainly not!
extern "C" using NixPluginEntry = void (*)();

void initPlugins()
{
    assert(!settings.pluginFiles.pluginsLoaded);
    for (const auto & pluginFile : settings.pluginFiles.get()) {
        Paths pluginFiles;
        try {
            auto ents = readDirectory(pluginFile);
            for (const auto & ent : ents) {
                pluginFiles.emplace_back(pluginFile + "/" + ent.name);
            }
        } catch (SysError & e) {
            if (e.errNo != ENOTDIR) {
                // I feel like it is more reasonable to skip plugins if they are
                // inaccessible, since it is *already* the case that plugins
                // are not guaranteed to load due to version mismatches etc
                // causing dlopen failures.
                printTaggedWarning(
                    "could not access plugin file '%s', skipping it: %s", pluginFile, e.msg()
                );
                continue;
            }
            pluginFiles.emplace_back(pluginFile);
        }
        for (const auto & file : pluginFiles) {
            /* handle is purposefully leaked as there may be state in the
               DSO needed by the action of the plugin. */
            void *handle =
                dlopen(file.c_str(), RTLD_LAZY | RTLD_LOCAL);
            if (!handle) {
                printTaggedWarning(
                    "could not dynamically open plugin file '%s', skipping it: %s", file, dlerror()
                );
                continue;
            }

            /* Older plugins use a statically initialized object to run their code.
               Newer plugins can also export nix_plugin_entry() */
            auto nix_plugin_entry = reinterpret_cast<NixPluginEntry>(dlsym(handle, "nix_plugin_entry"));
            if (nix_plugin_entry) {
                nix_plugin_entry();
            }
        }
    }

    /* Since plugins can add settings, try to re-apply previously
       unknown settings. */
    globalConfig.reapplyUnknownSettings();
    globalConfig.warnUnknownSettings();

    /* Tell the user if they try to set plugin-files after we've already loaded */
    settings.pluginFiles.pluginsLoaded = true;
}

static void preloadNSS()
{
    /* builtin:fetchurl can trigger a DNS lookup, which with glibc can trigger a dynamic library load of
       one of the glibc NSS libraries in a sandboxed child, which will fail unless the library's already
       been loaded in the parent. So we force a lookup of an invalid domain to force the NSS machinery to
       load its lookup libraries in the parent before any child gets a chance to. */
    static std::once_flag dns_resolve_flag;

    std::call_once(dns_resolve_flag, []() {
#ifdef __GLIBC__
        /* On linux, glibc will run every lookup through the nss layer.
         * That means every lookup goes, by default, through nscd, which acts as a local
         * cache.
         * Because we run builds in a sandbox, we also remove access to nscd otherwise
         * lookups would leak into the sandbox.
         *
         * But now we have a new problem, we need to make sure the nss_dns backend that
         * does the dns lookups when nscd is not available is loaded or available.
         *
         * We can't make it available without leaking nix's environment, so instead we'll
         * load the backend, and configure nss so it does not try to run dns lookups
         * through nscd.
         *
         * This is technically only used for builtins:fetch* functions so we only care
         * about dns.
         *
         * All other platforms are unaffected.
         */
        if (!dlopen(LIBNSS_DNS_SO, RTLD_NOW)) printTaggedWarning("unable to load nss_dns backend");
        // FIXME: get hosts entry from nsswitch.conf.
        __nss_configure_lookup("hosts", "files dns");
#endif
    });
}

static void registerStoreImplementations() {
  registerDummyStore();
  registerHttpBinaryCacheStore();
  registerLegacySSHStore();
  registerLocalBinaryCacheStore();
  registerLocalStore();
  registerS3BinaryCacheStore();
  registerSSHStore();
  registerUDSRemoteStore();
}

static bool initLibStoreDone = false;

void assertLibStoreInitialized() {
    if (!initLibStoreDone) {
        printError("The program must call nix::initNix() before calling any libstore library functions.");
        std::terminate();
    };
}

void initLibStore() {

    loadConfFile();

    preloadNSS();

#if __APPLE__
    /* Because of an objc quirk[1], calling curl_global_init for the first time
       after fork() will always result in a crash.
       Up until now the solution has been to set OBJC_DISABLE_INITIALIZE_FORK_SAFETY
       for every nix process to ignore that error.
       Instead of working around that error we address it at the core -
       by calling curl_global_init here, which should mean curl will already
       have been initialized by the time we try to do so in a forked process.

       [1] https://github.com/apple-oss-distributions/objc4/blob/01edf1705fbc3ff78a423cd21e03dfc21eb4d780/runtime/objc-initialize.mm#L614-L636
    */
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    registerStoreImplementations();

    initLibStoreDone = true;
}


}
