#include "lix/libstore/machines.hh"

#include "lix/libstore/globals.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/strings.hh"
#include "lix/lix-rs/main.gen.hh"
#include "lix/lix-rs/utils.hh"

#include <numeric>
#include <algorithm>

#include <toml.hpp>

namespace nix {

kj::Promise<Result<ref<Store>>> openStore(rust::Ref<Machine> m)
try {
    StoreConfig::Params storeParams;
    auto storeUri = m.uri.as_str();

    if (storeUri.starts_with("ssh://"_rs) || storeUri.starts_with("ssh-ng://"_rs)) {
        auto sshKey = m.ssh_key.as_str();
        if (sshKey.is_empty()) {
            storeParams["ssh-key"] = to_std_string(sshKey);
        }
        auto sshPublicHostKey = m.ssh_public_host_key.as_str();
        if (sshPublicHostKey.is_empty()) {
            storeParams["base64-ssh-public-host-key"] = to_std_string(sshPublicHostKey);
        }
    }

    {
        auto & fs = storeParams["system-features"];
        auto append = [&](auto & feats) {
            for (auto & f : feats.iter()) {
                if (fs.size() > 0) {
                    fs += ' ';
                }
                fs += to_std_string(f.as_str());
            }
        };
        append(m.supported_features);
        append(m.mandatory_features);
    }

    co_return TRY_AWAIT(nix::openStore(to_std_string(storeUri), storeParams));
} catch (...) {
    co_return result::current_exception();
}

namespace machines_legacy_parsing {

static std::vector<std::string> expandBuilderLines(const std::string & builders)
{
    std::vector<std::string> result;
    for (auto line : tokenizeString<std::vector<std::string>>(builders, "\n;")) {
        trim(line);
        line.erase(std::find(line.begin(), line.end(), '#'), line.end());
        if (line.empty()) continue;

        if (line[0] == '@') {
            const std::string path = trim(std::string(line, 1));
            std::string text;
            try {
                text = readFile(path);
            } catch (const SysError & e) {
                if (e.errNo != ENOENT)
                    throw;
                debug("cannot find machines file '%s'", path);
            }

            const auto lines = expandBuilderLines(text);
            result.insert(end(result), begin(lines), end(lines));
            continue;
        }

        result.emplace_back(line);
    }
    return result;
}

static Machine parseBuilderLine(const std::string & line, const std::string & thisSystem)
{
    const auto tokens = tokenizeString<std::vector<std::string>>(line);

    auto isSet = [&](size_t fieldIndex) {
        return tokens.size() > fieldIndex && tokens[fieldIndex] != "" && tokens[fieldIndex] != "-";
    };

    auto parseUnsignedIntField = [&](size_t fieldIndex) {
        const auto result = string2Int<unsigned int>(tokens[fieldIndex]);
        if (!result) {
            throw FormatError("bad machine specification: failed to convert column #%lu in a row: '%s' to 'unsigned int'", fieldIndex, line);
        }
        return result.value();
    };

    auto parseFloatField = [&](size_t fieldIndex) {
        const auto result = string2Float<float>(tokens[fieldIndex]);
        if (!result) {
            throw FormatError("bad machine specification: failed to convert column #%lu in a row: '%s' to 'float'", fieldIndex, line);
        }
        return result.value();
    };

    auto ensureBase64 = [&](size_t fieldIndex) {
        const auto & str = tokens[fieldIndex];
        try {
            base64Decode(str);
        } catch (const Error & e) {
            throw FormatError("bad machine specification: a column #%lu in a row: '%s' is not valid base64 string: %s", fieldIndex, line, e.what());
        }
        return str;
    };

    if (!isSet(0))
        throw FormatError("bad machine specification: store URL was not found at the first column of a row: '%s'", line);

    auto storeUri = tokens[0];
    // Backwards compatibility: if the URI is schemeless, is not a path,
    // and is not one of the special store connection words, prepend
    // ssh://.
    storeUri = storeUri.find("://") != std::string::npos || storeUri.find("/") != std::string::npos
            || storeUri == "auto" || storeUri == "daemon" || storeUri == "local"
            || storeUri.starts_with("auto?") || storeUri.starts_with("daemon?")
            || storeUri.starts_with("local?") || storeUri.starts_with("?")
        ? storeUri
        : "ssh://" + storeUri;

    auto systemTypes =
        isSet(1) ? tokenizeString<std::set<std::string>>(tokens[1], ",") : std::set<std::string>{thisSystem};
    auto sshKey = isSet(2) ? tokens[2] : "";
    auto maxJobs = isSet(3) ? parseUnsignedIntField(3) : 1U;
    auto speedFactor = isSet(4) ? parseFloatField(4) : 1.0f;

    auto supportedFeatures =
        isSet(5) ? tokenizeString<std::set<std::string>>(tokens[5], ",") : std::set<std::string>{};
    auto mandatoryFeatures =
        isSet(6) ? tokenizeString<std::set<std::string>>(tokens[6], ",") : std::set<std::string>{};
    auto sshPublicHostKey = isSet(7) ? ensureBase64(7) : "";

    speedFactor = speedFactor == 0.0f ? 1.0f : speedFactor;
    if (speedFactor < 0.0) {
        throw UsageError("speed factor must be >= 0");
    }

    return Machine::new_(
        rust::to_string(storeUri),
        rust::to_string(storeUri),
        rust::to_hash_set(systemTypes),
        rust::to_string(sshKey),
        maxJobs,
        speedFactor,
        rust::to_hash_set(supportedFeatures),
        rust::to_hash_set(mandatoryFeatures),
        rust::to_string(sshPublicHostKey),
        true
    );
}

static Machines parseBuilderLines(const std::vector<std::string> & builders, const std::string & thisSystem)
{
    Machines result = Machines::new_();
    for (auto & builderLine : builders) {
        result.push(parseBuilderLine(builderLine, thisSystem));
    }
    return result;
}

static Machines getMachines(const std::string & builders, const std::string & thisSystem)
{
    const auto builderLines = expandBuilderLines(builders);
    return parseBuilderLines(builderLines, thisSystem);
}

}

Machines getMachines()
{
    auto const builders = settings.builders;
    auto const thisSystem = settings.thisSystem;

    auto machinesResult =
        rust::lix::machines::get_machines(rust::to_string(builders.get()), rust::to_string(thisSystem.get()));

    return match_result(
        std::move(machinesResult),
        [](auto machines) { return machines; },
        [](auto err) -> Machines { throw UsageError(to_std_string(err.to_string())); }
    );
}

}

namespace rust {

namespace exported_functions {
Result<Vec<lix::machines::Machine>, String> parseBuilderLines(Ref<Str> setting, Ref<Str> thisSystem)
{
    using res_t = Result<Vec<lix::machines::Machine>, String>;
    auto builders = to_std_string(setting);
    auto thisSys = to_std_string(thisSystem);

    try {
        return res_t::Ok(nix::machines_legacy_parsing::getMachines(builders, thisSys));
    } catch (nix::FormatError & e) {
        return res_t::Err(to_string(nix::fmt("FormatError: %s", e.what())));
    } catch (nix::UsageError & e) {
        return res_t::Err(to_string(nix::fmt("UsageError: %s", e.what())));
    } catch (::std::exception & e) { // NOLINT(lix-foreign-exceptions)
        return res_t::Err(to_string(nix::fmt("UnknownException: %s", e.what())));
    }
}

}
}
