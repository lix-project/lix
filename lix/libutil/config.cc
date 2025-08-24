#include "lix/libutil/config.hh"
#include "lix/libutil/apply-config-options.hh"
#include "lix/libutil/args.hh"
#include "lix/libutil/abstract-setting-to-json.hh"
#include "lix/libutil/experimental-features.hh"
#include "lix/libutil/deprecated-features.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/strings.hh"

#include "lix/libutil/config-impl.hh"
#include <mutex>

namespace nix {

Config::Config(StringMap initials)
    : AbstractConfig(std::move(initials))
{ }

bool Config::set(const std::string & name, const std::string & value, const ApplyConfigOptions & options)
{
    bool append = false;
    auto i = _settings.find(name);
    if (i == _settings.end()) {
        if (name.starts_with("extra-")) {
            i = _settings.find(std::string(name, 6));
            if (i == _settings.end() || !i->second.setting->isAppendable())
                return false;
            append = true;
        } else
            return false;
    }
    i->second.setting->set(value, append, options);
    return true;
}

void Config::addSetting(AbstractSetting * setting)
{
    _settings.emplace(setting->name, Config::SettingData{false, setting});
    for (const auto & alias : setting->aliases)
        _settings.emplace(alias, Config::SettingData{true, setting});

    bool set = false;

    if (auto i = unknownSettings.find(setting->name); i != unknownSettings.end()) {
        setting->set(std::move(i->second));
        unknownSettings.erase(i);
        set = true;
    }

    for (auto & alias : setting->aliases) {
        if (auto i = unknownSettings.find(alias); i != unknownSettings.end()) {
            if (set)
                printTaggedWarning(
                    "setting '%s' is set, but it's an alias of '%s' which is also set",
                    alias,
                    setting->name
                );
            else {
                setting->set(std::move(i->second));
                unknownSettings.erase(i);
                set = true;
            }
        }
    }
}

AbstractConfig::AbstractConfig(StringMap initials)
    : unknownSettings(std::move(initials))
{ }

void AbstractConfig::warnUnknownSettings()
{
    for (const auto & s : unknownSettings)
        printTaggedWarning("unknown setting '%s'", s.first);
}

void AbstractConfig::reapplyUnknownSettings()
{
    auto unknownSettings2 = std::move(unknownSettings);
    unknownSettings = {};
        for (auto & s : unknownSettings2)
        set(s.first, s.second);
}

void Config::getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly)
{
    for (const auto & opt : _settings)
        if (!opt.second.isAlias && (!overriddenOnly || opt.second.setting->overridden))
            res.emplace(opt.first, SettingInfo{opt.second.setting->to_string(), opt.second.setting->description});
}


