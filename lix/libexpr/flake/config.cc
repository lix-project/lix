#include "lix/libexpr/flake/flake.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/users.hh"
#include "lix/libfetchers/fetch-settings.hh"

namespace nix::flake {

// setting name -> setting value -> allow or ignore.
typedef std::map<std::string, std::map<std::string, bool>> TrustedList;

Path trustedListPath()
{
    return getDataDir() + "/nix/trusted-settings.json";
}

static TrustedList readTrustedList()
{
    auto path = trustedListPath();
    if (!pathExists(path)) return {};
    auto json = json::parse(readFile(path), "trusted flake settings");
    return json;
}

static void writeTrustedList(const TrustedList & trustedList)
{
    auto path = trustedListPath();
    createDirs(dirOf(path));
    writeFile(path, JSON(trustedList).dump());
}

static bool batchAskForSetting(
    bool & negativeTrustOverride,
    TrustedList & trustedList,
    std::map<std::string, std::string> & untrustedSettings)
{
    printWarning("The following settings require your decision:");
    for (const auto & [name, valueS] : untrustedSettings) {
        // FIXME: filter ANSI escapes, newlines, \r, etc.
        logger->cout("- %s = %s", name, valueS);
    }

    auto reply = logger
                     ->ask(
                         fmt("Do you want to allow configuration settings to be applied?\nThis may allow the "
                             "flake to gain root, see the nix.conf manual page (" ANSI_BOLD "y" ANSI_NORMAL
                             "es for now/" ANSI_BOLD "A" ANSI_NORMAL "llow always/" ANSI_BOLD "n" ANSI_NORMAL
                             "o/" ANSI_BOLD "N" ANSI_NORMAL "o to all) ")
                     )
                     .value_or('n');

    if (reply == 'N') {
        printWarning("Rejecting all untrusted nix.conf entries");
        printTaggedWarning(
            "you can set '%s' to '%b' to automatically reject configuration options supplied by "
            "flakes",
            "accept-flake-config",
            false
        );
        negativeTrustOverride = true;
        return false;
    }

    if (reply == 'y' || reply == 'A') {
        auto alwaysAllow = reply == 'A';
        for (const auto & [name, valueS] : untrustedSettings) {
            if (alwaysAllow) {
                trustedList[name][valueS] = true;
            }
            globalConfig.set(name, valueS);
        }

        if (alwaysAllow) {
            writeTrustedList(trustedList);
        }

        return true;
    } else {
        printTaggedWarning(
            "you can set '%s' to '%b' to automatically reject configuration options supplied "
            "by flakes",
            "accept-flake-config",
            false
        );
    }

    auto didTrustedListChange = false;
    for (const auto & [name, valueS] : untrustedSettings) {
        auto individualReply = logger
                                   ->ask(
                                       fmt("Do you want to allow setting '%s = %s'? (" ANSI_BOLD
                                           "y" ANSI_NORMAL "es for now/" ANSI_BOLD "A" ANSI_NORMAL
                                           "llow always/" ANSI_BOLD "n" ANSI_NORMAL "o for now) ",
                                           name,
                                           valueS)
                                   )
                                   .value_or('n');

        if (individualReply == 'y' || individualReply == 'A') {
            if (individualReply == 'A') {
                trustedList[name][valueS] = true;
                didTrustedListChange = true;
            }

            globalConfig.set(name, valueS);
        }
    }

    if (didTrustedListChange) {
        writeTrustedList(trustedList);
    }

    return false;
}

void ConfigFile::apply()
{
    std::set<std::string> whitelist{"bash-prompt", "bash-prompt-prefix", "bash-prompt-suffix", "flake-registry", "commit-lockfile-summary"};

    // Allows to ignore all subsequent settings from this file.
    bool negativeTrustOverride = false;

    std::map<std::string, std::string> untrustedSettings;

    TrustedList trustedList = readTrustedList();

    for (auto & [name, value] : settings) {
        auto baseName = name.starts_with("extra-") ? std::string(name, 6) : name;

        // FIXME: Move into libutil/config.cc.
        std::string valueS;
        if (auto* s = std::get_if<std::string>(&value))
            valueS = *s;
        else if (auto* n = std::get_if<int64_t>(&value))
            valueS = fmt("%d", *n);
        else if (auto* b = std::get_if<Explicit<bool>>(&value))
            valueS = b->t ? "true" : "false";
        else if (auto ss = std::get_if<std::vector<std::string>>(&value))
            valueS = concatStringsSep(" ", *ss);  // FIXME: evil
        else
            assert(false);

        bool trusted = whitelist.count(baseName);

        if (!trusted) {
            switch (nix::fetchSettings.acceptFlakeConfig.get()) {
            case AcceptFlakeConfig::True: {
                trusted = true;
                break;
            }
            case AcceptFlakeConfig::Ask: {
                auto tlname = get(trustedList, name);
                if (auto saved = tlname ? get(*tlname, valueS) : nullptr) {
                    trusted = *saved;
                    printInfo("Using saved setting for '%s = %s' from ~/.local/share/nix/trusted-settings.json.", name, valueS);
                } else {
                    untrustedSettings[name] = valueS;
                }
                break;
            }
            case AcceptFlakeConfig::False: {
                trusted = false;
                break;
            };
            }
        }

        if (trusted) {
            debug("accepting trusted flake configuration setting '%s'", name);
            globalConfig.set(name, valueS);
        } else {
            printTaggedWarning(
                "ignoring untrusted flake configuration setting '%s', pass '%s' to trust it (may "
                "allow the flake to gain root, see the nix.conf manual page)",
                name,
                "--accept-flake-config"
            );
        }
    }

    if (!untrustedSettings.empty()) {
        batchAskForSetting(negativeTrustOverride, trustedList, untrustedSettings);
    }
}

}
