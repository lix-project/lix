#include "lix/libutil/args/root.hh"
#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libstore/globals.hh"
#include "lix/libcmd/legacy.hh"
#include "lix/libutil/namespaces.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/json.hh"
#include "lix/libmain/loggers.hh"
#include "lix/libcmd/markdown.hh"
#include "add-to-store.hh"
#include "build-remote.hh"
#include "build.hh"
#include "bundle.hh"
#include "cat.hh"
#include "config.hh"
#include "copy.hh"
#include "daemon.hh"
#include "derivation-add.hh"
#include "derivation-show.hh"
#include "derivation.hh"
#include "develop.hh"
#include "diff-closures.hh"
#include "doctor.hh"
#include "dump-path.hh"
#include "edit.hh"
#include "eval.hh"
#include "flake.hh"
#include "fmt.hh"
#include "hash.hh"
#include "log.hh"
#include "ls.hh"
#include "make-content-addressed.hh"
#include "nar.hh"
#include "nix-build.hh"
#include "nix-channel.hh"
#include "nix-collect-garbage.hh"
#include "nix-copy-closure.hh"
#include "nix-env.hh"
#include "nix-instantiate.hh"
#include "nix-store.hh"
#include "optimise-store.hh"
#include "path-from-hash-part.hh"
#include "path-info.hh"
#include "ping-store.hh"
#include "prefetch.hh"
#include "profile.hh"
#include "realisation.hh"
#include "registry.hh"
#include "repl.hh"
#include "run.hh"
#include "search.hh"
#include "sigs.hh"
#include "store-copy-log.hh"
#include "store-delete.hh"
#include "store-gc.hh"
#include "store-repair.hh"
#include "store.hh"
#include "upgrade-nix.hh"
#include "verify.hh"
#include "why-depends.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>

namespace nix {

void registerLegacyCommands()
{
    registerLegacyNixEnv();
    registerLegacyNixBuildAndNixShell();
    registerLegacyNixInstantiate();
    registerLegacyNixCopyClosure();
    registerLegacyNixCollectGarbage();
    registerLegacyNixChannel();
    registerLegacyNixStore();
    registerLegacyBuildRemote();
    registerLegacyNixDaemon();
    registerLegacyNixPrefetchUrl();
    registerLegacyNixHash();
}

void registerNixHelp();

void registerCommands()
{
    // keep-sorted start
    registerNixBuild();
    registerNixBundle();
    registerNixCat();
    registerNixConfig();
    registerNixCopy();
    registerNixDaemon();
    registerNixDerivation();
    registerNixDerivationAdd();
    registerNixDerivationShow();
    registerNixDevelop();
    registerNixDoctor();
    registerNixEdit();
    registerNixEval();
    registerNixFlake();
    registerNixFmt();
    registerNixHash();
    registerNixHelp();
    registerNixLog();
    registerNixLs();
    registerNixMakeContentAddressed();
    registerNixNar();
    registerNixPathInfo();
    registerNixProfile();
    registerNixRealisation();
    registerNixRegistry();
    registerNixRepl();
    registerNixRun();
    registerNixSearch();
    registerNixSigs();
    registerNixStore();
    registerNixStoreAdd();
    registerNixStoreCopyLog();
    registerNixStoreDelete();
    registerNixStoreDiffClosures();
    registerNixStoreDumpPath();
    registerNixStoreGc();
    registerNixStoreOptimise();
    registerNixStorePathFromHashPart();
    registerNixStorePing();
    registerNixStorePrefetchFile();
    registerNixStoreRepair();
    registerNixStoreVerify();
    registerNixUpgradeNix();
    registerNixWhyDepends();
    // keep-sorted end
}

static bool haveProxyEnvironmentVariables()
{
    static const std::vector<std::string> proxyVariables = {
        // keep-sorted start
        "FTP_PROXY",
        "HTTPS_PROXY",
        "HTTP_PROXY",
        "ftp_proxy",
        "http_proxy",
        "https_proxy"
        // keep-sorted end
    };
    for (auto & proxyVariable: proxyVariables) {
        if (getEnv(proxyVariable).has_value()) {
            return true;
        }
    }
    return false;
}

/* Check if we have a non-loopback/link-local network interface. */
static bool haveInternet()
{
    struct ifaddrs * addrs;

    if (getifaddrs(&addrs))
        return true;

    Finally free([&]() { freeifaddrs(addrs); });

    for (auto i = addrs; i; i = i->ifa_next) {
        if (!i->ifa_addr) continue;
        if (i->ifa_addr->sa_family == AF_INET) {
            if (ntohl(reinterpret_cast<sockaddr_in *>(i->ifa_addr)->sin_addr.s_addr) != INADDR_LOOPBACK) {
                return true;
            }
        } else if (i->ifa_addr->sa_family == AF_INET6) {
            if (!IN6_IS_ADDR_LOOPBACK(&reinterpret_cast<sockaddr_in6 *>(i->ifa_addr)->sin6_addr) &&
                !IN6_IS_ADDR_LINKLOCAL(&reinterpret_cast<sockaddr_in6 *>(i->ifa_addr)->sin6_addr))
                return true;
        }
    }

    if (haveProxyEnvironmentVariables()) return true;

    return false;
}

std::string programPath;

struct NixArgs : virtual MultiCommand, virtual MixCommonArgs, virtual RootArgs
{
    bool useNet = true;
    bool refresh = false;
    bool helpRequested = false;
    bool showVersion = false;

