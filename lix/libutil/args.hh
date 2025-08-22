#pragma once
///@file

#include "lix/libutil/async.hh"
#include "lix/libutil/experimental-features.hh"
#include "lix/libutil/json-fwd.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/ref.hh"

#include <functional>
#include <map>
#include <memory>
#include <limits>

#include <optional>
#include <set>
#include <filesystem>


namespace nix {

enum class HashType : char;

class MultiCommand;

class RootArgs;

class AddCompletions;

class Args
{
public:

    /**
     * Return a short one-line description of the command.
     */
    virtual std::string description() { return ""; }

    virtual bool forceImpureByDefault() { return false; }

    /**
     * Return documentation about this command, in Markdown format.
     */
    virtual std::string doc() { return ""; }

    virtual AsyncIoRoot & aio() = 0;

protected:

    /**
     * The largest `size_t` is used to indicate the "any" arity, for
     * handlers/flags/arguments that accept an arbitrary number of
     * arguments.
     */
    static const size_t ArityAny = std::numeric_limits<size_t>::max();

    /**
     * Arguments (flags/options and positional) have a "handler" which is
     * caused when the argument is parsed. The handler has an arbitrary side
     * effect, including possible affect further command-line parsing.
     *
     * There are many constructors in order to support many shorthand
     * initializations, and this is used a lot.
     */
    struct Handler
    {
        std::function<void(std::vector<std::string>)> fun;
        size_t arity;

        Handler() {}

        Handler(std::function<void(std::vector<std::string>)> && fun)
            : fun(std::move(fun))
            , arity(ArityAny)
        { }

        Handler(std::function<void()> && handler)
            : fun([handler{std::move(handler)}](std::vector<std::string>) { handler(); })
            , arity(0)
        { }

        Handler(std::function<void(std::string)> && handler)
            : fun([handler{std::move(handler)}](std::vector<std::string> ss) {
                handler(std::move(ss[0]));
              })
            , arity(1)
        { }

        Handler(std::function<void(std::string, std::string)> && handler)
            : fun([handler{std::move(handler)}](std::vector<std::string> ss) {
                handler(std::move(ss[0]), std::move(ss[1]));
              })
            , arity(2)
        { }

        Handler(std::vector<std::string> * dest)
            : fun([=](std::vector<std::string> ss) { *dest = ss; })
            , arity(ArityAny)
        { }

        Handler(std::string * dest)
            : fun([=](std::vector<std::string> ss) { *dest = ss[0]; })
            , arity(1)
        { }

        Handler(std::optional<std::string> * dest)
            : fun([=](std::vector<std::string> ss) { *dest = ss[0]; })
            , arity(1)
        { }

        template<class T>
        Handler(T * dest, const T & val)
            : fun([=](std::vector<std::string> ss) { *dest = val; })
            , arity(0)
        { }

        template<class I>
        Handler(I * dest)
            : fun([=](std::vector<std::string> ss) {
                *dest = string2IntWithUnitPrefix<I>(ss[0]);
              })
            , arity(1)
        { }

        template<class I>
        Handler(std::optional<I> * dest)
            : fun([=](std::vector<std::string> ss) {
                *dest = string2IntWithUnitPrefix<I>(ss[0]);
            })
            , arity(1)
        { }
    };

    /**
     * The basic function type of the completion callback.
     *
     * Used to define `CompleterClosure` and some common case completers
     * that individual flags/arguments can use.
     *
     * The `AddCompletions` that is passed is an interface to the state
     * stored as part of the root command
     */
    typedef void CompleterFun(AddCompletions &, size_t, std::string_view);

    /**
     * The closure type of the completion callback.
     *
     * This is what is actually stored as part of each Flag / Expected
     * Arg.
     */
    typedef std::function<CompleterFun> CompleterClosure;

    /**
     * Description of flags / options
     *
     * These are arguments like `-s` or `--long` that can (mostly)
     * appear in any order.
     */
    struct Flag
    {
        typedef std::shared_ptr<Flag> ptr;

