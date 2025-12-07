#include "lix/libutil/abstract-config.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/strings.hh"

namespace nix {

static void applyConfigInner(
    const std::string & contents,
    const ApplyConfigOptions & options,
    std::vector<std::pair<std::string, std::string>> & parsedContents
)
{
    unsigned int pos = 0;

    while (pos < contents.size()) {
        std::string line;
        while (pos < contents.size() && contents[pos] != '\n') {
            line += contents[pos++];
        }
        pos++;

        if (auto hash = line.find('#'); hash != line.npos) {
            line = std::string(line, 0, hash);
        }

        auto tokens = tokenizeString<std::vector<std::string>>(line);
        if (tokens.empty()) {
            continue;
        }

        if (tokens.size() < 2) {
            throw UsageError(
                "illegal configuration line '%1%' in '%2%'", line, options.relativeDisplay()
            );
        }

        auto include = false;
        auto ignoreMissing = false;
        if (tokens[0] == "include") {
            include = true;
        } else if (tokens[0] == "!include") {
            include = true;
            ignoreMissing = true;
        }

        if (include) {
            if (tokens.size() != 2) {
                throw UsageError(
                    "illegal configuration line '%1%' in '%2%'", line, options.relativeDisplay()
                );
            }
            if (!options.path) {
                throw UsageError("can only include configuration '%1%' from files", tokens[1]);
            }
            auto pathToInclude = absPath(tildePath(tokens[1], options.home), dirOf(*options.path));
            if (pathExists(pathToInclude)) {
                auto includeOptions = ApplyConfigOptions{
                    .path = pathToInclude,
                    .home = options.home,
                };
                try {
                    std::string includedContents = readFile(pathToInclude);
                    applyConfigInner(includedContents, includeOptions, parsedContents);
                } catch (SysError &) {
                    // TODO: Do we actually want to ignore this? Or is it better to fail?
                }
            } else if (!ignoreMissing) {
                throw Error(
                    "file '%1%' included from '%2%' not found", pathToInclude, *options.path
                );
            }
            continue;
        }

        if (tokens[1] != "=") {
            throw UsageError(
                "illegal configuration line '%1%' in '%2%'", line, options.relativeDisplay()
            );
        }

        std::string name = std::move(tokens[0]);

        auto i = tokens.begin();
        advance(i, 2);

        parsedContents.push_back({
            std::move(name),
            concatStringsSep(" ", Strings(i, tokens.end())),
        });
    };
}

AbstractConfig::AbstractConfig(StringMap initials) : unknownSettings(std::move(initials)) {}

void AbstractConfig::applyConfig(const std::string & contents, const ApplyConfigOptions & options)
{
    std::vector<std::pair<std::string, std::string>> parsedContents;

    applyConfigInner(contents, options, parsedContents);

    // First apply experimental-feature related settings
    for (const auto & [name, value] : parsedContents) {
        if (name == "experimental-features" || name == "extra-experimental-features") {
            set(name, value, options);
        }
    }

    // Then apply other settings
    for (const auto & [name, value] : parsedContents) {
        if (name != "experimental-features" && name != "extra-experimental-features") {
            set(name, value, options);
        }
    }
}

void AbstractConfig::warnUnknownSettings()
{
    for (const auto & s : unknownSettings) {
        printTaggedWarning("unknown setting '%s'", s.first);
    }
}

void AbstractConfig::reapplyUnknownSettings()
{
    auto unknownSettings2 = std::move(unknownSettings);
    unknownSettings = {};
    for (auto const & [name, value] : unknownSettings2) {
        set(name, value);
    }
}

}
