#include "lix/libutil/args.hh"
#include "lix/libutil/args/root.hh"
#include "lix/libutil/hash.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/json.hh" // IWYU pragma: keep (instances)
#include "lix/libutil/environment-variables.hh"

#include "lix/libutil/logging.hh"

#include <glob.h>

namespace nix {

void Args::addFlag(Flag && flag_)
{
    auto flag = std::make_shared<Flag>(std::move(flag_));
    if (flag->handler.arity != ArityAny)
        assert(flag->handler.arity == flag->labels.size());
    assert(flag->longName != "");
    longFlags[flag->longName] = flag;
    for (auto & alias : flag->aliases)
        longFlags[alias] = flag;
    if (flag->shortName) shortFlags[flag->shortName] = flag;
}

void Args::removeFlag(const std::string & longName)
{
    auto flag = longFlags.find(longName);
    assert(flag != longFlags.end());
    if (flag->second->shortName) shortFlags.erase(flag->second->shortName);
    longFlags.erase(flag);
}

void Completions::setType(AddCompletions::Type t)
{
    type = t;
}

void Completions::add(std::string completion, std::string description)
{
    description = trim(description);
    // ellipsize overflowing content on the back of the description
    auto end_index = description.find_first_of(".\n");
    if (end_index != std::string::npos) {
        auto needs_ellipsis = end_index != description.size() - 1;
        description.resize(end_index);
        if (needs_ellipsis)
            description.append(" [...]");
    }
    completions.insert(Completion {
        .completion = completion,
        .description = description
    });
}

bool Completion::operator<(const Completion & other) const
{ return completion < other.completion || (completion == other.completion && description < other.description); }

std::string completionMarker = "___COMPLETE___";

RootArgs & Args::getRoot()
{
    Args * p = this;
    while (p->parent)
        p = p->parent;

    auto res = p->asRootArgs();
    assert(res);
    return *res;
}

std::optional<std::string> RootArgs::needsCompletion(std::string_view s)
{
    if (!completions) return {};
    auto i = s.find(completionMarker);
    if (i != std::string::npos)
        return std::string(s.begin(), i);
    return {};
}

void RootArgs::parseCmdline(const Strings & _cmdline)
{
    Strings pendingArgs;
    bool dashDash = false;

    Strings cmdline(_cmdline);

    if (auto s = getEnv("NIX_GET_COMPLETIONS")) {
        size_t n = [&] {
            if (auto parsed = string2Int<size_t>(*s)) {
                return *parsed;
            }
            throw UsageError("Invalid value for environment variable NIX_GET_COMPLETIONS: %s", *s);
        }();

        if (!(n > 0 && n <= cmdline.size()))
            throw UsageError("Invalid word number to get completion for: %zu\n. Your autocompletions might be misconfigured", n);
        *std::next(cmdline.begin(), n - 1) += completionMarker;
        completions = std::make_shared<Completions>();
        verbosity = lvlError;
    }

    for (auto pos = cmdline.begin(); pos != cmdline.end(); ) {

        auto arg = *pos;

        /* Expand compound dash options (i.e., `-qlf' -> `-q -l -f',
           `-j3` -> `-j 3`). */
        if (!dashDash && arg.length() > 2 && arg[0] == '-' && arg[1] != '-' && isalpha(arg[1])) {
            *pos = (std::string) "-" + arg[1];
            auto next = pos; ++next;
            for (unsigned int j = 2; j < arg.length(); j++)
                if (isalpha(arg[j]))
                    cmdline.insert(next, (std::string) "-" + arg[j]);
                else {
                    cmdline.insert(next, std::string(arg, j));
                    break;
                }
            arg = *pos;
        }

        if (!dashDash && arg == "--") {
            dashDash = true;
            ++pos;
        }
        else if (!dashDash && std::string(arg, 0, 1) == "-") {
            if (!processFlag(pos, cmdline.end()))
                throw UsageError("unrecognised flag '%1%'", arg);
        }
        else {
            pos = rewriteArgs(cmdline, pos);
            pendingArgs.push_back(*pos++);
            if (processArgs(pendingArgs, false))
                pendingArgs.clear();
        }
    }

    processArgs(pendingArgs, true);

    initialFlagsProcessed();

    /* Now that we are done parsing, make sure that any experimental
     * feature required by the flags is enabled */
    for (auto & f : flagExperimentalFeatures)
        experimentalFeatureSettings.require(f);

    /* Now that all the other args are processed, run the deferred completions.
     */
    for (auto d : deferredCompletions)
        d.completer(*completions, d.n, d.prefix);
}

bool Args::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    assert(pos != end);