        std::string longName;
        std::set<std::string> aliases;
        char shortName = 0;
        std::string description;
        std::string category;
        Strings labels;
        Handler handler;
        CompleterClosure completer;
        /// Whether to hide this flag in generated documentation and CLI specifications.
        bool hidden = false;

        std::optional<ExperimentalFeature> experimentalFeature;

        static Flag mkHashTypeFlag(std::string && longName, HashType * ht);
        static Flag mkHashTypeOptFlag(std::string && longName, std::optional<HashType> * oht);
    };

    /**
     * Index of all registered "long" flag descriptions (flags like
     * `--long`).
     */
    std::map<std::string, Flag::ptr> longFlags;

    /**
     * Index of all registered "short" flag descriptions (flags like
     * `-s`).
     */
    std::map<char, Flag::ptr> shortFlags;

    /**
     * Process a single flag and its arguments, pulling from an iterator
     * of raw CLI args as needed.
     *
     * @return false if the flag is not recognised.
     */
    virtual bool processFlag(Strings::iterator & pos, Strings::iterator end);

    /**
     * Description of positional arguments
     *
     * These are arguments that do not start with a `-`, and for which
     * the order does matter.
     */
    struct ExpectedArg
    {
        std::string label;
        bool optional = false;
        Handler handler;
        CompleterClosure completer;
    };

    /**
     * Queue of expected positional argument forms.
     *
     * Positional argument descriptions are inserted on the back.
     *
     * As positional arguments are passed, these are popped from the
     * front, until there are hopefully none left as all args that were
     * expected in fact were passed.
     */
    std::list<ExpectedArg> expectedArgs;
    /**
     * List of processed positional argument forms.
     *
     * All items removed from `expectedArgs` are added here. After all
     * arguments were processed, this list should be exactly the same as
     * `expectedArgs` was before.
     *
     * This list is used to extend the lifetime of the argument forms.
     * If this is not done, some closures that reference the command
     * itself will segfault.
    */
   std::list<ExpectedArg> processedArgs;

   /**
    * Process some positional arugments
    *
    * @param finish: We have parsed everything else, and these are the only
    * arguments left. Used because we accumulate some "pending args" we might
    * have left over.
    *
    * @return true if the passed arguments were fully consumed and no further processing is
    * required, false if the passed arguments should be processed with more context.
    *
    */
   virtual bool processArgs(const Strings & args, bool finish);

   virtual Strings::iterator rewriteArgs(Strings & args, Strings::iterator pos)
   {
       return pos;
   }

    std::set<std::string> hiddenCategories;

    /**
     * Called after all command line flags before the first non-flag
     * argument (if any) have been processed.
     */
    virtual void initialFlagsProcessed() {}

    /**
     * Returns this Args as a RootArgs if it is one, or \ref std::nullopt otherwise.
     */
    virtual std::optional<std::reference_wrapper<RootArgs>> asRootArgs() {
        return std::nullopt;
    }

public:

    void addFlag(Flag && flag);

    void removeFlag(const std::string & longName);

    void expectArgs(ExpectedArg && arg)
    {
        expectedArgs.emplace_back(std::move(arg));
    }

    /**
     * Expect a string argument.
     */
    void expectArg(const std::string & label, std::string * dest, bool optional = false)
    {
        expectArgs({
            .label = label,
            .optional = optional,
            .handler = {dest}
        });
    }

    /**
     * Expect 0 or more arguments.
     */
    void expectArgs(const std::string & label, std::vector<std::string> * dest)
    {
        expectArgs({
            .label = label,
            .handler = {dest}
        });
    }

    static CompleterFun completePath;

    static CompleterFun completeDir;

    virtual JSON toJSON();

    friend class MultiCommand;