    AsyncIoRoot & aio_;
    AsyncIoRoot & aio() override { return aio_; }

    NixArgs(const std::string & programName, AsyncIoRoot & aio)
        /* NOTE: when using lix, the command map is empty as `lix-command` is not stabilized neither designed.
         * `lix` is only used for external commands. */
        : MultiCommand(programName == "lix" ? CommandMap() : CommandRegistry::getCommandsFor({}), programName == "lix")
        , MixCommonArgs(programName)
        , aio_(aio)
    {
        categories.clear();
        categories[catHelp] = "Help commands";
        categories[Command::catDefault] = "Main commands";
        categories[catSecondary] = "Infrequently used commands";
        categories[catUtility] = "Utility/scripting commands";
        categories[catNixInstallation] = "Commands for upgrading or troubleshooting your Nix installation";

        if (programName != "lix") {
            addFlag({
                .longName = "help",
                .description = "Show usage information.",
                .category = miscCategory,
                .handler = {[this]() { this->helpRequested = true; }},
            });

            addFlag({
                .longName = "print-build-logs",
                .shortName = 'L',
                .description = "Print full build logs on standard error.",
                .category = loggingCategory,
                .handler = {[&]() { logger->setPrintBuildLogs(true); }},
                .experimentalFeature = Xp::NixCommand,
            });

            addFlag({
                .longName = "version",
                .description = "Show version information.",
                .category = miscCategory,
                .handler = {[&]() { showVersion = true; }},
            });

            addFlag({
                .longName = "offline",
                .aliases = {"no-net"}, // FIXME: remove
                .description = "Disable substituters and consider all previously downloaded files up-to-date.",
                .category = miscCategory,
                .handler = {[&]() { useNet = false; }},
                .experimentalFeature = Xp::NixCommand,
            });

            addFlag({
                .longName = "refresh",
                .description = "Consider all previously downloaded files out-of-date.",
                .category = miscCategory,
                .handler = {[&]() { refresh = true; }},
                .experimentalFeature = Xp::NixCommand,
            });
        }
    }

