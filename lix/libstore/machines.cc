#include "lix/libstore/machines.hh"

#include "lix/libstore/globals.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/strings.hh"

#include <numeric>
#include <algorithm>

#include <toml.hpp>

namespace nix {

bool Machine::systemSupported(const std::string & system) const
{
    return system == "builtin" || (systemTypes.count(system) > 0);
}

bool Machine::allSupported(const std::set<std::string> & features) const
{
    return std::all_of(features.begin(), features.end(),
        [&](const std::string & feature) {
            return supportedFeatures.count(feature) ||
                mandatoryFeatures.count(feature);
        });
}

bool Machine::mandatoryMet(const std::set<std::string> & features) const
{
    return std::all_of(mandatoryFeatures.begin(), mandatoryFeatures.end(),
        [&](const std::string & feature) {
            return features.count(feature);
        });
}

kj::Promise<Result<std::pair<ref<Store>, Pipe>>> Machine::openStore() const
try {
    Pipe pipe;

    StoreConfig::Params storeParams;
    if (storeUri.starts_with("ssh://")) {
        // Remote builds become flakey, when having more than one ssh connection
        storeParams["max-connections"] = "1";
    }

    if (storeUri.starts_with("ssh://") || storeUri.starts_with("ssh-ng://")) {
        pipe.create();
        storeParams["log-fd"] = std::to_string(pipe.writeSide.get());
        if (sshKey != "")
            storeParams["ssh-key"] = sshKey;
        if (sshPublicHostKey != "")
            storeParams["base64-ssh-public-host-key"] = sshPublicHostKey;
    }

    {
        auto & fs = storeParams["system-features"];
        auto append = [&](auto feats) {
            for (auto & f : feats) {
                if (fs.size() > 0) fs += ' ';
                fs += f;
            }
        };
        append(supportedFeatures);
        append(mandatoryFeatures);
    }

    co_return {TRY_AWAIT(nix::openStore(storeUri, storeParams)), std::move(pipe)};
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

static Machine parseBuilderLine(const std::string & line)
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

    auto systemTypes = isSet(1) ? tokenizeString<std::set<std::string>>(tokens[1], ",")
                                : std::set<std::string>{settings.thisSystem};
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

    return {
        storeUri,
        systemTypes,
        sshKey,
        maxJobs,
        speedFactor,
        supportedFeatures,
        mandatoryFeatures,
        sshPublicHostKey
    };
}

static Machines parseBuilderLines(const std::vector<std::string> & builders)
{
    Machines result;
    std::transform(builders.begin(), builders.end(), std::back_inserter(result), parseBuilderLine);
    return result;
}

Machines getMachines()
{
    const auto builderLines = expandBuilderLines(settings.builders);
    return parseBuilderLines(builderLines);
}

}