static void applyConfigInner(const std::string & contents, const ApplyConfigOptions & options, std::vector<std::pair<std::string, std::string>> & parsedContents) {
    unsigned int pos = 0;

    while (pos < contents.size()) {
        std::string line;
        while (pos < contents.size() && contents[pos] != '\n')
            line += contents[pos++];
        pos++;

        if (auto hash = line.find('#'); hash != line.npos)
            line = std::string(line, 0, hash);

        auto tokens = tokenizeString<std::vector<std::string>>(line);
        if (tokens.empty()) continue;

        if (tokens.size() < 2)
            throw UsageError("illegal configuration line '%1%' in '%2%'", line, options.relativeDisplay());

        auto include = false;
        auto ignoreMissing = false;
        if (tokens[0] == "include")
            include = true;
        else if (tokens[0] == "!include") {
            include = true;
            ignoreMissing = true;
        }

        if (include) {
            if (tokens.size() != 2) {
                throw UsageError("illegal configuration line '%1%' in '%2%'", line, options.relativeDisplay());
            }
            if (!options.path) {
                throw UsageError("can only include configuration '%1%' from files", tokens[1]);
            }
            auto pathToInclude = absPath(tildePath(tokens[1], options.home), dirOf(*options.path));
            if (pathExists(pathToInclude)) {
                auto includeOptions = ApplyConfigOptions {
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
                throw Error("file '%1%' included from '%2%' not found", pathToInclude, *options.path);
            }
            continue;
        }

        if (tokens[1] != "=")
            throw UsageError("illegal configuration line '%1%' in '%2%'", line, options.relativeDisplay());

        std::string name = std::move(tokens[0]);

        auto i = tokens.begin();
        advance(i, 2);

        parsedContents.push_back({
            std::move(name),
            concatStringsSep(" ", Strings(i, tokens.end())),
        });
    };
}

void AbstractConfig::applyConfig(const std::string & contents, const ApplyConfigOptions & options) {
    std::vector<std::pair<std::string, std::string>> parsedContents;

    applyConfigInner(contents, options, parsedContents);

    // First apply experimental-feature related settings
    for (const auto & [name, value] : parsedContents)
        if (name == "experimental-features" || name == "extra-experimental-features")
            set(name, value, options);

    // Then apply other settings
    for (const auto & [name, value] : parsedContents)
        if (name != "experimental-features" && name != "extra-experimental-features")
            set(name, value, options);
}

void Config::resetOverridden()
{
    for (auto & s : _settings)
        s.second.setting->overridden = false;
}

JSON Config::toJSON()
{
    auto res = JSON::object();
    for (const auto & s : _settings)
        if (!s.second.isAlias)
            res.emplace(s.first, s.second.setting->toJSON());
    return res;
}

void Config::convertToArgs(Args & args, const std::string & category)
{
    for (auto & s : _settings) {
        if (!s.second.isAlias)
            s.second.setting->convertToArg(args, category);
    }
}

AbstractSetting::AbstractSetting(
    const std::string & name,
    const std::string & description,
    const std::set<std::string> & aliases,
    std::optional<ExperimentalFeature> experimentalFeature)
    : name(name)
    , description(stripIndentation(description))
    , aliases(aliases)
    , experimentalFeature(std::move(experimentalFeature))
{
}

AbstractSetting::~AbstractSetting()
{
    // Check against a gcc miscompilation causing our constructor
    // not to run (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80431).
    assert(created == 123);
}

JSON AbstractSetting::toJSON()
{
    return JSON(toJSONObject());
}

std::map<std::string, JSON> AbstractSetting::toJSONObject() const
{
    std::map<std::string, JSON> obj;
    obj.emplace("description", description);
    obj.emplace("aliases", aliases);
    if (experimentalFeature)
        obj.emplace("experimentalFeature", *experimentalFeature);
    else
        obj.emplace("experimentalFeature", nullptr);
    return obj;
}

void AbstractSetting::convertToArg(Args & args, const std::string & category)
{
}


bool AbstractSetting::isOverridden() const { return overridden; }

template<> std::string BaseSetting<std::string>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    return str;
}

template<> std::string BaseSetting<std::string>::to_string() const
{
    return value;
}

template<> std::optional<std::string> BaseSetting<std::optional<std::string>>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    if (str == "")
        return std::nullopt;
    else
        return { str };
}

template<> std::string BaseSetting<std::optional<std::string>>::to_string() const
{
    return value ? *value : "";
}

template<> std::optional<uint16_t> BaseSetting<std::optional<uint16_t>>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    if (str == "")
        return std::nullopt;
    else if (auto n = string2Int<uint16_t>(str))
        return n;
    else
        throw UsageError("setting '%s' has invalid value '%s'", name, str);
}

template<> std::string BaseSetting<std::optional<uint16_t>>::to_string() const
{
    return value ? std::to_string(*value) : "";
}

template<> bool BaseSetting<bool>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    if (str == "true" || str == "yes" || str == "1")
        return true;
    else if (str == "false" || str == "no" || str == "0")
        return false;
    else
        throw UsageError("Boolean setting '%s' has invalid value '%s'", name, str);
}

template<> std::string BaseSetting<bool>::to_string() const
{
    return value ? "true" : "false";
}

template<> void BaseSetting<bool>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .description = fmt("Enable the `%s` setting.", name),
        .category = category,
        .handler = {[this] { override(true); }},
        .experimentalFeature = experimentalFeature,
    });
    args.addFlag({
        .longName = "no-" + name,
        .description = fmt("Disable the `%s` setting.", name),
        .category = category,
        .handler = {[this] { override(false); }},
        .experimentalFeature = experimentalFeature,
    });
}

template<> Strings BaseSetting<Strings>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    return tokenizeString<Strings>(str);
}

template<> void BaseSetting<Strings>::appendOrSet(Strings newValue, bool append, const ApplyConfigOptions & options)
{
    if (!append) value.clear();
    value.insert(value.end(), std::make_move_iterator(newValue.begin()),
                              std::make_move_iterator(newValue.end()));
}