    std::map<std::string, std::vector<std::string>> aliases = {
        // keep-sorted start
        {"add-to-store", {"store", "add-path"}},
        {"cat-nar", {"nar", "cat"}},
        {"cat-store", {"store", "cat"}},
        {"copy-sigs", {"store", "copy-sigs"}},
        {"dev-shell", {"develop"}},
        {"diff-closures", {"store", "diff-closures"}},
        {"dump-path", {"store", "dump-path"}},
        {"hash-file", {"hash", "file"}},
        {"hash-path", {"hash", "path"}},
        {"ls-nar", {"nar", "ls"}},
        {"ls-store", {"store", "ls"}},
        {"make-content-addressable", {"store", "make-content-addressed"}},
        {"optimise-store", {"store", "optimise"}},
        {"ping-store", {"store", "ping"}},
        {"show-config", {"config", "show"}},
        {"show-derivation", {"derivation", "show"}},
        {"sign-paths", {"store", "sign"}},
        {"to-base16", {"hash", "to-base16"}},
        {"to-base32", {"hash", "to-base32"}},
        {"to-base64", {"hash", "to-base64"}},
        {"verify", {"store", "verify"}},
        // keep-sorted end
    };

    bool aliasUsed = false;

    Strings::iterator rewriteArgs(Strings & args, Strings::iterator pos) override
    {
        if (aliasUsed || command || pos == args.end()) return pos;
        auto arg = *pos;
        auto i = aliases.find(arg);
        if (i == aliases.end()) return pos;
        printTaggedWarning(
            "'%s' is a deprecated alias for '%s'", arg, concatStringsSep(" ", i->second)
        );
        pos = args.erase(pos);
        for (auto j = i->second.rbegin(); j != i->second.rend(); ++j)
            pos = args.insert(pos, *j);
        aliasUsed = true;
        return pos;
    }

    std::string description() override
    {
        return "a tool for reproducible and declarative configuration management";
    }

    std::string doc() override
    {
        return
          #include "nix.md"
          ;
    }

    void run() override
    {
        command->second->run();
    }

    // Plugins may add new subcommands.
    void pluginsInited() override
    {
        commands = CommandRegistry::getCommandsFor({});
    }

    std::string dumpCli()
    {
        auto res = JSON::object();

        res["args"] = toJSON();

        auto stores = JSON::object();
        for (auto & implem : *StoreImplementations::registered) {
            auto storeConfig = implem.getConfig();
            auto storeName = storeConfig->name();
            auto & j = stores[storeName];
            j["doc"] = storeConfig->doc();
            j["settings"] = storeConfig->toJSON();
            j["experimentalFeature"] = storeConfig->experimentalFeature();
        }
        res["stores"] = std::move(stores);

        return res.dump();
    }
};

/* Render the help for the specified subcommand to stdout using
   lowdown. */
static void showHelp(AsyncIoRoot & aio, std::vector<std::string> subcommand, NixArgs & toplevel)
{
    auto mdName = subcommand.empty() ? "nix" : fmt("nix3-%s", concatStringsSep("-", subcommand));

    evalSettings.restrictEval.override(false);
    evalSettings.pureEval.override(false);
    Evaluator evaluator(aio, {}, aio.blockOn(openStore("dummy://")));
    auto state = evaluator.begin(aio);

    Value vGenerateManpage;
    state->eval(
        evaluator.parseExprFromString(
#include "generate-manpage.nix.gen.hh"
            , CanonPath::root
        ),
        vGenerateManpage
    );

    Value vDump;
    vDump.mkString(toplevel.dumpCli());

    Value vRes;
    state->callFunction(vGenerateManpage, evaluator.builtins.get("false"), vRes, noPos);
    state->callFunction(vRes, vDump, vRes, noPos);

    auto attr = vRes.attrs()->get(evaluator.symbols.create(mdName + ".md"));
    if (!attr)
        throw UsageError("`nix` has no subcommand '%s'", concatStringsSep("", subcommand));

    auto markdown =
        state->forceString(attr->value, noPos, "while evaluating the lowdown help text");

    RunPager pager;
    std::cout << renderMarkdownToTerminal(markdown) << "\n";
}

static NixArgs & getNixArgs(Command & cmd)
{
    return dynamic_cast<NixArgs &>(cmd.getRoot());
}

struct CmdHelp : Command
{
    std::vector<std::string> subcommand;

