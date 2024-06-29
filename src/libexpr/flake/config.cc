#include "flake.hh"
#include "logging.hh"
#include "users.hh"
#include "fetch-settings.hh"

#include <nlohmann/json.hpp>

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
    auto json = nlohmann::json::parse(readFile(path));
    return json;
}

static void writeTrustedList(const TrustedList & trustedList)
{
    auto path = trustedListPath();
    createDirs(dirOf(path));
    writeFile(path, nlohmann::json(trustedList).dump());
}

void ConfigFile::apply()
{
    std::set<std::string> whitelist{"bash-prompt", "bash-prompt-prefix", "bash-prompt-suffix", "flake-registry", "commit-lockfile-summary"};

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
            valueS = concatStringsSep(" ", *ss); // FIXME: evil
        else
            assert(false);

        bool trusted = whitelist.count(baseName);
        if (!trusted) {
            switch (nix::fetchSettings.acceptFlakeConfig) {
            case AcceptFlakeConfig::True: {
                trusted = true;
                break;
            }
            case AcceptFlakeConfig::Ask: {
                auto trustedList = readTrustedList();
                auto tlname = get(trustedList, name);
                if (auto saved = tlname ? get(*tlname, valueS) : nullptr) {
                    trusted = *saved;
                    printInfo("Using saved setting for '%s = %s' from ~/.local/share/nix/trusted-settings.json.", name, valueS);
                } else {
                    // FIXME: filter ANSI escapes, newlines, \r, etc.
                    if (std::tolower(logger->ask(fmt("Do you want to allow configuration setting '%s' to be set to '" ANSI_RED "%s" ANSI_NORMAL "' (y/N)? This may allow the flake to gain root, see the nix.conf manual page.", name, valueS)).value_or('n')) == 'y') {
                        trusted = true;
                    } else {
                        warn("you can set '%s' to '%b' to automatically reject configuration options supplied by flakes", "accept-flake-config", false);
                    }
                    if (std::tolower(logger->ask(fmt("do you want to permanently mark this value as %s (y/N)?",  trusted ? "trusted": "untrusted" )).value_or('n')) == 'y') {
                        trustedList[name][valueS] = trusted;
                        writeTrustedList(trustedList);
                    }
                }
                break;
            }
            case nix::AcceptFlakeConfig::False: {
                trusted = false;
                break;
            };
            }
        }

        if (trusted) {
            debug("accepting trusted flake configuration setting '%s'", name);
            globalConfig.set(name, valueS);
        } else {
            warn("ignoring untrusted flake configuration setting '%s', pass '%s' to trust it (may allow the flake to gain root, see the nix.conf manual page)", name, "--accept-flake-config");
        }
    }
}

}