    auto & rootArgs = getRoot();

    auto process = [&](const std::string & name, const Flag & flag) -> bool {
        ++pos;

        if (auto & f = flag.experimentalFeature)
            rootArgs.flagExperimentalFeatures.insert(*f);

        std::vector<std::string> args;
        bool anyCompleted = false;
        for (size_t n = 0 ; n < flag.handler.arity; ++n) {
            if (pos == end) {
                if (flag.handler.arity == ArityAny || anyCompleted) break;
                throw UsageError(
                    "flag '%s' requires %d argument(s), but only %d were given",
                    name, flag.handler.arity, n);
            }
            if (auto prefix = rootArgs.needsCompletion(*pos)) {
                anyCompleted = true;
                if (flag.completer) {
                    rootArgs.deferredCompletions.push_back({
                        .completer = flag.completer,
                        .n = n,
                        .prefix = *prefix,
                    });
                }
            }
            args.push_back(*pos++);
        }
        if (!anyCompleted)
            flag.handler.fun(std::move(args));
        return true;
    };

    if (std::string(*pos, 0, 2) == "--") {
        if (auto prefix = rootArgs.needsCompletion(*pos)) {
            for (auto & [name, flag] : longFlags) {
                if (!hiddenCategories.count(flag->category)
                    && name.starts_with(std::string(*prefix, 2)))
                {
                    if (auto & f = flag->experimentalFeature)
                        rootArgs.flagExperimentalFeatures.insert(*f);
                    rootArgs.completions->add("--" + name, flag->description);
                }
            }
            return false;
        }
        auto i = longFlags.find(std::string(*pos, 2));
        if (i == longFlags.end()) return false;
        return process("--" + i->first, *i->second);
    }

    if (std::string(*pos, 0, 1) == "-" && pos->size() == 2) {
        auto c = (*pos)[1];
        auto i = shortFlags.find(c);
        if (i == shortFlags.end()) return false;
        return process(std::string("-") + c, *i->second);
    }

    if (auto prefix = rootArgs.needsCompletion(*pos)) {
        if (prefix == "-") {
            rootArgs.completions->add("--");
            for (auto & [flagName, flag] : shortFlags)
                if (experimentalFeatureSettings.isEnabled(flag->experimentalFeature))
                    rootArgs.completions->add(std::string("-") + flagName, flag->description);
        }
    }

    return false;
}