    CmdHelp()
    {
        expectArgs({
            .label = "subcommand",
            .handler = {&subcommand},
        });
    }

    std::string description() override
    {
        return "show help about `nix` or a particular subcommand";
    }

    std::string doc() override
    {
        return
          #include "help.md"
          ;
    }

    Category category() override { return catHelp; }

    void run() override
    {
        assert(parent);
        MultiCommand * toplevel = parent;
        while (toplevel->parent) toplevel = toplevel->parent;
        showHelp(aio(), subcommand, getNixArgs(*this));
    }
};


struct CmdHelpStores : Command
{
    std::string description() override
    {
        return "show help about store types and their settings";
    }

    std::string doc() override
    {
        return
          #include "help-stores.md"
          ;
    }

    Category category() override { return catHelp; }

    void run() override
    {
        showHelp(aio(), {"help-stores"}, getNixArgs(*this));
    }
};

void registerNixHelp()
{
    registerCommand<CmdHelp>("help");
    registerCommand<CmdHelpStores>("help-stores");
}

void mainWrapped(AsyncIoRoot & aio, int argc, char * * argv)
{
    savedArgv = argv;

    /* The chroot helper needs to be run before any threads have been
       started. */
    if (argc > 0 && argv[0] == chrootHelperName) {
        chrootHelper(argc, argv);
        return;
    }

    initNix();
    initLibExpr();

    #if __linux__
    if (getuid() == 0) {
        try {
            saveMountNamespace();
            if (unshare(CLONE_NEWNS) == -1)
                throw SysError("setting up a private mount namespace");
        } catch (Error & e) { }
    }
    #endif

    programPath = argv[0];
    auto programName = std::string(baseNameOf(programPath));

    if (argc > 1 && std::string_view(argv[1]) == "__build-remote") {
        programName = "build-remote";
        argv++; argc--;
    }

    // Clean up the progress bar if shown using --log-format in a legacy command too.
    // Otherwise, this is a harmless no-op.
    Finally f([] { logger->pause(); });

    {
        registerLegacyCommands();
        auto legacy = (*LegacyCommandRegistry::commands)[programName];
        if (legacy) {
            return legacy(aio, std::string(baseNameOf(argv[0])), Strings(argv + 1, argv + argc));
        }
    }

    evalSettings.pureEval.setDefault(true);

    setLogFormat(LogFormat::bar);
    settings.verboseBuild = false;
    // FIXME: stop messing about with log verbosity depending on if it is interactive use
    if (isatty(STDERR_FILENO)) {
        verbosity = lvlNotice;
    } else {
        verbosity = lvlInfo;
    }

    registerCommands();
    // NOTE: out of over-cautiousness for backward compatibility,
    // the program name had always been `nix` for a long time.
    // Only when we invoke it as `lix`, we should propagate `lix`.
    NixArgs args(programName == "lix" ? programName : "nix", aio);

    if (argc == 2 && std::string(argv[1]) == "__dump-cli") {
        logger->cout(args.dumpCli());
        return;
    }

    if (argc == 2 && std::string(argv[1]) == "__dump-language") {
        experimentalFeatureSettings.experimentalFeatures.override(ExperimentalFeatures{}
            | Xp::Flakes
            | Xp::FetchClosure);
        evalSettings.pureEval.override(false);
        Evaluator state(aio, {}, aio.blockOn(openStore("dummy://")));
        auto res = JSON::object();
        res["builtins"] = ({
            auto builtinsJson = JSON::object();
            auto builtins = state.builtins.env.values[0].attrs();
            for (auto & builtin : *builtins) {
                auto b = JSON::object();
                if (!builtin.value.isPrimOp()) {
                    continue;
                }
                auto primOp = builtin.value.primOp();
                if (!primOp->doc) {
                    continue;
                }
                b["arity"] = primOp->arity;
                b["args"] = primOp->args;
                b["doc"] = trim(stripIndentation(primOp->doc));
                b["experimental-feature"] = primOp->experimentalFeature;
                builtinsJson[std::string(state.symbols[builtin.name])] = std::move(b);
            }
            std::move(builtinsJson);
        });
        res["constants"] = ({
            auto constantsJson = JSON::object();
            for (auto & [name, info] : state.builtins.constantInfos) {
                auto c = JSON::object();
                if (!info.doc) continue;
                c["doc"] = trim(stripIndentation(info.doc));
                c["type"] = showType(info.type, false);
                c["impure-only"] = info.impureOnly;
                constantsJson[name] = std::move(c);
            }
            std::move(constantsJson);
        });
        logger->cout("%s", res);
        return;
    }

    if (argc == 2 && std::string(argv[1]) == "__dump-xp-features") {
        logger->cout(documentExperimentalFeatures().dump());
        return;
    }

    if (argc == 2 && std::string(argv[1]) == "__dump-dp-features") {
        logger->cout(documentDeprecatedFeatures().dump());
        return;
    }

    try {
        args.parseCmdline({argv + 1, argv + argc});
    } catch (UsageError &) {
        if (!args.helpRequested && !args.completions) throw;
    }

    if (args.completions) {
        switch (args.completions->type) {
        case Completions::Type::Normal:
            logger->cout("normal"); break;
        case Completions::Type::Filenames:
            logger->cout("filenames"); break;
        case Completions::Type::Attrs:
            logger->cout("attrs"); break;
        }
        for (auto & s : args.completions->completions)
            logger->cout(s.completion + "\t" + trim(s.description));
        return;
    }

    if (args.helpRequested) {
        std::vector<std::string> subcommand;
        MultiCommand * command = &args;
        while (command) {
            if (command && command->command) {
                subcommand.push_back(command->command->first);
                command = dynamic_cast<MultiCommand *>(&*command->command->second);
            } else
                break;
        }
        showHelp(aio, subcommand, args);
        return;
    }

    if (args.showVersion) {
        printVersion(programName);
        return;
    }

    if (!args.command)
        throw UsageError("no subcommand specified");

    experimentalFeatureSettings.require(
        args.command->second->experimentalFeature());

    if (args.useNet && !haveInternet()) {
        printTaggedWarning(
            "you don't have Internet access; disabling some network-dependent features"
        );
        args.useNet = false;
    }

    if (!args.useNet) {
        // FIXME: should check for command line overrides only.
        settings.useSubstitutes.setDefault(false);
        settings.tarballTtl.setDefault(std::numeric_limits<unsigned int>::max());
        fileTransferSettings.tries.setDefault(1);
        fileTransferSettings.maxConnectTimeout.setDefault(1);
        fileTransferSettings.initialConnectTimeout.setDefault(1);
    }

    if (args.refresh) {
        settings.tarballTtl.override(0);
        settings.ttlNegativeNarInfoCache.override(0);
        settings.ttlPositiveNarInfoCache.override(0);
    }

    if (args.command->second->forceImpureByDefault()) {
        evalSettings.pureEval.setDefault(false);
    }
    args.run();
}

}

int main(int argc, char * * argv)
{
    if (argc < 1) {
        std::cerr << "no, we don't have pkexec at home. provide argv[0]." << std::endl;
        std::abort();
    }

    // Increase the default stack size for the evaluator and for
    // libstdc++'s std::regex.
    nix::setStackSize(64 * 1024 * 1024);

    return nix::handleExceptions(argv[0], [&]() {
        nix::AsyncIoRoot aio;
        nix::mainWrapped(aio, argc, argv);
    });
}
