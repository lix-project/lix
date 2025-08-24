#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/path.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async-collect.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include "sigs.hh"

#include <atomic>
#include <functional>

namespace nix {

struct CmdCopySigs : StorePathsCommand
{
    Strings substituterUris;

    CmdCopySigs()
    {
        addFlag({
            .longName = "substituter",
            .shortName = 's',
            .description = "Copy signatures from the specified store.",
            .labels = {"store-uri"},
            .handler = {[&](std::string s) { substituterUris.push_back(s); }},
        });
    }

    std::string description() override
    {
        return "copy store path signatures from substituters";
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        if (substituterUris.empty())
            throw UsageError("you must specify at least one substituter using '-s'");

        // FIXME: factor out commonality with MixVerify.
        std::vector<ref<Store>> substituters;
        for (auto & s : substituterUris)
            substituters.push_back(aio().blockOn(openStore(s)));

        size_t added = 0;

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        auto doPath = [&](const StorePath & storePath) -> kj::Promise<Result<void>> {
            try {
                auto info = TRY_AWAIT(store->queryPathInfo(storePath));

                StringSet newSigs;

                for (auto & store2 : substituters) {
                    try {
                        auto info2 = TRY_AWAIT(store2->queryPathInfo(info->path));

                        /* Don't import signatures that don't match this
                           binary. */
                        if (info->narHash != info2->narHash || info->narSize != info2->narSize
                            || info->references != info2->references)
                        {
                            continue;
                        }

                        for (auto & sig : info2->sigs) {
                            if (!info->sigs.count(sig)) {
                                newSigs.insert(sig);
                            }
                        }
                    } catch (InvalidPath &) {
                    }
                }

                if (!newSigs.empty()) {
                    TRY_AWAIT(store->addSignatures(storePath, newSigs));
                    added += newSigs.size();
                }

                co_return result::success();
            } catch (...) {
                co_return result::current_exception();
            }
        };

        aio().blockOn(asyncSpread(storePaths, doPath));

        printInfo("imported %d signatures", added);
    }
};

struct CmdSign : StorePathsCommand
{
    Path secretKeyFile;

    CmdSign()
    {
        addFlag({
            .longName = "key-file",
            .shortName = 'k',
            .description = "File containing the secret signing key.",
            .labels = {"file"},
            .handler = {&secretKeyFile},
            .completer = completePath
        });
    }

    std::string description() override
    {
        return "sign store paths";
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        if (secretKeyFile.empty())
            throw UsageError("you must specify a secret key file using '-k'");

        auto secretKey = SecretKey::parse(readFile(secretKeyFile));

        size_t added = 0;

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        auto doPath = [&](const StorePath & storePath) -> kj::Promise<Result<void>> {
            try {
                auto info = TRY_AWAIT(store->queryPathInfo(storePath));

                auto info2(*info);
                info2.sigs.clear();
                info2.sign(*store, secretKey);
                assert(!info2.sigs.empty());

                if (!info->sigs.count(*info2.sigs.begin())) {
                    TRY_AWAIT(store->addSignatures(storePath, info2.sigs));
                    added++;
                }

                co_return result::success();
            } catch (...) {
                co_return result::current_exception();
            }
        };

        aio().blockOn(asyncSpread(storePaths, doPath));

        printInfo("added %d signatures", added);
    }
};

struct CmdKeyGenerateSecret : Command
{
    std::optional<std::string> keyName;

    CmdKeyGenerateSecret()
    {
        addFlag({
            .longName = "key-name",
            .description = "Identifier of the key (e.g. `cache.example.org-1`).",
            .labels = {"name"},
            .handler = {&keyName},
        });
    }

    std::string description() override
    {
        return "generate a secret key for signing store paths";
    }

    std::string doc() override
    {
        return
          #include "key-generate-secret.md"
          ;
    }

    void run() override
    {
        if (!keyName)
            throw UsageError("required argument '--key-name' is missing");

        writeFull(STDOUT_FILENO, SecretKey::generate(*keyName).to_string());
    }
};

struct CmdKeyConvertSecretToPublic : Command
{
    std::string description() override
    {
        return "generate a public key for verifying store paths from a secret key read from standard input";
    }

    std::string doc() override
    {
        return
          #include "key-convert-secret-to-public.md"
          ;
    }

    void run() override
    {
        auto secretKey = SecretKey::parse(drainFD(STDIN_FILENO));
        writeFull(STDOUT_FILENO, secretKey.toPublicKey().to_string());
    }
};

struct CmdKey : MultiCommand
{
    CmdKey()
        : MultiCommand({
            {"generate-secret",
             [](auto & aio) { return make_ref<MixAio<CmdKeyGenerateSecret>>(aio); }},
            {"convert-secret-to-public",
             [](auto & aio) { return make_ref<MixAio<CmdKeyConvertSecretToPublic>>(aio); }},
        })
    {
    }

    std::string description() override
    {
        return "generate and convert Nix signing keys";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix key' requires a sub-command.");

        logger->pause();
        command->second->run();
    }
};

void registerNixSigs()
{
    registerCommand2<CmdCopySigs>({"store", "copy-sigs"});
    registerCommand2<CmdSign>({"store", "sign"});
    registerCommand<CmdKey>("key");
}

}