bool Args::processArgs(const Strings & args, bool finish)
{
    if (expectedArgs.empty()) {
        if (!args.empty())
            throw UsageError("unexpected argument '%1%'", args.front());
        return true;
    }

    auto & rootArgs = getRoot();

    auto & exp = expectedArgs.front();

    bool res = false;

    if ((exp.handler.arity == ArityAny && finish) ||
        (exp.handler.arity != ArityAny && args.size() == exp.handler.arity))
    {
        std::vector<std::string> ss;
        bool anyCompleted = false;
        for (const auto &[n, s] : enumerate(args)) {
            if (auto prefix = rootArgs.needsCompletion(s)) {
                anyCompleted = true;
                ss.push_back(*prefix);
                if (exp.completer) {
                    rootArgs.deferredCompletions.push_back({
                        .completer = exp.completer,
                        .n = n,
                        .prefix = *prefix,
                    });
                }
            } else
                ss.push_back(s);
        }
        if (!anyCompleted)
            exp.handler.fun(ss);

        /* Move the list element to the processedArgs. This is almost the same as
           `processedArgs.push_back(expectedArgs.front()); expectedArgs.pop_front()`,
           except that it will only adjust the next and prev pointers of the list
           elements, meaning the actual contents don't move in memory. This is
           critical to prevent invalidating internal pointers! */
        processedArgs.splice(
            processedArgs.end(),
            expectedArgs,
            expectedArgs.begin(),
            ++expectedArgs.begin());

        res = true;
    }

    if (finish && !expectedArgs.empty() && !expectedArgs.front().optional)
        throw UsageError("more arguments are required");

    return res;
}

JSON Args::toJSON()
{
    auto flags = JSON::object();

    for (auto & [name, flag] : longFlags) {
        auto j = JSON::object();
        if (hiddenCategories.count(flag->category)) continue;
        if (flag->aliases.count(name)) continue;
        if (flag->shortName)
            j["shortName"] = std::string(1, flag->shortName);
        if (flag->description != "")
            j["description"] = trim(flag->description);
        j["category"] = flag->category;
        if (flag->handler.arity != ArityAny)
            j["arity"] = flag->handler.arity;
        if (!flag->labels.empty())
            j["labels"] = flag->labels;
        j["experimental-feature"] = flag->experimentalFeature;
        j["hidden"] = flag->hidden;
        flags[name] = std::move(j);
    }

    auto args = JSON::array();

    for (auto & arg : expectedArgs) {
        auto j = JSON::object();
        j["label"] = arg.label;
        j["optional"] = arg.optional;
        if (arg.handler.arity != ArityAny)
            j["arity"] = arg.handler.arity;
        args.push_back(std::move(j));
    }

    auto res = JSON::object();
    res["description"] = trim(description());
    res["flags"] = std::move(flags);
    res["args"] = std::move(args);
    auto s = doc();
    if (s != "") res.emplace("doc", stripIndentation(s));
    return res;
}

static void hashTypeCompleter(AddCompletions & completions, size_t index, std::string_view prefix)
{
    for (auto & type : hashTypes)
        if (type.starts_with(prefix))
            completions.add(type);
}

Args::Flag Args::Flag::mkHashTypeFlag(std::string && longName, HashType * ht)
{
    return Flag {
        .longName = std::move(longName),
        .description = "hash algorithm ('md5', 'sha1', 'sha256', or 'sha512')",
        .labels = {"hash-algo"},
        .handler = {[ht](std::string s) {
            *ht = parseHashType(s);
        }},
        .completer = hashTypeCompleter,
    };
}

Args::Flag Args::Flag::mkHashTypeOptFlag(std::string && longName, std::optional<HashType> * oht)
{
    return Flag {
        .longName = std::move(longName),
        .description = "hash algorithm ('md5', 'sha1', 'sha256', or 'sha512'). Optional as can also be gotten from SRI hash itself.",
        .labels = {"hash-algo"},
        .handler = {[oht](std::string s) {
            *oht = std::optional<HashType> { parseHashType(s) };
        }},
        .completer = hashTypeCompleter,
    };
}

static void _completePath(AddCompletions & completions, std::string_view prefix, bool onlyDirs)
{
    completions.setType(Completions::Type::Filenames);
    glob_t globbuf;
    int flags = GLOB_NOESCAPE;
    #ifdef GLOB_ONLYDIR
    if (onlyDirs)
        flags |= GLOB_ONLYDIR;
    #endif
    // using expandTilde here instead of GLOB_TILDE(_CHECK) so that ~<Tab> expands to /home/user/
    if (glob((expandTilde(prefix) + "*").c_str(), flags, nullptr, &globbuf) == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
            if (onlyDirs) {
                auto st = stat(globbuf.gl_pathv[i]);
                if (!S_ISDIR(st.st_mode)) continue;
            }
            completions.add(globbuf.gl_pathv[i]);
        }
    }
    globfree(&globbuf);
}

