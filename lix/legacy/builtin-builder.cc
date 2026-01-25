#include "lix/libcmd/legacy.hh"
#include "lix/libstore/builtins.hh"
#include "lix/libstore/builtins/buildenv.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/types.hh"
#include <string_view>

using std::literals::operator""sv;

namespace nix {

static int main_builtin_builder(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    logger = makeJSONLogger(*logger);

    std::map<std::string, std::string> env;

    auto argvIt = argv.begin();
    const auto argvEnd = argv.end();

    // we do not use the argument parsing functions we have in libmain here, neither
    // the legacy versions nor the newer ones. the legacy version could work, but we
    // want to provide two sets of arguments separated by `--` and would need rather
    // unpleasant state handling to use the legacy parser. the more modern parser is
    // entirely incapable of doing this for us since it's all statically configured.
    const auto getArg = [&](std::string_view desc) {
        if (argvIt == argvEnd) {
            throw Error("expected a value for %s", desc);
        }
        return *argvIt++;
    };

    const auto unescapeNul = [](std::string in) {
        return replaceStrings(replaceStrings(std::move(in), "\\0", "\0"sv), "\\\\", "\\");
    };

    while (argvIt != argvEnd) {
        const auto arg = getArg("option");
        if (arg == "--") {
            break;
        } else if (!arg.starts_with("--")) {
            throw Error("unexpected builtin option %s", arg);
        }
        settings.set(arg.substr(2), unescapeNul(getArg(arg)));
    }

    while (argvIt != argvEnd) {
        const auto key = getArg("builder argument");
        if (!key.starts_with("--")) {
            throw Error("unexpected builtin builder argument %s", key);
        }
        env[unescapeNul(key.substr(2))] = unescapeNul(getArg(key));
    }

    auto getAttr = [&](const std::string & name) {
        auto i = env.find(name);
        if (i == env.end()) {
            throw Error("attribute '%s' missing", name);
        }
        return i->second;
    };

    const auto builder = getAttr("builder");

    throw Error("unknown builtin builder %s", builder);
}

void registerLegacyBuiltinBuilder()
{
    LegacyCommandRegistry::add("builtin-builder", main_builtin_builder);
}

}
