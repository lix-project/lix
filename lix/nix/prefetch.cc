#include "lix/libcmd/command.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/loggers.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/tarfile.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libexpr/eval-inline.hh" // IWYU pragma: keep
#include "lix/libcmd/legacy.hh"
#include "lix/libstore/temporary-dir.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/terminal.hh"
#include "prefetch.hh"

namespace nix {

/* If ‘url’ starts with ‘mirror://’, then resolve it using the list of
   mirrors defined in Nixpkgs. */
std::string resolveMirrorUrl(EvalState & state, const std::string & url)
{
    if (url.substr(0, 9) != "mirror://") return url;

    std::string s(url, 9);
    auto p = s.find('/');
    if (p == std::string::npos) throw Error("invalid mirror URL '%s'", url);
    std::string mirrorName(s, 0, p);

    Value vMirrors;
    // FIXME: use nixpkgs flake
    state.eval(state.ctx.parseExprFromString(
            "import <nixpkgs/pkgs/build-support/fetchurl/mirrors.nix>",
            CanonPath::root),
        vMirrors);
    state.forceAttrs(vMirrors, noPos, "while evaluating the set of all mirrors");

    auto mirrorList = vMirrors.attrs()->get(state.ctx.symbols.create(mirrorName));
    if (!mirrorList) {
        throw Error("unknown mirror name '%s'", mirrorName);
    }
    state.forceList(mirrorList->value, noPos, "while evaluating one mirror configuration");

    if (mirrorList->value.listSize() < 1) {
        throw Error("mirror URL '%s' did not expand to anything", url);
    }

    std::string mirror(state.forceString(
        mirrorList->value.listElems()[0], noPos, "while evaluating the first available mirror"
    ));
    return mirror + (mirror.ends_with("/") ? "" : "/") + s.substr(p + 1);
}

std::tuple<StorePath, Hash> prefetchFile(
    AsyncIoRoot & aio,
    ref<Store> store,
    const std::string & url,
    std::optional<std::string> name,
    HashType hashType,
    std::optional<Hash> expectedHash,
    bool unpack,
    bool executable)
{
    auto ingestionMethod = unpack || executable ? FileIngestionMethod::Recursive : FileIngestionMethod::Flat;

    /* Figure out a name in the Nix store. */
    if (!name) {
        name = baseNameOf(url);
        if (name->empty())
            throw Error("cannot figure out file name for '%s'", url);
    }

    std::optional<StorePath> storePath;
    std::optional<Hash> hash;

    /* If an expected hash is given, the file may already exist in
       the store. */
    if (expectedHash) {
        hashType = expectedHash->type;
        storePath = store->makeFixedOutputPath(*name, FixedOutputInfo {
            .method = ingestionMethod,
            .hash = *expectedHash,
            .references = {},
        });
        if (aio.blockOn(store->isValidPath(*storePath)))
            hash = expectedHash;
        else
            storePath.reset();
    }

    if (!storePath) {

        AutoDelete tmpDir(createTempDir(), true);
        Path tmpFile = (Path) tmpDir + "/tmp";

        /* Download the file. */
        {
            auto mode = 0600;
            if (executable)
                mode = 0700;

            AutoCloseFD fd{open(tmpFile.c_str(), O_WRONLY | O_CREAT | O_EXCL, mode)};
            if (!fd) throw SysError("creating temporary file '%s'", tmpFile);

            FdSink sink(fd.get());

            aio.blockOn(aio.blockOn(getFileTransfer()->download(url)).second->drainInto(sink));
        }

        /* Optionally unpack the file. */
        if (unpack) {
            Activity act(*logger, lvlChatty, actUnknown,
                fmt("unpacking '%s'", url));
            Path unpacked = (Path) tmpDir + "/unpacked";
            createDirs(unpacked);
            unpackTarfile(tmpFile, unpacked);

            /* If the archive unpacks to a single file/directory, then use
               that as the top-level. */
            auto entries = readDirectory(unpacked);
            if (entries.size() == 1)
                tmpFile = unpacked + "/" + entries[0].name;
            else
                tmpFile = unpacked;
        }

        Activity act(*logger, lvlChatty, actUnknown,
            fmt("adding '%s' to the store", url));

        auto info = aio.blockOn(
            store->addToStoreSlow(*name, tmpFile, ingestionMethod, hashType, expectedHash)
        );
        storePath = info.path;
        assert(info.ca);
        hash = info.ca->hash;
    }

    return {storePath.value(), hash.value()};
}

static int main_nix_prefetch_url(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        HashType ht = HashType::SHA256;
        std::vector<std::string> args;
        bool printPath = getEnv("PRINT_PATH") == "1";
        bool fromExpr = false;
        std::string attrPath;
        bool unpack = false;
        bool executable = false;
        std::optional<std::string> name;

        struct MyArgs : LegacyArgs, MixEvalArgs
        {
            using LegacyArgs::LegacyArgs;
        };

        MyArgs myArgs(aio, programName, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-prefetch-url");
            else if (*arg == "--version")
                printVersion("nix-prefetch-url");
            else if (*arg == "--type") {
                auto s = getArg(*arg, arg, end);
                ht = parseHashType(s);
            }
            else if (*arg == "--print-path")
                printPath = true;
            else if (*arg == "--attr" || *arg == "-A") {
                fromExpr = true;
                attrPath = getArg(*arg, arg, end);
            }
            else if (*arg == "--unpack")
                unpack = true;
            else if (*arg == "--executable")
                executable = true;
            else if (*arg == "--name")
                name = getArg(*arg, arg, end);
            else if (*arg != "" && arg->at(0) == '-')
                return false;
            else
                args.push_back(*arg);
            return true;
        });