template<> std::string BaseSetting<Strings>::to_string() const
{
    return concatStringsSep(" ", value);
}

template<> StringSet BaseSetting<StringSet>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    return tokenizeString<StringSet>(str);
}

template<> void BaseSetting<StringSet>::appendOrSet(StringSet newValue, bool append, const ApplyConfigOptions & options)
{
    if (!append) value.clear();
    value.insert(std::make_move_iterator(newValue.begin()), std::make_move_iterator(newValue.end()));
}

template<> std::string BaseSetting<StringSet>::to_string() const
{
    return concatStringsSep(" ", value);
}

template<> ExperimentalFeatures BaseSetting<ExperimentalFeatures>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    ExperimentalFeatures res{};
    for (auto & s : tokenizeString<StringSet>(str)) {
        if (auto thisXpFeature = parseExperimentalFeature(s); thisXpFeature) {
            res = res | thisXpFeature.value();
        } else
            printTaggedWarning("unknown experimental feature '%s'", s);
    }
    return res;
}

template<> void BaseSetting<ExperimentalFeatures>::appendOrSet(ExperimentalFeatures newValue, bool append, const ApplyConfigOptions & options)
{
    if (append)
        value = value | newValue;
    else
        value = newValue;
}

template<> std::string BaseSetting<ExperimentalFeatures>::to_string() const
{
    StringSet stringifiedXpFeatures;
    for (size_t tag = 0; tag < sizeof(ExperimentalFeatures) * CHAR_BIT; tag++)
        if ((value & ExperimentalFeature(tag)) != ExperimentalFeatures{})
            stringifiedXpFeatures.insert(std::string(showExperimentalFeature(ExperimentalFeature(tag))));
    return concatStringsSep(" ", stringifiedXpFeatures);
}

template<> DeprecatedFeatures BaseSetting<DeprecatedFeatures>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    DeprecatedFeatures res{};
    for (auto & s : tokenizeString<StringSet>(str)) {
        if (auto thisDpFeature = parseDeprecatedFeature(s); thisDpFeature)
            res = res | thisDpFeature.value();
        else
            printTaggedWarning("unknown deprecated feature '%s'", s);
    }
    return res;
}

template<> void BaseSetting<DeprecatedFeatures>::appendOrSet(DeprecatedFeatures newValue, bool append, const ApplyConfigOptions & options)
{
    if (append)
        value = value | newValue;
    else
        value = newValue;
}

template<> std::string BaseSetting<DeprecatedFeatures>::to_string() const
{
    StringSet stringifiedDpFeatures;
    for (size_t tag = 0; tag < sizeof(DeprecatedFeatures) * CHAR_BIT; tag++)
        if ((value & DeprecatedFeature(tag)) != DeprecatedFeatures{})
            stringifiedDpFeatures.insert(std::string(showDeprecatedFeature(DeprecatedFeature(tag))));
    return concatStringsSep(" ", stringifiedDpFeatures);
}

template<> StringMap BaseSetting<StringMap>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    StringMap res;
    for (const auto & s : tokenizeString<Strings>(str)) {
        if (auto eq = s.find_first_of('='); s.npos != eq)
            res.emplace(std::string(s, 0, eq), std::string(s, eq + 1));
        // else ignored
    }
    return res;
}

template<> void BaseSetting<StringMap>::appendOrSet(StringMap newValue, bool append, const ApplyConfigOptions & options)
{
    if (!append) value.clear();
    value.insert(std::make_move_iterator(newValue.begin()), std::make_move_iterator(newValue.end()));
}

template<> std::string BaseSetting<StringMap>::to_string() const
{
    return std::transform_reduce(value.cbegin(), value.cend(), std::string{},
        [](const auto & l, const auto  &r) { return l + " " + r; },
        [](const auto & kvpair){ return kvpair.first + "=" + kvpair.second; });
}

template class BaseSetting<int>;
template class BaseSetting<unsigned int>;
template class BaseSetting<long>;
template class BaseSetting<unsigned long>;
template class BaseSetting<long long>;
template class BaseSetting<unsigned long long>;
template class BaseSetting<bool>;
template class BaseSetting<std::string>;
template class BaseSetting<Strings>;
template class BaseSetting<StringSet>;
template class BaseSetting<StringMap>;
template class BaseSetting<ExperimentalFeatures>;
template class BaseSetting<DeprecatedFeatures>;

