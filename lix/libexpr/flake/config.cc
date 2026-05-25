#include "lix/libexpr/flake/flake.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/users.hh"
#include "lix/libfetchers/fetch-settings.hh"
#include <cctype>

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

static bool
askForEachSetting(TrustedList & trustedList, std::map<std::string, std::string> & untrustedSettings)
{
    // clang-format off
    constexpr auto prompt =
        "[" ANSI_BOLD "y" ANSI_NORMAL "]es for now/"
        "[" ANSI_BOLD "N" ANSI_NORMAL "]o for now/"
        "[" ANSI_BOLD "a" ANSI_NORMAL "]lways allow";
    // clang-format on

    auto didTrustedListChange = false;
    int acceptedCount = 0;
    for (const auto & [name, valueS] : untrustedSettings) {
        auto reply =
            logger
                ->ask(
                    fmt("Do you want to allow setting '" ANSI_MAGENTA "%s = %s" ANSI_NORMAL "'? (%s) ",
                        name,
                        valueS,
                        prompt)
                )
                .value_or("n");

        reply = toLower(reply);

        static const std::string yes = "yes for now";
        static const std::string no = "no for now";
        static const std::string always = "always allow";

        while (true) {
            if (no.starts_with(reply)) {
                break;
            }

            if (yes.starts_with(reply) || always.starts_with(reply)) {
                if (reply[0] == 'a') {
                    trustedList[name][valueS] = true;
                    didTrustedListChange = true;
                }

                globalConfig.set(name, valueS);
                acceptedCount++;
                break;
            }

            // if the reply wasn't a prefix of any answer, ask the user again
            reply = logger->ask(fmt("Couldn't understand reply.\n%s: ", prompt)).value_or("n");
        }
    }

    if (didTrustedListChange) {
        writeTrustedList(trustedList);
    }

    // return false if *none* of the settings were accepted
    return acceptedCount > 0;
}

static bool batchAskForSetting(
    bool & negativeTrustOverride,
    TrustedList & trustedList,
    std::map<std::string, std::string> & untrustedSettings)
{
    std::string warning("The following settings require your decision:");
    for (const auto & [name, valueS] : untrustedSettings) {
        // FIXME: filter ANSI escapes, newlines, \r, etc.
        warning += fmt("\n- %s = %s", name, valueS);
    }

    printWarning("%s", warning);

    // clang-format off
    constexpr auto globalPrompt =
        "[" ANSI_BOLD "y" ANSI_NORMAL "]es for now/"
        "[" ANSI_BOLD "n" ANSI_NORMAL "]o for now/"
        "[" ANSI_BOLD "a" ANSI_NORMAL "]lways allow/"
        "[" ANSI_BOLD "I" ANSI_NORMAL "]ndividually review";
    // clang-format on

    auto reply =
        logger
            ->ask(
                fmt("Do you want to allow these configuration settings to be applied?\n" ANSI_BOLD
                    "This may allow the flake to gain root" ANSI_NORMAL ", see the nix.conf manual page.\n"
                    "(%s) ",
                    globalPrompt)
            )
            .value_or("I"); // if the answer is empty (or there is no interactive prompt),
                            // just default to reviewing each individually

    reply = toLower(reply);

    static const std::string yes = "yes for now";
    static const std::string no = "no for now";
    static const std::string always = "always allow";
    static const std::string review = "individually review";

    // when interactive, loops and reprompts until the reply is one of y/n/a/i (or any prefix of the answers)
    while (true) {
        if (no.starts_with(reply)) {
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

        if (yes.starts_with(reply) || always.starts_with(reply)) {
            auto alwaysAllow = reply[0] == 'a';
            for (const auto & [name, valueS] : untrustedSettings) {
                if (alwaysAllow) {
                    trustedList[name][valueS] = true;
                }
                globalConfig.set(name, valueS);
            }

            if (alwaysAllow) {
                printTaggedWarning(
                    "adding these configuration settings to the trusted list at %s, "
                    "edit it if you want to remove them in the future",
                    trustedListPath()
                );
                writeTrustedList(trustedList);
            }

            return true;
        }

        if (review.starts_with(reply)) {
            return askForEachSetting(trustedList, untrustedSettings);
        }

        // if the reply wasn't a prefix of any, ask the user again
        reply = logger->ask(fmt("Couldn't understand reply.\n%s: ", globalPrompt)).value_or("n");
    }
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