    /**
     * The parent command, used if this is a subcommand.
     *
     * Invariant: An Args with a null parent must also be a RootArgs
     *
     * \todo this would probably be better in the CommandClass.
     * getRoot() could be an abstract method that peels off at most one
     * layer before recuring.
     */
    MultiCommand * parent = nullptr;

    /**
     * Traverse parent pointers until we find the \ref RootArgs "root
     * arguments" object.
     */
    RootArgs & getRoot();
};

/**
 * A command is an argument parser that can be executed by calling its
 * run() method.
 */
struct Command : virtual public Args
{
    friend class MultiCommand;

    virtual ~Command() { }

    /**
     * Entry point to the command
     */
    virtual void run() = 0;

    typedef int Category;

    static constexpr Category catDefault = 0;
    static constexpr Category catCustom = 1000;

    virtual std::optional<ExperimentalFeature> experimentalFeature ();

    virtual Category category() { return catDefault; }
};

using CommandMap = std::map<std::string, std::function<ref<Command>(AsyncIoRoot &)>>;

/**
 * An argument parser that supports multiple subcommands,
 * i.e. ‘<command> <subcommand>’.
 */
class MultiCommand : public Command
{
public:
    CommandMap commands;
    Strings customCommandSearchPaths;
    bool isExternalSubcommand;

    std::map<Command::Category, std::string> categories;

    /**
     * Selected command, if any.
     */
    std::optional<std::pair<std::string, ref<Command>>> command;

    MultiCommand(const CommandMap & commands, bool allowExternal = false);

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;

    JSON toJSON() override;
};

/**
 * An external command wrapper which is represented by a external binary.
 * i.e. `lix-flakes`.
 */
class ExternalCommand : virtual public Command
{
    Strings externalArgv;
    AsyncIoRoot & aio_;
public:
    std::filesystem::path absoluteBinaryPath;
    static constexpr std::string_view lixExternalPrefix = "lix-";

    ExternalCommand(AsyncIoRoot & aio, std::filesystem::path absoluteBinaryPath);

    virtual bool processFlag(Strings::iterator & pos, Strings::iterator end) override;
    virtual bool processArgs(const Strings & args, bool finish) override;
    virtual void run() override;
    virtual std::optional<ExperimentalFeature> experimentalFeature () override {
        return Xp::LixCustomSubCommands;
    }

    // Create a custom category section.
    virtual Category category() override { return catCustom; }

    virtual AsyncIoRoot & aio() override { return aio_; }
};

/* This returns a Command handle
 * if the command name exist in one of the search paths and points to an executable regular file.
 *
 * i.e. if $searchpath/$command exist for any $searchpath in `searchPaths` and $searchpath/$command links to an executable regular file.
 */
std::optional<ref<Command>> searchForCustomSubcommand(const std::string_view & command, const Strings & searchPaths);
/* This will read all directories in searchPaths one by one and look for all executable regular files which starts with `$prefix-`.
 * Finally, it will return the list of commands stripped of their `$prefix` prefix.
 *
 * If you need to know about a specific command, prefer `searchForCustomSubcommand`.
 */
Strings searchForAllAvailableCustomSubcommands(const std::string_view & prefix, const Strings & searchPaths);

struct Completion {
    std::string completion;
    std::string description;

    bool operator<(const Completion & other) const;
};

/**
 * The abstract interface for completions callbacks
 *
 * The idea is to restrict the callback so it can only add additional
 * completions to the collection, or set the completion type. By making
 * it go through this interface, the callback cannot make any other
 * changes, or even view the completions / completion type that have
 * been set so far.
 */
class AddCompletions
{
public:

    /**
     * The type of completion we are collecting.
     */
    enum class Type {
        Normal,
        Filenames,
        Attrs,
    };

    /**
     * Set the type of the completions being collected
     *
     * \todo it should not be possible to change the type after it has been set.
     */
    virtual void setType(Type type) = 0;

    /**
     * Add a single completion to the collection
     */
    virtual void add(std::string completion, std::string description = "") = 0;
};

}