static Path parsePath(const AbstractSetting & s, const std::string & str, const ApplyConfigOptions & options)
{
    if (str == "") {
        throw UsageError("setting '%s' is a path and paths cannot be empty", s.name);
    } else {
        auto tildeResolvedPath = tildePath(str, options.home);
        if (options.path) {
            return absPath(tildeResolvedPath, dirOf(*options.path));
        } else {
            return canonPath(tildeResolvedPath);
        }
    }
}

template<> Path PathsSetting<Path>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    return parsePath(*this, str, options);
}

template<> std::optional<Path> PathsSetting<std::optional<Path>>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    if (str == "")
        return std::nullopt;
    else
        return parsePath(*this, str, options);
}

template<> Paths PathsSetting<Paths>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    auto strings = tokenizeString<Strings>(str);
    Paths parsed;

    for (auto str : strings) {
        parsed.push_back(parsePath(*this, str, options));
    }

    return parsed;
}

template<> PathSet PathsSetting<PathSet>::parse(const std::string &str, const ApplyConfigOptions & options) const
{
    auto strings = tokenizeString<Strings>(str);
    PathSet parsed;

    for (auto str : strings) {
        parsed.insert(parsePath(*this, str, options));
    }

    return parsed;
}

template class PathsSetting<Path>;
template class PathsSetting<std::optional<Path>>;
template class PathsSetting<Paths>;
template class PathsSetting<PathSet>;


bool GlobalConfig::set(const std::string & name, const std::string & value, const ApplyConfigOptions & options)
{
    for (auto & config : *configRegistrations)
        if (config->set(name, value, options)) return true;

    unknownSettings.emplace(name, value);

    return false;
}

void GlobalConfig::getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly)
{
    for (auto & config : *configRegistrations)
        config->getSettings(res, overriddenOnly);
}

void GlobalConfig::resetOverridden()
{
    for (auto & config : *configRegistrations)
        config->resetOverridden();
}

JSON GlobalConfig::toJSON()
{
    auto res = JSON::object();
    for (const auto & config : *configRegistrations)
        res.update(config->toJSON());
    return res;
}

std::string GlobalConfig::toKeyValue(bool overriddenOnly)
{
    std::string res;
    std::map<std::string, Config::SettingInfo> settings;
    globalConfig.getSettings(settings, overriddenOnly);
    for (const auto & s : settings)
        res += fmt("%s = %s\n", s.first, s.second.value);
    return res;
}

void GlobalConfig::convertToArgs(Args & args, const std::string & category)
{
    for (auto & config : *configRegistrations)
        config->convertToArgs(args, category);
}

GlobalConfig globalConfig;

GlobalConfig::ConfigRegistrations * GlobalConfig::configRegistrations;

GlobalConfig::Register::Register(Config * config)
{
    if (!configRegistrations)
        configRegistrations = new ConfigRegistrations;
    configRegistrations->emplace_back(config);
}

FeatureSettings experimentalFeatureSettings;

FeatureSettings& featureSettings = experimentalFeatureSettings;

static GlobalConfig::Register rSettings(&experimentalFeatureSettings);

bool FeatureSettings::isEnabled(const ExperimentalFeature & feature) const
{
    auto & f = experimentalFeatures.get();
    return (f & feature) != ExperimentalFeatures{};
}

void FeatureSettings::require(const ExperimentalFeature & feature) const
{
    if (!isEnabled(feature))
        throw MissingExperimentalFeature(feature);
}

bool FeatureSettings::isEnabled(const std::optional<ExperimentalFeature> & feature) const
{
    return !feature || isEnabled(*feature);
}

void FeatureSettings::require(const std::optional<ExperimentalFeature> & feature) const
{
    if (feature) require(*feature);
}

bool FeatureSettings::isEnabled(const DeprecatedFeature & feature) const
{
    auto & f = deprecatedFeatures.get();
    return (f & feature) != DeprecatedFeatures{};
}

void FeatureSettings::require(const DeprecatedFeature & feature) const
{
    if (!isEnabled(feature))
        throw MissingDeprecatedFeature(feature);
}

bool FeatureSettings::isEnabled(const std::optional<DeprecatedFeature> & feature) const
{
    return !feature || isEnabled(*feature);
}

void FeatureSettings::require(const std::optional<DeprecatedFeature> & feature) const
{
    if (feature) require(*feature);
}

}