namespace machines_toml_parsing {

static constexpr int MIN_VERSION = 1;
static constexpr int LATEST_VERSION = 1;
// Toml format:
// [[machines.andesite]]
// uri = "..."
//
// [[machines.diorite]]
// ...

template<typename T>
static toml::result<T, std::string> parse(const toml::value & data, const std::string & key)
{
    try {
        return toml::success(toml::get<T>(data.at(key)));
    } catch (toml::type_error & e) { // NOLINT(lix-foreign-exceptions)
        return toml::failure<std::string>({e.what()});
    } catch (std::out_of_range & _) { // NOLINT(lix-foreign-exceptions)
        const auto ei =
            toml::make_error_info(fmt("%s must be present", key), data, "but was not set");
        return toml::failure(toml::format_error(ei));
    }
}

template<typename T>
static toml::result<T, std::string>
parse(const toml::value & data, const std::string & key, T defaultValue)
{
    if (!data.contains(key)) {
        return toml::success(defaultValue);
    }
    try {
        return toml::success(toml::get<T>(data.at(key)));
    } catch (toml::type_error & e) { // NOLINT(lix-foreign-exceptions)
        // invalid value
        return toml::failure<std::string>({e.what()});
    }
}

static const std::set<std::string> EXPECTED_KEYS = {
    "uri",
    "system-types",
    "ssh-key",
    "jobs",
    "speed-factor",
    "supported-features",
    "mandatory-features",
    "ssh-public-host-key",
    "enable",
};

static toml::result<float, std::string> getSpeedFactor(const toml::value & data)
{
    if (data.contains("speed-factor")) {
        auto sf = data.at("speed-factor");
        if (sf.is_integer()) {
            return toml::success(static_cast<float>(sf.as_integer()));
        }
        if (sf.is_floating()) {
            return toml::success(static_cast<float>(sf.as_floating()));
        }
        return toml::failure(toml::format_error(toml::make_error_info(
            "bad_cast to floating for `speed-factor`", sf, "Was neither an integer nor a float"
        )));
    }
    return toml::success(1.0f);
}

static toml::result<Machine, std::vector<std::string>> parseMachine(const toml::value & data)
{
    std::vector<std::string> errs;

    if (!data.is_table()) {
        errs.push_back(toml::format_error(toml::make_error_info(
            "Each machine must be a table", data, "This should be a table. Did you mean `.uri = `?"
        )));
        return toml::failure(errs);
    }

    // parsing
    auto storeUri = parse<std::string>(data, "uri");
    auto systemTypes = parse<std::vector<std::string>>(
        data, "system-types", std::vector<std::string>{settings.thisSystem}
    );
    auto sshKey = parse<std::string>(data, "ssh-key", "");
    auto maxJobs = parse<int>(data, "jobs", 1U);
    auto speedFactor = getSpeedFactor(data);
    auto supportedFeatures =
        parse<std::vector<std::string>>(data, "supported-features", std::vector<std::string>{});
    auto mandatoryFeatures =
        parse<std::vector<std::string>>(data, "mandatory-features", std::vector<std::string>{});
    auto sshPublicHostKey = parse<std::string>(data, "ssh-public-host-key", "");

    // parsing validation
    if (storeUri.is_err()) {
        errs.push_back(storeUri.as_err());
    }
    if (systemTypes.is_err()) {
        errs.push_back(systemTypes.as_err());
    }
    if (sshKey.is_err()) {
        errs.push_back(sshKey.as_err());
    }
    if (maxJobs.is_err()) {
        errs.push_back(maxJobs.as_err());
    }
    if (speedFactor.is_err()) {
        errs.push_back(speedFactor.as_err());
    }
    if (supportedFeatures.is_err()) {
        errs.push_back(supportedFeatures.as_err());
    }
    if (mandatoryFeatures.is_err()) {
        errs.push_back(mandatoryFeatures.as_err());
    }
    if (sshPublicHostKey.is_err()) {
        errs.push_back(sshPublicHostKey.as_err());
    }

    // value validation
    if (maxJobs.is_ok() && maxJobs.as_ok() < 0) {
        auto ei =
            toml::make_error_info("jobs must be >= 0", data.at("jobs"), "but got negative value");
        errs.push_back(toml::format_error(ei));
    }

    if (speedFactor.is_ok() && speedFactor.as_ok() < 0.0) {
        auto ei = toml::make_error_info(
            "speed factor must be >= 0", data.at("speed-factor"), "but got negative value"
        );
        errs.push_back(toml::format_error(ei));
    }

    for (const auto & [key, _] : data.as_table()) {
        if (!EXPECTED_KEYS.contains(key)) {
            errs.push_back(toml::format_error(toml::make_error_info(
                fmt("unexpected key `%s`", key), data.at(key), "should not be present"
            )));
        }
    }

    if (!errs.empty()) {
        return toml::failure(errs);
    }
    return toml::success<Machine>({
        storeUri.unwrap(),
        std::set(systemTypes.unwrap().begin(), systemTypes.unwrap().end()),
        sshKey.unwrap(),
        static_cast<unsigned>(maxJobs.unwrap()),
        speedFactor.unwrap(),
        std::set(supportedFeatures.unwrap().begin(), supportedFeatures.unwrap().end()),
        std::set(mandatoryFeatures.unwrap().begin(), mandatoryFeatures.unwrap().end()),
        base64Encode(sshPublicHostKey.unwrap()),
    });
}

static toml::result<Machines, std::vector<std::string>> parseToml(const toml::value & data)
{
    auto const array_name = "machines";
    std::vector<std::string> parserErrors;
    Machines machines;
    // Empty config
    if (data.size() == 0) {
        return toml::success<Machines>({});
    }
    if (!data.is_table()) {
        parserErrors.push_back(
            "Top level must be a table. This should never throw as this is required by the toml "
            "SPEC"
        );
        return toml::failure(parserErrors);
    }

    if (auto config_version = parse<int>(data, "version", LATEST_VERSION); config_version.is_err())
    {
        parserErrors.push_back(config_version.as_err());
    } else if (config_version.as_ok() < MIN_VERSION || config_version.as_ok() > LATEST_VERSION) {
        parserErrors.push_back(
            fmt("Unable to parse Machines of version %d, only versions between %d and %d are "
                "supported.",
                config_version.as_ok(),
                MIN_VERSION,
                LATEST_VERSION)
        );
    }
    if (!parserErrors.empty()) {
        return toml::failure(parserErrors);
    }

    auto & tbl = data.as_table();
    std::string unexpected_keys;
    for (auto it = tbl.begin(); it != tbl.end(); ++it) {
        if (it->first == array_name || it->first == "version") {
            // expected keys
            continue;
        }
        unexpected_keys += ", " + it->first;
    }
    if (unexpected_keys.size()) {
        parserErrors.push_back(fmt("unexpected keys found: %s", unexpected_keys.erase(0, 2)));
    }

    if (!data.at(array_name).is_table()) {
        parserErrors.push_back(
            fmt("Expected key `%s` to be a table of name -> machine configurations", array_name)
        );
        return toml::failure(parserErrors);
    }

    for (const auto & [name, machine] : data.at(array_name).as_table()) {
        auto const res = parseMachine(machine);
        if (res.is_err()) {
            auto err = res.as_err();
            parserErrors.push_back(fmt("for machine %s:", name));
            parserErrors.insert(parserErrors.end(), err.begin(), err.end());
            continue;
        }
        auto enable = parse<bool>(machine, "enable", true);
        if (enable.is_ok()) {
            if (enable.unwrap()) {
                // Check if it hasn't been statically disabled
                // But still throw parsing errors if it was
                machines.push_back(res.unwrap());
            }
        } else {
            parserErrors.push_back(enable.as_err());
        }
    }

    if (!parserErrors.empty()) {
        return toml::failure(parserErrors);
    }
    return toml::success(machines);
}

static std::optional<Machines> getMachines()
{
    toml::value data;
    auto buildersStr = settings.builders.get();
    try {
        if (buildersStr.size() > 0 && buildersStr.at(0) == '@') {
            data = toml::parse(buildersStr.substr(1));
        } else {
            data = toml::parse_str(settings.builders);
        }
    } catch (toml::syntax_error const & e) { // NOLINT(lix-foreign-exceptions)
        if (toLower(buildersStr).contains("toml") || buildersStr.contains("\"")) {
            // Yes, we are sure this is a TOML and no this shitty legacy format
            // so we can safely throw the syntax error here
            throw UsageError(fmt("invalid Machines TOML syntax: \n%s", e.what()));
        }
        return {};
    } catch (toml::file_io_error const & _) { // NOLINT(lix-foreign-exceptions)
        // sadly we have to do this otherwise we break the old format,
        // which requires **silently ignoring** invalid files
        return {};
    }
    auto const fromToml = parseToml(data);
    if (fromToml.is_ok()) {
        return fromToml.unwrap();
    }

    auto const & errs = fromToml.as_err();
    std::string msg = "invalid Machines TOML:\n";
    msg += concatStringsSep("\n", errs);

    throw UsageError(msg);
}

}

Machines getMachines()
{
    auto const toml_result = machines_toml_parsing::getMachines();
    if (toml_result.has_value()) {
        return toml_result.value();
    }
    debug("Trying again with legacy format");
    return machines_legacy_parsing::getMachines();
}

}