void Args::completePath(AddCompletions & completions, size_t, std::string_view prefix)
{
    _completePath(completions, prefix, false);
}

void Args::completeDir(AddCompletions & completions, size_t, std::string_view prefix)
{
    _completePath(completions, prefix, true);
}

static bool isAcceptableLixSubcommandExe(const std::filesystem::path & exe_path)
{
    namespace fs = std::filesystem;

    return fs::is_regular_file(exe_path) &&
        (fs::status(exe_path).permissions() & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
}

std::optional<ref<Command>> searchForCustomSubcommand(AsyncIoRoot & aio, const std::string_view & command, const std::string_view & prefix, const Strings & searchPaths)
{
    namespace fs = std::filesystem;
    for (auto searchPath : searchPaths) {
        if (searchPath.empty()) continue;

        auto path = fs::path(searchPath) / fs::path(prefix).concat(command);

        try {
            if (isAcceptableLixSubcommandExe(path)) {
                debug("Found requested external subcommand '%s' in '%s'", command, path);
                return make_ref<ExternalCommand>(aio, path);
            }
        } catch (fs::filesystem_error & fs_exc) { // NOLINT(lix-foreign-exceptions)
            if (fs_exc.code() != std::errc::no_such_file_or_directory && fs_exc.code() != std::errc::not_a_directory && fs_exc.code() != std::errc::permission_denied) {
                throw SysError("while searching for the subcommand '%1%' in search path '%2%': '%3%'", command, searchPath, fs_exc.what());
            }
        }
    }

    return std::nullopt;
}

Strings searchForAllAvailableCustomSubcommands(const std::string_view & prefix, const Strings & searchPaths)
{
    namespace fs = std::filesystem;
    Strings commandNames;

    for (auto searchPath : searchPaths) {
        if (searchPath.empty()) continue;

        if (!fs::exists(searchPath) || !fs::is_directory(searchPath)) {
            // TODO(Raito): this will break all the time our functional tests
            // for people with garbage in their $PATH which is my personal case.

            // warn("The search path '%s' for custom subcommands does not exist or is not a directory, ignoring...", searchPath);
            continue;
        }

        // Browse per prefix.
        for (const auto& entry : fs::directory_iterator(searchPath)) {
            try {
                if (isAcceptableLixSubcommandExe(entry.path())) {
                    auto filename = entry.path().filename().string();

                    if (filename.starts_with(prefix)) {
                        auto suffix = filename.substr(prefix.size());

                        debug("Found custom subcommand ('%s') '%s'", filename, suffix);

                        commandNames.push_back(suffix);
                    }
                }
            } catch (fs::filesystem_error & fs_exc) { // NOLINT(lix-foreign-exceptions)
                if (fs_exc.code() != std::errc::no_such_file_or_directory && fs_exc.code() != std::errc::not_a_directory && fs_exc.code() != std::errc::permission_denied) {
                    throw SysError("while searching for all available commands in search path '%1%', while analyzing '%2%': %3%'", searchPath, entry.path(), fs_exc.what());
                }
            }
        }
    }

    return commandNames;
}

std::optional<ExperimentalFeature> Command::experimentalFeature ()
{
    return { Xp::NixCommand };
}

MultiCommand::MultiCommand(const CommandMap & commands_, bool allowExternal)
    : commands(commands_),
      customCommandSearchPaths(
          allowExternal ? tokenizeString<Strings>(
              getEnv("PATH").value_or(""),
              ":"
          ) : Strings()
      ),
      isExternalSubcommand(false)
{
    expectArgs({
        .label = "subcommand",
        .optional = true,
        .handler = {[=,this](std::string s) {
            assert(!command);
            auto it = commands.find(s);

            // NOTE: this logic does not rely
            // on `allowExternal`, indeed:
            // if external subcommands are not allowed, the search paths should be empty which will short-circuit any search and will never ever return external subcommands.

            // If `i` is not found, look into custom subcommands.
            if (it == commands.end()) {
                debug("looking for %s", s);
                auto possibleNewSubcommand = searchForCustomSubcommand(aio(), s, ExternalCommand::lixExternalPrefix, customCommandSearchPaths);

                if (possibleNewSubcommand) {
                    command = {s, *possibleNewSubcommand};
                    isExternalSubcommand = true;
                }
            } else {
                command = {s, it->second(aio())};
            }

            // By this point, we tried everything:
            // (a) built-in commands
            // (b) filesystem view of external subcommands
            //
            // We are going to do the expensive thing of looking for all external subcommands now
            // for error reporting purpose.
            if (!command) {
                std::set<std::string> commandNames;
                auto customCommands = searchForAllAvailableCustomSubcommands("lix-", customCommandSearchPaths);
                // As we are going to throw an error, there's no need to fill the hot cache of subcommands.
                for (auto & name : customCommands)
                    commandNames.insert(name);
                for (auto & [name, _] : commands)
                    commandNames.insert(name);
                auto suggestions = Suggestions::bestMatches(commandNames, s);
                throw UsageError(suggestions, "'%s' is not a recognised command", s);
            }

            if (isExternalSubcommand) {
                debug("Found external subcommand for %s", s);
            }

            command->second->parent = this;
        }},
        .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
            for (auto & [name, command] : commands)
                if (name.starts_with(prefix))
                    completions.add(name);
        }}
    });

    categories[Command::catDefault] = "Available commands";
    if (allowExternal) {
        categories[Command::catCustom] = "External custom commands";
    }
}