        myArgs.parseCmdline(argv);

        if (args.size() > 2)
            throw UsageError("too many arguments");

        if (isOutputARealTerminal(StandardOutputStream::Stderr))
            setLogFormat(LogFormat::bar);

        auto store = aio.blockOn(openStore());
        auto evaluator = std::make_unique<Evaluator>(aio, myArgs.searchPath, store);
        auto state = evaluator->begin(aio);

        Bindings & autoArgs = *myArgs.getAutoArgs(*evaluator);

        /* If -A is given, get the URL from the specified Nix
           expression. */
        std::string url;
        if (!fromExpr) {
            if (args.empty())
                throw UsageError("you must specify a URL");
            url = args[0];
        } else {
            Value vRoot;
            state->evalFile(
                evaluator->paths.resolveExprPath(
                    aio.blockOn(lookupFileArg(*evaluator, args.empty() ? "." : args[0])).unwrap()),
                vRoot);
            Value v(findAlongAttrPath(*state, attrPath, autoArgs, vRoot).first);
            state->forceAttrs(v, noPos, "while evaluating the source attribute to prefetch");

            /* Extract the URL. */
            auto * attr = v.attrs()->get(evaluator->symbols.create("urls"));
            if (!attr)
                throw Error("attribute 'urls' missing");
            state->forceList(attr->value, noPos, "while evaluating the urls to prefetch");
            if (attr->value.listSize() < 1) {
                throw Error("'urls' list is empty");
            }
            url = state->forceString(
                attr->value.listElems()[0],
                noPos,
                "while evaluating the first url from the urls list"
            );

            /* Extract the hash mode. */
            auto attr2 = v.attrs()->get(evaluator->symbols.create("outputHashMode"));
            if (!attr2)
                printInfo("warning: this does not look like a fetchurl call");
            else
                unpack = state->forceString(
                             attr2->value,
                             noPos,
                             "while evaluating the outputHashMode of the source to prefetch"
                         )
                    == "recursive";

            /* Extract the name. */
            if (!name) {
                auto attr3 = v.attrs()->get(evaluator->symbols.create("name"));
                if (!attr3)
                    name = state->forceString(
                        attr3->value, noPos, "while evaluating the name of the source to prefetch"
                    );
            }
        }

        std::optional<Hash> expectedHash;
        if (args.size() == 2)
            expectedHash = Hash::parseAny(args[1], ht);

        auto [storePath, hash] = prefetchFile(
            aio, store, resolveMirrorUrl(*state, url), name, ht, expectedHash, unpack, executable
        );

        logger->pause();

        if (!printPath)
            printInfo("path is '%s'", store->printStorePath(storePath));

        logger->cout(printHash16or32(hash));
        if (printPath)
            logger->cout(store->printStorePath(storePath));

        return 0;
    }
}

void registerLegacyNixPrefetchUrl() {
    LegacyCommandRegistry::add("nix-prefetch-url", main_nix_prefetch_url);
}

struct CmdStorePrefetchFile : StoreCommand, MixJSON
{
    std::string url;
    bool executable = false;
    bool unpack = false;
    std::optional<std::string> name;
    HashType hashType = HashType::SHA256;
    std::optional<Hash> expectedHash;

    CmdStorePrefetchFile()
    {
        addFlag({
            .longName = "name",
            .description = "Override the name component of the resulting store path. It defaults to the base name of *url*.",
            .labels = {"name"},
            .handler = {&name}
        });

        addFlag({
            .longName = "expected-hash",
            .description = "The expected hash of the file.",
            .labels = {"hash"},
            .handler = {[&](std::string s) {
                expectedHash = Hash::parseAny(s, hashType);
            }}
        });

        addFlag(Flag::mkHashTypeFlag("hash-type", &hashType));

        addFlag({
            .longName = "executable",
            .description =
                "Make the resulting file executable. Note that this causes the "
                "resulting hash to be a NAR hash rather than a flat file hash.",
            .handler = {&executable, true},
        });

        addFlag({
            .longName = "unpack",
            .description =
                "Unpack the archive (which must be a tarball or zip file) and add "
                "the result to the Nix store.",
            .handler = {&unpack, true},
        });

        expectArg("url", &url);
    }

    std::string description() override
    {
        return "download a file into the Nix store";
    }

    std::string doc() override
    {
        return
          #include "store-prefetch-file.md"
          ;
    }
    void run(ref<Store> store) override
    {
        auto [storePath, hash] =
            prefetchFile(aio(), store, url, name, hashType, expectedHash, unpack, executable);

        if (json) {
            auto res = JSON::object();
            res["storePath"] = store->printStorePath(storePath);
            res["hash"] = hash.to_string(Base::SRI, true);
            logger->cout(res.dump());
        } else {
            notice("Downloaded '%s' to '%s' (hash '%s').",
                url,
                store->printStorePath(storePath),
                hash.to_string(Base::SRI, true));
        }
    }
};

void registerNixStorePrefetchFile()
{
    registerCommand2<CmdStorePrefetchFile>({"store", "prefetch-file"});
}

}