bool MultiCommand::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    if (!isExternalSubcommand && Args::processFlag(pos, end)) return true;
    if (command && command->second->processFlag(pos, end)) return true;
    return false;
}

bool MultiCommand::processArgs(const Strings & args, bool finish)
{
    if (command)
        return command->second->processArgs(args, finish);
    else
        return Args::processArgs(args, finish);
}

JSON MultiCommand::toJSON()
{
    // FIXME: use Command::toJSON() as well.

    auto cmds = JSON::object();

    for (auto & [name, commandFun] : commands) {
        auto command = commandFun(aio());
        auto j = command->toJSON();
        auto cat = JSON::object();
        cat["id"] = command->category();
        cat["description"] = trim(categories[command->category()]);
        cat["experimental-feature"] = command->experimentalFeature();
        j["category"] = std::move(cat);
        cmds[name] = std::move(j);
    }

    auto res = Args::toJSON();
    res["commands"] = std::move(cmds);
    return res;
}

ExternalCommand::ExternalCommand(AsyncIoRoot & aio, std::filesystem::path absoluteBinaryPath) : aio_(aio), absoluteBinaryPath(absoluteBinaryPath) {
    // NOTE: on shell invocation, argv[0] is the basename of the binary invoked.
    // We just reproduce this behavior.
    externalArgv.push_back(this->absoluteBinaryPath.filename());
}

bool ExternalCommand::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    externalArgv.push_back(*pos++);

    // All flags are recognised as we leave
    // parsing to the external commands.
    return true;
}

bool ExternalCommand::processArgs(const Strings & args, bool finish)
{
    for (const auto & arg : args) {
        externalArgv.push_back(arg);
    }

    return true;
}

void ExternalCommand::run() {
    printMsg(
        lvlChatty,
        "running external command: %s",
        concatMapStringsSep(" ", externalArgv, shellEscape)
    );
    execv(absoluteBinaryPath.c_str(), stringsToCharPtrs(externalArgv).data());

    throw SysError(errno, "failed to execute external command '%1%'", absoluteBinaryPath);
}

}
