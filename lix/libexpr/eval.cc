#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libstore/path.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/ansicolor.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/english.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libexpr/primops.hh"
#include "lix/libexpr/print-options.hh"
#include "lix/libmain/shared.hh"
#include "lix/libutil/suggestions.hh"
#include "lix/libutil/types.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/downstream-placeholder.hh"
#include "lix/libexpr/gc-alloc.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libexpr/function-trace.hh"
#include "lix/libstore/profiles.hh"
#include "lix/libexpr/print.hh"
#include "lix/libexpr/gc-small-vector.hh"
#include "lix/libfetchers/fetch-to-store.hh"
#include "lix/libexpr/flake/flakeref.hh"
#include "lix/libutil/exit.hh"
#include "lix/libutil/json.hh"
#include "symbol-table.hh"

#include <algorithm>
#include <iostream>
#include <ostream>
#include <sstream>
#include <cstring>
#include <optional>
#include <string>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fstream>
#include <functional>

#include <sys/resource.h>
#include <boost/container/small_vector.hpp>

#if HAVE_BOEHMGC

#define GC_INCLUDE_NEW

#include <gc/gc.h>
#include <gc/gc_cpp.h>

#endif

namespace nix {

RootValue allocRootValue(Value * v)
{
    return std::allocate_shared<Value *>(TraceableAllocator<Value *>(), v);
}

// Pretty print types for assertion errors
std::ostream & operator << (std::ostream & os, const ValueType t) {
    os << showType(t);
    return os;
}

std::string printValue(EvalState & state, Value & v)
{
    std::ostringstream out;
    v.print(state, out);
    return out.str();
}

const Value * getPrimOp(const Value &v) {
    const Value * primOp = &v;
    while (primOp->isPrimOpApp()) {
        primOp = primOp->primOpApp.left;
    }
    assert(primOp->isPrimOp());
    return primOp;
}

std::string_view showType(ValueType type, bool withArticle)
{
    #define WA(a, w) withArticle ? a " " w : w
    switch (type) {
        case nInt: return WA("an", "integer");
        case nBool: return WA("a", "Boolean");
        case nString: return WA("a", "string");
        case nPath: return WA("a", "path");
        case nNull: return "null";
        case nAttrs: return WA("a", "set");
        case nList: return WA("a", "list");
        case nFunction: return WA("a", "function");
        case nExternal: return WA("an", "external value");
        case nFloat: return WA("a", "float");
        case nThunk: return WA("a", "thunk");
    }
    abort();
}


std::string showType(const Value & v)
{
    // Allow selecting a subset of enum values
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (v.internalType) {
        case tString: return v.string.context ? "a string with context" : "a string";
        case tPrimOp:
            return fmt("the built-in function '%s'", std::string(v.primOp->name));
        case tPrimOpApp:
            return fmt("the partially applied built-in function '%s'", std::string(getPrimOp(v)->primOp->name));
        case tExternal: return v.external->showType();
        case tThunk: return v.isBlackhole() ? "a black hole" : "a thunk";
        case tApp: return "a function application";
    default:
        return std::string(showType(v.type()));
    }
    #pragma GCC diagnostic pop
}


#if HAVE_BOEHMGC
/* Called when the Boehm GC runs out of memory. */
static void * oomHandler(size_t requested)
{
    /* Convert this to a proper C++ exception. */
    throw std::bad_alloc();
}

#endif


static Symbol getName(const AttrName & name, EvalState & state, Env & env)
{
    if (name.symbol) {
        return name.symbol;
    } else {
        Value nameValue;
        name.expr->eval(state, env, nameValue);
        state.forceStringNoCtx(nameValue, name.expr->getPos(), "while evaluating an attribute name");
        return state.ctx.symbols.create(nameValue.string.s);
    }
}

static bool libexprInitialised = false;

void initLibExpr()
{
    if (libexprInitialised) return;

#if HAVE_BOEHMGC
    /* Initialise the Boehm garbage collector. */

    /* Don't look for interior pointers. This reduces the odds of
       misdetection a bit. */
    GC_set_all_interior_pointers(0);

    /* We don't have any roots in data segments, so don't scan from
       there. */
    GC_set_no_dls(1);

    GC_INIT();

    GC_set_oom_fn(oomHandler);

    /* Set the initial heap size to something fairly big (25% of
       physical RAM, up to a maximum of 384 MiB) so that in most cases
       we don't need to garbage collect at all.  (Collection has a
       fairly significant overhead.)  The heap size can be overridden
       through libgc's GC_INITIAL_HEAP_SIZE environment variable.  We
       should probably also provide a nix.conf setting for this.  Note
       that GC_expand_hp() causes a lot of virtual, but not physical
       (resident) memory to be allocated.  This might be a problem on
       systems that don't overcommit. */
    if (!getEnv("GC_INITIAL_HEAP_SIZE")) {
        int64_t size = 32 * 1024 * 1024;
#if HAVE_SYSCONF && defined(_SC_PAGESIZE) && defined(_SC_PHYS_PAGES)
        int64_t maxSize = 384 * 1024 * 1024;
        int64_t pageSize = sysconf(_SC_PAGESIZE);
        int64_t pages = sysconf(_SC_PHYS_PAGES);
        if (pageSize != -1) {
            size = (pageSize * pages) / 4; // 25% of RAM
        }
        if (size > maxSize) size = maxSize;
#endif
        debug("setting initial heap size to %1% bytes", size);
        GC_expand_hp(size);
    }

#endif

    fetchers::initLibFetchers();

    libexprInitialised = true;
}

StaticSymbols::StaticSymbols(SymbolTable & symbols)
    : outPath(symbols.create("outPath"))
    , drvPath(symbols.create("drvPath"))
    , type(symbols.create("type"))
    , meta(symbols.create("meta"))
    , name(symbols.create("name"))
    , value(symbols.create("value"))
    , system(symbols.create("system"))
    , overrides(symbols.create("__overrides"))
    , outputs(symbols.create("outputs"))
    , outputName(symbols.create("outputName"))
    , ignoreNulls(symbols.create("__ignoreNulls"))
    , file(symbols.create("file"))
    , line(symbols.create("line"))
    , column(symbols.create("column"))
    , functor(symbols.create("__functor"))
    , toString(symbols.create("__toString"))
    , right(symbols.create("right"))
    , wrong(symbols.create("wrong"))
    , structuredAttrs(symbols.create("__structuredAttrs"))
    , allowedReferences(symbols.create("allowedReferences"))
    , allowedRequisites(symbols.create("allowedRequisites"))
    , disallowedReferences(symbols.create("disallowedReferences"))
    , disallowedRequisites(symbols.create("disallowedRequisites"))
    , maxSize(symbols.create("maxSize"))
    , maxClosureSize(symbols.create("maxClosureSize"))
    , builder(symbols.create("builder"))
    , args(symbols.create("args"))
    , contentAddressed(symbols.create("__contentAddressed"))
    , impure(symbols.create("__impure"))
    , outputHash(symbols.create("outputHash"))
    , outputHashAlgo(symbols.create("outputHashAlgo"))
    , outputHashMode(symbols.create("outputHashMode"))
    , recurseForDerivations(symbols.create("recurseForDerivations"))
    , description(symbols.create("description"))
    , self(symbols.create("self"))
    , startSet(symbols.create("startSet"))
    , operator_(symbols.create("operator"))
    , key(symbols.create("key"))
    , path(symbols.create("path"))
    , prefix(symbols.create("prefix"))
    , outputSpecified(symbols.create("outputSpecified"))
    , exprSymbols{
        .sub = symbols.create("__sub"),
        .lessThan = symbols.create("__lessThan"),
        .mul = symbols.create("__mul"),
        .div = symbols.create("__div"),
        .or_ = symbols.create("or"),
        .findFile = symbols.create("__findFile"),
        .nixPath = symbols.create("__nixPath"),
        .body = symbols.create("body"),
        .overrides = symbols.create("__overrides"),
    }
{
}

EvalMemory::EvalMemory()
    : valueAllocCache(std::allocate_shared<void *>(TraceableAllocator<void *>(), nullptr))
    , env1AllocCache(std::allocate_shared<void *>(TraceableAllocator<void *>(), nullptr))
{
    assert(libexprInitialised);
}

EvalBuiltins::EvalBuiltins(
    EvalMemory & mem,
    SymbolTable & symbols,
    const SearchPath & searchPath,
    const Path & storeDir,
    size_t size
)
    : mem(mem)
    , symbols(symbols)
    , env(mem.allocEnv(size))
    , staticEnv{std::make_shared<StaticEnv>(nullptr, nullptr)}
{
    createBaseEnv(searchPath, storeDir);
}

EvalPaths::EvalPaths(
    AsyncIoRoot & aio,
    const ref<Store> & store,
    SearchPath searchPath,
    EvalErrorContext & errors
)
    : store(store)
    , searchPath_(std::move(searchPath))
    , errors(errors)
{
    if (evalSettings.restrictEval || evalSettings.pureEval) {
        allowedPaths = AllowedPath{.allowAllChildren = false};

        for (auto & i : searchPath_.elements) {
            auto r = aio.blockOn(resolveSearchPathPath(i.path));
            if (!r) continue;

            auto path = std::move(*r);

            if (store->isInStore(path)) {
                try {
                    StorePathSet closure;
                    aio.blockOn(store->computeFSClosure(store->toStorePath(path).first, closure));
                    for (auto & path : closure)
                        allowPath(path);
                } catch (InvalidPath &) {
                    allowPath(path);
                }
            } else
                allowPath(path);
        }
    }
}

Evaluator::Evaluator(
    AsyncIoRoot & aio,
    const SearchPath & _searchPath,
    ref<Store> store,
    std::shared_ptr<Store> buildStore,
    std::function<ReplExitStatus(EvalState & es, ValMap const & extraEnv)> debugRepl
)
    : s(symbols)
    , paths(aio, store, [&] {
        SearchPath searchPath;
        if (!evalSettings.pureEval) {
            for (auto & i : _searchPath.elements)
                searchPath.elements.emplace_back(SearchPath::Elem {i});
            for (auto & i : evalSettings.nixPath.get())
                searchPath.elements.emplace_back(SearchPath::Elem::parse(i));
        }
        return searchPath;
    }(), errors)
    , builtins(mem, symbols, paths.searchPath(), store->config().storeDir)
    , repair(NoRepair)
    , store(store)
    , buildStore(buildStore ? ref<Store>::unsafeFromPtr(buildStore) : store)
    , debug{
          debugRepl ? std::make_unique<DebugState>(
              positions,
              symbols,
              [this, debugRepl](const ValMap & extraEnv, NeverAsync) {
                  return activeEval
                      ? debugRepl(*activeEval, extraEnv)
                      : ReplExitStatus::Continue;
              })
                    : nullptr
      }
    , errors{positions, debug.get()}
{
    stats.countCalls = getEnv("NIX_COUNT_CALLS").value_or("0") != "0";

    static_assert(sizeof(Env) <= 16, "environment must be <= 16 bytes");
}

box_ptr<EvalState> Evaluator::begin(AsyncIoRoot & aio)
{
    assert(!activeEval);
    return box_ptr<EvalState>::unsafeFromNonnull(
        std::unique_ptr<EvalState>(new EvalState(aio, *this))
    );
}

EvalState::EvalState(AsyncIoRoot & aio, Evaluator & ctx) : ctx(ctx), aio(aio)
{
    ctx.activeEval = this;
}

EvalState::~EvalState()
{
    ctx.activeEval = nullptr;
}


void EvalPaths::allowPath(const Path & path)
{
    if (!allowedPaths) {
        return;
    }

    CanonPath p(path);
    auto * level = &*allowedPaths;
    for (const auto & entry : p) {
        level = &level->children.emplace(std::piecewise_construct, std::tuple(entry), std::tuple())
                     .first->second;
    }
    level->allowAllChildren = true;
}

void EvalPaths::allowPath(const StorePath & storePath)
{
    if (allowedPaths)
        allowPath(store->toRealPath(storePath));
}

void EvalPaths::allowAndSetStorePathString(const StorePath & storePath, Value & v)
{
    allowPath(storePath);

    mkStorePathString(storePath, v);
}

CheckedSourcePath EvalPaths::checkSourcePath(const SourcePath & path_)
{
    if (!allowedPaths) return auto(path_).unsafeIntoChecked();

    auto i = resolvedPaths.find(path_.canonical().abs());
    if (i != resolvedPaths.end())
        return i->second;

    /* First canonicalize the path without symlinks, so we make sure an
     * attacker can't append ../../... to a path that would be in allowedPaths
     * and thus leak symlink targets.
     */
    const CanonPath abspath{path_.canonical().abs()};

    if (abspath.abs().starts_with(corepkgsPrefix)) {
        return SourcePath(std::move(abspath)).unsafeIntoChecked();
    }

    /* Resolve symlinks. This is mostly restricted copy of canonPath with
       resolveSymlinks=true, because we need access to intermediat paths. */
    debug("checking access to '%s'", abspath);

    /* Count the number of times we follow a symlink and stop at some
       arbitrary (but high) limit to prevent infinite loops. */
    unsigned int followCount = 0, maxFollow = 1024;

    std::optional<CanonPath> componentsBacking;
    std::vector<std::string_view> components(abspath.begin(), abspath.end());

retry:

    if (++followCount >= maxFollow) {
        throw Error("infinite symlink recursion in path '%1%'", path_);
    }

    // TODO: tests for this stuff
    const auto * level = &*allowedPaths;
    CheckedSourcePath current = SourcePath(CanonPath::root).unsafeIntoChecked();
    for (auto ct = components.begin(); ct != components.end(); ct++) {
        auto & p = *ct;
        // an empty level means all subpaths are allowed, propagate this forwards
        // by setting level=nullptr for the subsequent checks. a symlink will set
        // level to the "VFS" root and restart the check with a the resolved path
        if (level) {
            if (level->allowAllChildren) {
                level = nullptr;
            } else if (auto it = level->children.find(p); it != level->children.end()) {
                level = &it->second;
            } else {
                goto failed;
            }
        }
        auto next = (current + p).unsafeIntoChecked();
        auto st = next.maybeLstat();
        // resolve symlinks, treating nonexistant components like regular directories.
        // this mirrors canonPath behavior and is necessary for `builtins.pathExists`.
        if (st && st->type == InputAccessor::tSymlink) {
            auto target = next.readLink();
            auto levelResolved = target.starts_with("/")
                ? CanonPath(target)
                : CanonPath(current.canonical().abs() + "/" + target);
            for (ct++; ct != components.end(); ct++) {
                levelResolved.push(*ct);
            }
            components = {levelResolved.begin(), levelResolved.end()};
            componentsBacking = std::move(levelResolved);
            followCount += 1;
            goto retry;
        }
        current = std::move(next);
    }
    // Downstream users (e.g. `builtins.readDir` or `builtins.path`) will want to descend.
    if (level && !level->allowAllChildren) {
        goto failed;
    }

    resolvedPaths.insert_or_assign(path_.canonical().abs(), current);
    return current;

failed:
    auto modeInformation = evalSettings.pureEval
        ? "in pure eval mode (use '--impure' to override)"
        : "in restricted mode";
    throw RestrictedPathError("access to absolute path '%1%' is forbidden %2%", abspath, modeInformation);
}


void EvalPaths::checkURI(const std::string & uri)
{
    if (!evalSettings.restrictEval) return;

    /* 'uri' should be equal to a prefix, or in a subdirectory of a
       prefix. Thus, the prefix https://github.co does not permit
       access to https://github.com. Note: this allows 'http://' and
       'https://' as prefixes for any http/https URI. */
    for (auto & prefix : evalSettings.allowedUris.get())
        if (uri == prefix ||
            (uri.size() > prefix.size()
            && prefix.size() > 0
            && uri.starts_with(prefix)
            && (prefix[prefix.size() - 1] == '/' || uri[prefix.size()] == '/')))
            return;

    /* If the URI is a path, then check it against allowedPaths as
       well. */
    if (uri.starts_with("/")) {
        checkSourcePath(CanonPath(uri));
        return;
    }

    if (uri.starts_with("file://")) {
        checkSourcePath(CanonPath(std::string(uri, 7)));
        return;
    }

    throw RestrictedPathError("access to URI '%s' is forbidden in restricted mode", uri);
}


Path EvalPaths::toRealPath(const Path & path, const NixStringContext & context)
{
    // FIXME: check whether 'path' is in 'context'.
    return
        !context.empty() && store->isInStore(path)
        ? store->toRealPath(path)
        : path;
}


Value * EvalBuiltins::addConstant(const std::string & name, const Value & v, Constant info)
{
    Value * v2 = mem.allocValue();
    *v2 = v;
    addConstant(name, v2, info);
    return v2;
}


void EvalBuiltins::addConstant(const std::string & name, Value * v, Constant info)
{
    auto name2 = name.substr(0, 2) == "__" ? name.substr(2) : name;

    constantInfos.push_back({name2, info});

    if (!(evalSettings.pureEval && info.impureOnly)) {
        /* Check the type, if possible.

           We might know the type of a thunk in advance, so be allowed
           to just write it down in that case. */
        if (auto gotType = v->type(true); gotType != nThunk)
            assert(info.type == gotType);

        /* Install value the base environment. */
        staticEnv->vars.emplace_back(symbols.create(name), baseEnvDispl);
        env.values[baseEnvDispl++] = v;
        env.values[0]->attrs->push_back(Attr(symbols.create(name2), v));
    }
}


std::ostream & operator<<(std::ostream & output, PrimOp & primOp)
{
    output << "primop " << primOp.name;
    return output;
}

Value * EvalBuiltins::addPrimOp(PrimOp && primOp)
{
    /* Hack to make constants lazy: turn them into a application of
       the primop to a dummy value. */
    if (primOp.arity == 0) {
        primOp.arity = 1;
        auto vPrimOp = mem.allocValue();
        vPrimOp->mkPrimOp(new PrimOp(primOp));
        Value v;
        v.mkApp(vPrimOp, vPrimOp);
        return addConstant(primOp.name, v, {
            .type = nThunk, // FIXME
            .doc = primOp.doc,
        });
    }

    auto envName = symbols.create(primOp.name);
    if (primOp.name.starts_with("__"))
        primOp.name = primOp.name.substr(2);

    Value * v = mem.allocValue();
    v->mkPrimOp(new PrimOp(primOp));
    staticEnv->vars.emplace_back(envName, baseEnvDispl);
    env.values[baseEnvDispl++] = v;
    env.values[0]->attrs->push_back(Attr(symbols.create(primOp.name), v));
    return v;
}


Value & EvalBuiltins::get(const std::string & name)
{
    return *env.values[0]->attrs->find(symbols.create(name))->value;
}


std::optional<EvalBuiltins::Doc> EvalBuiltins::getDoc(Value & v)
{
    if (v.isPrimOp()) {
        auto v2 = &v;
        if (auto * doc = v2->primOp->doc)
            return Doc {
                .pos = {},
                .name = v2->primOp->name,
                .arity = v2->primOp->arity,
                .args = v2->primOp->args,
                .doc = doc,
            };
    }
    return {};
}


static std::set<std::string_view> sortedBindingNames(const SymbolTable & st, const StaticEnv & se)
{
    std::set<std::string_view> bindings;
    for (auto [symbol, displ] : se.vars)
        bindings.emplace(st[symbol]);
    return bindings;
}


// just for the current level of StaticEnv, not the whole chain.
void printStaticEnvBindings(const SymbolTable & st, const StaticEnv & se)
{
    std::cout << ANSI_MAGENTA;
    for (auto & i : sortedBindingNames(st, se))
        std::cout << i << " ";
    std::cout << ANSI_NORMAL;
    std::cout << std::endl;
}

// just for the current level of Env, not the whole chain.
void printWithBindings(const SymbolTable & st, const Env & env)
{
    if (env.values[0]->type() == nAttrs) {
        std::set<std::string_view> bindings;
        for (const auto & attr : *env.values[0]->attrs)
            bindings.emplace(st[attr.name]);

        std::cout << "with: ";
        std::cout << ANSI_MAGENTA;
        for (auto & i : bindings)
            std::cout << i << " ";
        std::cout << ANSI_NORMAL;
        std::cout << std::endl;
    }
}

void printEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env, int lvl)
{
    std::cout << "Env level " << lvl << std::endl;

    if (se.up && env.up) {
        std::cout << "static: ";
        printStaticEnvBindings(st, se);
        if (se.isWith)
            printWithBindings(st, env);
        std::cout << std::endl;
        printEnvBindings(st, *se.up, *env.up, ++lvl);
    } else {
        std::cout << ANSI_MAGENTA;
        // for the top level, don't print the double underscore ones;
        // they are in builtins.
        for (auto & i : sortedBindingNames(st, se))
            if (!i.starts_with("__"))
                std::cout << i << " ";
        std::cout << ANSI_NORMAL;
        std::cout << std::endl;
        if (se.isWith)
            printWithBindings(st, env);  // probably nothing there for the top level.
        std::cout << std::endl;

    }
}

void printEnvBindings(const EvalState &es, const Expr & expr, const Env & env)
{
    // just print the names for now
    auto se = es.ctx.debug->staticEnvFor(expr);
    if (se)
        printEnvBindings(es.ctx.symbols, *se, env, 0);
}

void mapStaticEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env, ValMap & vm)
{
    // add bindings for the next level up first, so that the bindings for this level
    // override the higher levels.
    // The top level bindings (builtins) are skipped since they are added for us by initEnv()
    if (env.up && se.up) {
        mapStaticEnvBindings(st, *se.up, *env.up, vm);

        if (se.isWith && env.values[0]->type() == nAttrs) {
            // add 'with' bindings.
            Bindings::iterator j = env.values[0]->attrs->begin();
            while (j != env.values[0]->attrs->end()) {
                vm[st[j->name]] = j->value;
                ++j;
            }
        } else {
            // iterate through staticenv bindings and add them.
            for (auto & i : se.vars)
                vm[st[i.first]] = env.values[i.second];
        }
    }
}

std::unique_ptr<ValMap> mapStaticEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env)
{
    auto vm = std::make_unique<ValMap>();
    mapStaticEnvBindings(st, se, env, *vm);
    return vm;
}

/**
 * Sets `inDebugger` to true on construction and false on destruction.
 */
class DebuggerGuard {
    bool & inDebugger;
public:
    DebuggerGuard(bool & inDebugger) : inDebugger(inDebugger) {
        inDebugger = true;
    }
    ~DebuggerGuard() {
        inDebugger = false;
    }
};

void DebugState::onEvalError(
    const EvalError * error, const Env & env, const Expr & expr, NeverAsync
)
{
    // Make sure we have a debugger to run and we're not already in a debugger.
    if (inDebugger)
        return;

    auto dts =
        error && expr.getPos()
        ? addTrace(DebugTrace {
            .pos = error->info().pos ? error->info().pos : positions[expr.getPos()],
            .expr = expr,
            .env = env,
            .hint = error->info().msg,
            .isError = true
        })
        : nullptr;

    if (error)
    {
        printError("%s\n", error->what());

        if (trylevel > 0 && error->info().level != lvlInfo) {
            if (evalSettings.ignoreExceptionsDuringTry) {
                printError("This exception occurred in a 'tryEval' call, " ANSI_RED "despite the use of " ANSI_GREEN "--ignore-try" ANSI_RED " to attempt to skip these" ANSI_NORMAL ". This is probably a bug. We would appreciate if you report it along with what caused it at https://git.lix.systems/lix-project/lix/issues.\n");
            } else {
                printError("This exception occurred in a 'tryEval' call. Use " ANSI_GREEN "--ignore-try" ANSI_NORMAL " to skip these.\n");
            }
        }
    }

    auto se = staticEnvFor(expr);
    if (se) {
        auto vm = mapStaticEnvBindings(symbols, *se.get(), env);
        DebuggerGuard _guard(inDebugger);
        auto exitStatus = errorCallback(*vm, {});
        switch (exitStatus) {
            case ReplExitStatus::QuitAll:
                if (error)
                    throw *error;
                throw Exit(0);
            case ReplExitStatus::Continue:
                break;
            default:
                abort();
        }
    }
}

DebugState::TraceFrame DebugState::addTrace(DebugTrace t)
{
    struct UnlinkDebugTrace
    {
        DebugState * state;
        void operator()(DebugTrace * trace)
        {
            state->latestTrace = trace->parent;
            delete trace;
        }
    };

    t.parent = latestTrace.lock();
    std::unique_ptr<DebugTrace, UnlinkDebugTrace> trace(
        new auto(std::move(t)), UnlinkDebugTrace{.state = this}
    );
    std::shared_ptr<DebugTrace> entry(std::move(trace));
    latestTrace = entry;
    return TraceFrame{entry};
}

template<typename... Args>
static DebugState::TraceFrame makeDebugTraceStacker(
    EvalState & state,
    Expr & expr,
    Env & env,
    std::shared_ptr<Pos> && pos,
    const Args & ... formatArgs)
{
    auto trace = state.ctx.debug->addTrace(DebugTrace{
        .pos = std::move(pos),
        .expr = expr,
        .env = env,
        .hint = HintFmt(formatArgs...),
        .isError = false,
    });
    if (state.ctx.debug->stop && state.ctx.debug->errorCallback)
        state.ctx.debug->onEvalError(nullptr, env, expr);
    return trace;
}

inline Value * EvalState::lookupVar(Env * env, const ExprVar & var, bool noEval)
{
    for (auto l = var.level; l; --l, env = env->up) ;

    if (!var.fromWith) return env->values[var.displ];

    // This early exit defeats the `maybeThunk` optimization for variables from `with`,
    // The added complexity of handling this appears to be similarly in cost, or
    // the cases where applicable were insignificant in the first place.
    if (noEval) return nullptr;

    auto * fromWith = var.fromWith;
    while (1) {
        forceAttrs(*env->values[0], fromWith->pos, "while evaluating the first subexpression of a with expression");
        Bindings::iterator j = env->values[0]->attrs->find(var.name);
        if (j != env->values[0]->attrs->end()) {
            if (ctx.stats.countCalls) ctx.stats.attrSelects[j->pos]++;
            return j->value;
        }
        if (!fromWith->parentWith)
            ctx.errors.make<UndefinedVarError>("undefined variable '%1%'", ctx.symbols[var.name]).atPos(var.pos).withFrame(*env, var).debugThrow();
        for (size_t l = fromWith->prevWith; l; --l, env = env->up) ;
        fromWith = fromWith->parentWith;
    }
}

Value EvalMemory::newList(size_t size)
{
    Value v;
    v.mkList(size);
    if (size > 2)
        v.bigList.elems = gcAllocType<Value *>(size);
    stats.nrListElems += size;
    return v;
}


void Evaluator::evalLazily(Expr & e, Value & v)
{
    v.mkThunk(&builtins.env, e);
    stats.nrThunks++;
}


void EvalState::mkPos(Value & v, PosIdx p)
{
    auto origin = ctx.positions.originOf(p);
    if (auto path = std::get_if<CheckedSourcePath>(&origin)) {
        auto attrs = ctx.buildBindings(3);
        attrs.alloc(ctx.s.file).mkString(path->to_string());
        makePositionThunks(*this, p, attrs.alloc(ctx.s.line), attrs.alloc(ctx.s.column));
        v.mkAttrs(attrs);
    } else
        v.mkNull();
}


void EvalPaths::mkStorePathString(const StorePath & p, Value & v)
{
    v.mkString(
        store->printStorePath(p),
        NixStringContext {
            NixStringContextElem::Opaque { .path = p },
        });
}


std::string EvalState::mkOutputStringRaw(
    const SingleDerivedPath::Built & b,
    std::optional<StorePath> optStaticOutputPath,
    const ExperimentalFeatureSettings & xpSettings)
{
    /* In practice, this is testing for the case of CA derivations, or
       dynamic derivations. */
    return optStaticOutputPath
        ? ctx.store->printStorePath(std::move(*optStaticOutputPath))
        /* Downstream we would substitute this for an actual path once
           we build the floating CA derivation */
        : DownstreamPlaceholder::fromSingleDerivedPathBuilt(b, xpSettings).render();
}


void EvalState::mkOutputString(
    Value & value,
    const SingleDerivedPath::Built & b,
    std::optional<StorePath> optStaticOutputPath,
    const ExperimentalFeatureSettings & xpSettings)
{
    value.mkString(
        mkOutputStringRaw(b, optStaticOutputPath, xpSettings),
        NixStringContext { b });
}


std::string EvalState::mkSingleDerivedPathStringRaw(
    const SingleDerivedPath & p)
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & o) {
            return ctx.store->printStorePath(o.path);
        },
        [&](const SingleDerivedPath::Built & b) {
            auto optStaticOutputPath = std::visit(overloaded {
                [&](const SingleDerivedPath::Opaque & o) {
                    auto drv = aio.blockOn(ctx.store->readDerivation(o.path));
                    auto i = drv.outputs.find(b.output);
                    if (i == drv.outputs.end())
                        throw Error("derivation '%s' does not have output '%s'", b.drvPath->to_string(*ctx.store), b.output);
                    return i->second.path(*ctx.store, drv.name, b.output);
                },
                [&](const SingleDerivedPath::Built & o) -> std::optional<StorePath> {
                    return std::nullopt;
                },
            }, b.drvPath->raw());
            return mkOutputStringRaw(b, optStaticOutputPath);
        }
    }, p.raw());
}


void EvalState::mkSingleDerivedPathString(
    const SingleDerivedPath & p,
    Value & v)
{
    v.mkString(
        mkSingleDerivedPathStringRaw(p),
        NixStringContext {
            std::visit([](auto && v) -> NixStringContextElem { return v; }, p),
        });
}


/* Create a thunk for the delayed computation of the given expression
   in the given environment.  But if the expression is a variable,
   then look it up right away.  This significantly reduces the number
   of thunks allocated. */
Value * Expr::maybeThunk(EvalState & state, Env & env)
{
    Value * v = state.ctx.mem.allocValue();
    v->mkThunk(&env, *this);
    state.ctx.stats.nrThunks++;
    return v;
}


Value * ExprVar::maybeThunk(EvalState & state, Env & env)
{
    Value * v = state.lookupVar(&env, *this, true);
    /* The value might not be initialised in the environment yet.
       In that case, ignore it. */
    if (v) { state.ctx.stats.nrAvoided++; return v; }
    return Expr::maybeThunk(state, env);
}


Value * ExprLiteral::maybeThunk(EvalState & state, Env & env)
{
    state.ctx.stats.nrAvoided++;
    return &v;
}


struct CachedEvalFile
{
    Value result;
    explicit CachedEvalFile(Value result): result(result) {}
};

void EvalState::evalFile(const SourcePath & path_, Value & v)
{
    auto path = ctx.paths.checkSourcePath(path_);

    if (auto i = ctx.caches.fileEval.find(path); i != ctx.caches.fileEval.end()) {
        v = i->second->result;
        return;
    }

    auto resolvedPath = ctx.paths.resolveExprPath(path);
    if (auto i = ctx.caches.fileEval.find(resolvedPath); i != ctx.caches.fileEval.end()) {
        v = i->second->result;
        return;
    }

    debug("evaluating file '%1%'", resolvedPath);
    Expr & e = ctx.parseExprFromFile(ctx.paths.checkSourcePath(resolvedPath));

    try {
        auto dts = ctx.debug
            ? makeDebugTraceStacker(
                *this,
                e,
                ctx.builtins.env,
                e.getPos() ? std::make_shared<Pos>(ctx.positions[e.getPos()]) : nullptr,
                "while evaluating the file '%1%':", resolvedPath.to_string())
            : nullptr;

        eval(e, v);
    } catch (Error & e) {
        e.addTrace(nullptr, "while evaluating the file '%1%':", resolvedPath.to_string());
        throw;
    }

    auto cache = std::allocate_shared<CachedEvalFile>(TraceableAllocator<CachedEvalFile>(), v);
    ctx.caches.fileEval[resolvedPath] = cache;
    if (path != resolvedPath) ctx.caches.fileEval[path] = cache;
}


void EvalState::resetFileCache()
{
    ctx.caches.fileEval.clear();
}


void EvalState::eval(Expr & e, Value & v)
{
    e.eval(*this, ctx.builtins.env, v);
}

#define checkType(typeName, stringName) \
    if (v.type() != (typeName)) \
        ctx.errors.make<TypeError>( \
                "expected a %1% but found %2%: %3%", \
                Uncolored(stringName), \
                showType(v), \
                ValuePrinter(*this, v, errorPrintOptions) \
            ).atPos(e.getPos()).withFrame(env, e).debugThrow();

inline bool EvalState::evalBool(Env & env, Expr & e)
{
    Value v;
    e.eval(*this, env, v);
    checkType(nBool, "Boolean");
    return v.boolean;
}


inline void EvalState::evalAttrs(Env & env, Expr & e, Value & v)
{
    e.eval(*this, env, v);
    checkType(nAttrs, "set");
}

inline void EvalState::evalList(Env & env, Expr & e, Value & v)
{
    e.eval(*this, env, v);
    checkType(nList, "list");
}


void Expr::eval(EvalState & state, Env & env, Value & v)
{
    abort();
}


void ExprLiteral::eval(EvalState & state, Env & env, Value & v)
{
    v = this->v;
}

Env * ExprAttrs::buildInheritFromEnv(EvalState & state, Env & up)
{
    Env & inheritEnv = state.ctx.mem.allocEnv(inheritFromExprs->size());
    inheritEnv.up = &up;

    Displacement displ = 0;
    for (auto & from : *inheritFromExprs)
        inheritEnv.values[displ++] = from->maybeThunk(state, up);

    return &inheritEnv;
}

void ExprSet::eval(EvalState & state, Env & env, Value & v)
{
    v.mkAttrs(state.ctx.buildBindings(attrs.size() + dynamicAttrs.size()).finish());
    auto dynamicEnv = &env;

    if (recursive) {

        /* Create a new environment that contains the attributes in
           this `rec'. */
        Env & env2(state.ctx.mem.allocEnv(attrs.size()));
        env2.up = &env;
        dynamicEnv = &env2;
        Env * inheritEnv = inheritFromExprs ? buildInheritFromEnv(state, env2) : nullptr;

        ExprAttrs::AttrDefs::iterator overrides = attrs.find(state.ctx.s.overrides);
        bool hasOverrides = overrides != attrs.end();

        /* The recursive attributes are evaluated in the new
           environment, while the inherited attributes are evaluated
           in the original environment. */
        Displacement displ = 0;
        for (auto & i : attrs) {
            Value * vAttr;
            if (hasOverrides && i.second.kind != ExprAttrs::AttrDef::Kind::Inherited) {
                vAttr = state.ctx.mem.allocValue();
                vAttr->mkThunk(i.second.chooseByKind(&env2, &env, inheritEnv), *i.second.e);
                state.ctx.stats.nrThunks++;
            } else
                vAttr = i.second.e->maybeThunk(state, *i.second.chooseByKind(&env2, &env, inheritEnv));
            env2.values[displ++] = vAttr;
            v.attrs->push_back(Attr(i.first, vAttr, i.second.pos));
        }

        /* If the rec contains an attribute called `__overrides', then
           evaluate it, and add the attributes in that set to the rec.
           This allows overriding of recursive attributes, which is
           otherwise not possible.  (You can use the // operator to
           replace an attribute, but other attributes in the rec will
           still reference the original value, because that value has
           been substituted into the bodies of the other attributes.
           Hence we need __overrides.) */
        if (hasOverrides) {
            Value * vOverrides = (*v.attrs)[overrides->second.displ].value;
            state.forceAttrs(*vOverrides, noPos, "while evaluating the `__overrides` attribute");
            Bindings * newBnds = state.ctx.mem.allocBindings(v.attrs->capacity() + vOverrides->attrs->size());
            for (auto & i : *v.attrs)
                newBnds->push_back(i);
            for (auto & i : *vOverrides->attrs) {
                ExprAttrs::AttrDefs::iterator j = attrs.find(i.name);
                if (j != attrs.end()) {
                    (*newBnds)[j->second.displ] = i;
                    env2.values[j->second.displ] = i.value;
                } else
                    newBnds->push_back(i);
            }
            newBnds->sort();
            v.attrs = newBnds;
        }
    }

    else {
        Env * inheritEnv = inheritFromExprs ? buildInheritFromEnv(state, env) : nullptr;
        for (auto & i : attrs) {
            v.attrs->push_back(Attr(
                    i.first,
                    i.second.e->maybeThunk(state, *i.second.chooseByKind(&env, &env, inheritEnv)),
                    i.second.pos));
        }
    }

    /* Dynamic attrs apply *after* rec and __overrides. */
    for (auto & i : dynamicAttrs) {
        Value nameVal;
        i.nameExpr->eval(state, *dynamicEnv, nameVal);
        state.forceValue(nameVal, i.pos);
        if (nameVal.type() == nNull)
            continue;
        state.forceStringNoCtx(nameVal, i.pos, "while evaluating the name of a dynamic attribute");
        auto nameSym = state.ctx.symbols.create(nameVal.string.s);
        Bindings::iterator j = v.attrs->find(nameSym);
        if (j != v.attrs->end())
            state.ctx.errors.make<EvalError>("dynamic attribute '%1%' already defined at %2%", state.ctx.symbols[nameSym], state.ctx.positions[j->pos]).atPos(i.pos).withFrame(env, *this).debugThrow();

        i.valueExpr->setName(nameSym);
        /* Keep sorted order so find can catch duplicates */
        v.attrs->push_back(Attr(nameSym, i.valueExpr->maybeThunk(state, *dynamicEnv), i.pos));
        v.attrs->sort(); // FIXME: inefficient
    }

    v.attrs->pos = pos;
}


void ExprLet::eval(EvalState & state, Env & env, Value & v)
{
    /* Create a new environment that contains the attributes in this
       `let'. */
    Env & env2(state.ctx.mem.allocEnv(attrs.size()));
    env2.up = &env;

    Env * inheritEnv = inheritFromExprs ? buildInheritFromEnv(state, env2) : nullptr;

    /* The recursive attributes are evaluated in the new environment,
       while the inherited attributes are evaluated in the original
       environment. */
    Displacement displ = 0;
    for (auto & i : attrs) {
        env2.values[displ++] = i.second.e->maybeThunk(
            state,
            *i.second.chooseByKind(&env2, &env, inheritEnv));
    }

    auto dts = state.ctx.debug
        ? makeDebugTraceStacker(
            state,
            *this,
            env2,
            getPos()
                ? std::make_shared<Pos>(state.ctx.positions[getPos()])
                : nullptr,
            "while evaluating a '%1%' expression",
            "let"
        )
        : nullptr;

    body->eval(state, env2, v);
}


void ExprList::eval(EvalState & state, Env & env, Value & v)
{
    v = state.ctx.mem.newList(elems.size());
    for (auto [n, v2] : enumerate(v.listItems()))
        const_cast<Value * &>(v2) = elems[n]->maybeThunk(state, env);
}


Value * ExprList::maybeThunk(EvalState & state, Env & env)
{
    if (elems.empty()) {
        return &Value::EMPTY_LIST;
    }
    return Expr::maybeThunk(state, env);
}


void ExprVar::eval(EvalState & state, Env & env, Value & v)
{
    Value * v2 = state.lookupVar(&env, *this, false);
    try {
        state.forceValue(*v2, pos);
    } catch (Error & e) {
        /* `name` can be invalid if we are an ExprInheritFrom */
        if (name) {
            e.addTrace(
                state.ctx.positions[getPos()],
                "while evaluating %s",
                state.ctx.symbols[name]
            );
        }
        throw;
    }
    v = *v2;
}


static std::string showAttrPath(EvalState & state, Env & env, const AttrPath & attrPath)
{
    std::ostringstream out;
    bool first = true;
    for (auto & i : attrPath) {
        if (!first) out << '.'; else first = false;
        try {
            out << state.ctx.symbols[getName(i, state, env)];
        } catch (Error & e) {
            assert(!i.symbol);
            out << "\"${...}\"";
        }
    }
    return out.str();
}


void ExprSelect::eval(EvalState & state, Env & env, Value & v)
{
    Value vFirst;

    // Pointer to the current attrset Value in this select chain.
    Value * vCurrent = &vFirst;
    // Position for the current attrset Value in this select chain.
    PosIdx posCurrent;
    // Position for the current selector in this select chain.
    PosIdx posCurrentSyntax;

    try {
        e->eval(state, env, vFirst);
    } catch (Error & e) {
        assert(this->e != nullptr);
        e.addTrace(
            state.ctx.positions[getPos()],
            "while evaluating an expression to select '%s' on it",
            showAttrPath(state.ctx.symbols, this->attrPath)
        );
        throw;
    }

    try {
        auto dts = state.ctx.debug
            ? makeDebugTraceStacker(
                state,
                *this,
                env,
                state.ctx.positions[getPos()],
                "while evaluating the attribute '%1%'",
                showAttrPath(state, env, attrPath))
            : nullptr;

        for (auto const & [partIdx, currentAttrName] : enumerate(attrPath)) {
            state.ctx.stats.nrLookups++;

            Symbol const name = getName(currentAttrName, state, env);

            try {
                state.forceValue(*vCurrent, pos);
            } catch (Error & e) {
                e.addTrace(
                    state.ctx.positions[currentAttrName.pos],
                    "while evaluating an expression to select '%s' on it",
                    state.ctx.symbols[name]
                );
                throw;
            }

            if (vCurrent->type() != nAttrs) {

                // If we have an `or` provided default,
                // then this is allowed to not be an attrset.
                if (def != nullptr) {
                    this->def->eval(state, env, v);
                    return;
                }

                // Otherwise, we must type error.
                state.ctx.errors.make<TypeError>(
                    "expected a set but found %s: %s",
                    showType(*vCurrent),
                    ValuePrinter(state, *vCurrent, errorPrintOptions)
                ).addTrace(
                    currentAttrName.pos,
                    HintFmt("while selecting '%s'", state.ctx.symbols[name])
                ).debugThrow();
            }

            // Now that we know this is actually an attrset, try to find an attr
            // with the selected name.
            Bindings::iterator attrIt = vCurrent->attrs->find(name);
            if (attrIt == vCurrent->attrs->end()) {

                // If we have an `or` provided default, then we'll use that.
                if (def != nullptr) {
                    this->def->eval(state, env, v);
                    return;
                }

                // Otherwise, missing attr error.
                std::set<std::string> allAttrNames;
                for (auto const & attr : *vCurrent->attrs) {
                    allAttrNames.insert(state.ctx.symbols[attr.name]);
                }
                auto suggestions = Suggestions::bestMatches(allAttrNames, state.ctx.symbols[name]);
                state.ctx.errors.make<EvalError>("attribute '%s' missing", state.ctx.symbols[name])
                    .atPos(currentAttrName.pos)
                    .withSuggestions(suggestions)
                    .withFrame(env, *this)
                    .debugThrow();
            }

            // If we're here, then we successfully found the attribute.
            // Set our currently operated-on attrset to this one, and keep going.
            vCurrent = attrIt->value;
            posCurrent = attrIt->pos;
            posCurrentSyntax = currentAttrName.pos;
            if (state.ctx.stats.countCalls) state.ctx.stats.attrSelects[posCurrent]++;
        }

        state.forceValue(*vCurrent, (posCurrent ? posCurrent : posCurrentSyntax));

    } catch (Error & e) {
        auto pos2r = state.ctx.positions[posCurrent];
        if (pos2r && !std::get_if<Pos::Hidden>(&pos2r.origin))
            e.addTrace(pos2r, "while evaluating the attribute '%1%'",
                showAttrPath(state, env, attrPath));
        throw;
    }

    v = *vCurrent;
}


void ExprOpHasAttr::eval(EvalState & state, Env & env, Value & v)
{
    Value vTmp;
    Value * vAttrs = &vTmp;

    e->eval(state, env, vTmp);

    for (auto & i : attrPath) {
        state.forceValue(*vAttrs, getPos());
        Bindings::iterator j;
        auto name = getName(i, state, env);
        if (vAttrs->type() != nAttrs ||
            (j = vAttrs->attrs->find(name)) == vAttrs->attrs->end())
        {
            v.mkBool(false);
            return;
        } else {
            vAttrs = j->value;
        }
    }

    v.mkBool(true);
}


void ExprLambda::eval(EvalState & state, Env & env, Value & v)
{
    v.mkLambda(&env, this);
}

namespace {
/** Increments a count on construction and decrements on destruction.
 */
class CallDepth {
  size_t & count;
public:
  CallDepth(size_t & count) : count(count) {
    ++count;
  }
  ~CallDepth() {
    --count;
  }
};
};

struct FormalsMatch
{
    std::vector<SymbolStr> missing;
    std::vector<SymbolStr> unexpected;
    std::set<std::string> unused;
};

/** Matchup an attribute argument set to a lambda's formal arguments,
 * or return what arguments were required but not given, or given but not allowed.
 */
FormalsMatch matchupLambdaAttrs(EvalState & state, Env & env, Displacement & displ, AttrsPattern const & pattern, Bindings & attrs, SymbolTable & symbols)
{
    size_t attrsUsed = 0;

    FormalsMatch result;

    for (auto const & formal : pattern.formals) {

        // The attribute whose name matches the name of the formal we're matching up, if it exists.
        Attr const * matchingArg = attrs.get(formal.name);
        if (matchingArg) {
            attrsUsed += 1;
            env.values[displ] = matchingArg->value;
            displ += 1;

            // We're done here. Move on to the next formal.
            continue;
        }

        // The argument for this formal wasn't given.
        result.unused.insert(symbols[formal.name]);
        // If the formal has a default, use it.
        if (formal.def) {
            env.values[displ] = formal.def->maybeThunk(state, env);
            displ += 1;
        } else {
            // Otherwise, let our caller know what was missing.
            result.missing.push_back(symbols[formal.name]);
        }
    }

    // Check for unexpected extra arguments.
    if (!pattern.ellipsis && attrsUsed != attrs.size()) {
        // Return the first unexpected argument.
        for (Attr const & attr : attrs) {
            if (!pattern.has(attr.name)) {
                result.unexpected.push_back(symbols[attr.name]);
            }
        }
    }

    return result;
}

Env & SimplePattern::match(ExprLambda & lambda, EvalState & state, Env & up, Value * arg, const PosIdx pos)
{
    Env & env2(state.ctx.mem.allocEnv(1));
    env2.up = &up;
    env2.values[0] = arg;
    return env2;
}

Env & AttrsPattern::match(ExprLambda & lambda, EvalState & state, Env & up, Value * arg, const PosIdx pos)
{
    auto & ctx = state.ctx;

    Env & env2(ctx.mem.allocEnv(formals.size() + (name ? 1 : 0)));
    env2.up = &up;
    Displacement displ = 0;

    try {
        state.forceAttrs(*arg, lambda.pos, "while evaluating the value passed for the lambda argument");
    } catch (Error & e) {
        if (pos) e.addTrace(ctx.positions[pos], "from call site");
        throw;
    }

    if (name)
        env2.values[displ++] = arg;

    ///* For each formal argument, get the actual argument.  If
    //   there is no matching actual argument but the formal
    //   argument has a default, use the default. */
    auto const formalsMatch = matchupLambdaAttrs(
        state,
        env2,
        displ,
        *this,
        *arg->attrs,
        ctx.symbols
    );


    if (!formalsMatch.unexpected.empty() || !formalsMatch.missing.empty()) {
        Suggestions sug; // empty suggestions -> no suggestions
        if (!formalsMatch.unexpected.empty()) {
            // suggestions only for the first unexpected argument
            // TODO: suggestions for all unexpected arguments
            sug = Suggestions::bestMatches(formalsMatch.unused, formalsMatch.unexpected.front());
        }

        auto argFmt = [](const SymbolStr & argument) { return HintFmt("'%s'", argument); };

        [&](){
            if (formalsMatch.unexpected.empty() && !formalsMatch.missing.empty()) {
                return ctx.errors.make<TypeError>(
                    "function '%s' called without required argument%s %s", lambda.getName(ctx.symbols),
                    Uncolored((formalsMatch.missing.size() == 1) ? "" : "s"),
                    Uncolored(concatStringsCommaAnd(argFmt, formalsMatch.missing))
                );
            } else if (!formalsMatch.unexpected.empty() && formalsMatch.missing.empty()) {
                return ctx.errors.make<TypeError>(
                    "function '%s' called with unexpected argument%s %s", lambda.getName(ctx.symbols),
                    Uncolored((formalsMatch.unexpected.size() == 1) ? "" : "s"),
                    Uncolored(concatStringsCommaAnd(argFmt, formalsMatch.unexpected))
                );
            } else {
                return ctx.errors.make<TypeError>(
                    "function '%s' called without required argument%s %s and with unexpected argument%s %s", lambda.getName(ctx.symbols),
                    Uncolored((formalsMatch.missing.size() == 1) ? "" : "s"),
                    Uncolored(concatStringsCommaAnd(argFmt, formalsMatch.missing)),
                    Uncolored((formalsMatch.unexpected.size() == 1) ? "" : "s"),
                    Uncolored(concatStringsCommaAnd(argFmt, formalsMatch.unexpected))
                );
            }
        }().atPos(lambda.pos)
            .withTrace(pos, "from call site")
            .withSuggestions(sug)
            .withFrame(up, lambda)
            .debugThrow();
    }

    return env2;
}

void EvalState::callFunction(Value & fun, size_t nrArgs, Value * * args, Value & vRes, const PosIdx pos)
{
    if (callDepth > evalSettings.maxCallDepth)
        ctx.errors.make<EvalError>("stack overflow; max-call-depth exceeded").atPos(pos).debugThrow();
    CallDepth _level(callDepth);

    auto trace = evalSettings.traceFunctionCalls
        ? std::make_unique<FunctionCallTrace>(ctx.positions[pos])
        : nullptr;

    forceValue(fun, pos);

    Value vCur(fun);

    auto makeAppChain = [&]()
    {
        vRes = vCur;
        for (size_t i = 0; i < nrArgs; ++i) {
            auto fun2 = ctx.mem.allocValue();
            *fun2 = vRes;
            vRes.mkPrimOpApp(fun2, args[i]);
        }
    };

    Attr * functor;

    while (nrArgs > 0) {

        if (vCur.isLambda()) {

            ExprLambda & lambda(*vCur.lambda.fun);

            Env & env2 = lambda.pattern->match(lambda, *this, *vCur.lambda.env, args[0], pos);

            ctx.stats.nrFunctionCalls++;
            if (ctx.stats.countCalls) ctx.stats.addCall(lambda);

            /* Evaluate the body. */
            try {
                auto dts = ctx.debug
                    ? makeDebugTraceStacker(
                        *this, *lambda.body, env2, ctx.positions[lambda.pos],
                        "while calling %s",
                        lambda.getQuotedName(ctx.symbols))
                    : nullptr;

                lambda.body->eval(*this, env2, vCur);
            } catch (Error & e) {
                if (loggerSettings.showTrace.get()) {
                    e.addTrace(
                        ctx.positions[lambda.pos],
                        "while calling %s",
                        lambda.getQuotedName(ctx.symbols));
                    if (pos) e.addTrace(ctx.positions[pos], "from call site");
                }
                throw;
            }

            nrArgs--;
            args += 1;
        }

        else if (vCur.isPrimOp()) {

            size_t argsLeft = vCur.primOp->arity;

            if (nrArgs < argsLeft) {
                /* We don't have enough arguments, so create a tPrimOpApp chain. */
                makeAppChain();
                return;
            } else {
                /* We have all the arguments, so call the primop. */
                auto * fn = vCur.primOp;

                ctx.stats.nrPrimOpCalls++;
                if (ctx.stats.countCalls) ctx.stats.primOpCalls[fn->name]++;

                try {
                    fn->fun(*this, noPos, args, vCur);
                } catch (ThrownError & e) {
                    // Distinguish between an error that simply happened while "throw"
                    // was being evaluated and an explicit thrown error.
                    if (fn->name == "throw") {
                        e.addTrace(ctx.positions[pos], "caused by explicit %s", "throw");
                    } else {
                        e.addTrace(ctx.positions[pos], "while calling the '%s' builtin", fn->name);
                    }
                    throw;
                } catch (Error & e) {
                    e.addTrace(ctx.positions[pos], "while calling the '%1%' builtin", fn->name);
                    throw;
                }

                nrArgs -= argsLeft;
                args += argsLeft;
            }
        }

        else if (vCur.isPrimOpApp()) {
            /* Figure out the number of arguments still needed. */
            size_t argsDone = 0;
            Value * primOp = &vCur;
            while (primOp->isPrimOpApp()) {
                argsDone++;
                primOp = primOp->primOpApp.left;
            }
            assert(primOp->isPrimOp());
            auto arity = primOp->primOp->arity;
            auto argsLeft = arity - argsDone;

            if (nrArgs < argsLeft) {
                /* We still don't have enough arguments, so extend the tPrimOpApp chain. */
                makeAppChain();
                return;
            } else {
                /* We have all the arguments, so call the primop with
                   the previous and new arguments. */

                // max arity as of writing is 3. even 4 seems excessive though.
                SmallVector<Value *, 4> vArgs(arity);
                auto n = argsDone;
                for (Value * arg = &vCur; arg->isPrimOpApp(); arg = arg->primOpApp.left)
                    vArgs[--n] = arg->primOpApp.right;

                for (size_t i = 0; i < argsLeft; ++i)
                    vArgs[argsDone + i] = args[i];

                auto fn = primOp->primOp;
                ctx.stats.nrPrimOpCalls++;
                if (ctx.stats.countCalls) ctx.stats.primOpCalls[fn->name]++;

                try {
                    // TODO:
                    // 1. Unify this and above code. Heavily redundant.
                    // 2. Create a fake env (arg1, arg2, etc.) and a fake expr (arg1: arg2: etc: builtins.name arg1 arg2 etc)
                    //    so the debugger allows to inspect the wrong parameters passed to the builtin.
                    fn->fun(*this, noPos, vArgs.data(), vCur);
                } catch (Error & e) {
                    e.addTrace(ctx.positions[pos], "while calling the '%1%' builtin", fn->name);
                    throw;
                }

                nrArgs -= argsLeft;
                args += argsLeft;
            }
        }

        else if (vCur.type() == nAttrs && (functor = vCur.attrs->get(ctx.s.functor))) {
            /* 'vCur' may be allocated on the stack of the calling
               function, but for functors we may keep a reference, so
               heap-allocate a copy and use that instead. */
            Value * args2[] = {ctx.mem.allocValue(), args[0]};
            *args2[0] = vCur;
            try {
                callFunction(*functor->value, 2, args2, vCur, functor->pos);
            } catch (Error & e) {
                e.addTrace(ctx.positions[pos], "while calling a functor (an attribute set with a '__functor' attribute)");
                throw;
            }
            nrArgs--;
            args++;
        }

        else
            ctx.errors.make<TypeError>(
                    "attempt to call something which is not a function but %1%: %2%",
                    showType(vCur),
                    ValuePrinter(*this, vCur, errorPrintOptions))
                .atPos(pos)
                .debugThrow();
    }

    vRes = vCur;
}


void ExprCall::eval(EvalState & state, Env & env, Value & v)
{
    auto dts = state.ctx.debug
        ? makeDebugTraceStacker(
            state,
            *this,
            env,
            getPos()
                ? std::make_shared<Pos>(state.ctx.positions[getPos()])
                : nullptr,
            "while calling a function"
        )
        : nullptr;

    Value vFun;
    fun->eval(state, env, vFun);

    // Empirical arity of Nixpkgs lambdas by regex e.g. ([a-zA-Z]+:(\s|(/\*.*\/)|(#.*\n))*){5}
    // 2: over 4000
    // 3: about 300
    // 4: about 60
    // 5: under 10
    // This excluded attrset lambdas (`{...}:`). Contributions of mixed lambdas appears insignificant at ~150 total.
    SmallValueVector<4> vArgs(args.size());
    for (size_t i = 0; i < args.size(); ++i)
        vArgs[i] = args[i]->maybeThunk(state, env);

    state.callFunction(vFun, args.size(), vArgs.data(), v, pos);
}


// Lifted out of callFunction() because it creates a temporary that
// prevents tail-call optimisation.
void EvalStatistics::addCall(ExprLambda & fun)
{
    functionCalls[&fun]++;
}


void EvalState::autoCallFunction(Bindings & args, Value & fun, Value & res)
{
    auto pos = fun.determinePos(noPos);

    forceValue(fun, pos);

    if (fun.type() == nAttrs) {
        auto found = fun.attrs->find(ctx.s.functor);
        if (found != fun.attrs->end()) {
            Value * v = ctx.mem.allocValue();
            callFunction(*found->value, fun, *v, pos);
            forceValue(*v, pos);
            return autoCallFunction(args, *v, res);
        }
    }

    if (!fun.isLambda()) {
        res = fun;
        return;
    }
    auto pattern = dynamic_cast<AttrsPattern *>(fun.lambda.fun->pattern.get());
    if (!pattern) {
        res = fun;
        return;
    }

    auto attrs = ctx.buildBindings(std::max(static_cast<uint32_t>(pattern->formals.size()), args.size()));

    if (pattern->ellipsis) {
        // If the formals have an ellipsis (eg the function accepts extra args) pass
        // all available automatic arguments (which includes arguments specified on
        // the command line via --arg/--argstr)
        for (auto & v : args)
            attrs.insert(v);
    } else {
        // Otherwise, only pass the arguments that the function accepts
        for (auto & i : pattern->formals) {
            Bindings::iterator j = args.find(i.name);
            if (j != args.end()) {
                attrs.insert(*j);
            } else if (!i.def) {
                ctx.errors.make<MissingArgumentError>(R"(cannot evaluate a function that has an argument without a value ('%1%')
Lix attempted to evaluate a function as a top level expression; in
this case it must have its arguments supplied either by default
values, or passed explicitly with '--arg' or '--argstr'. See
https://docs.lix.systems/manual/lix/stable/language/constructs.html#functions)", ctx.symbols[i.name])
                    .atPos(i.pos).withFrame(*fun.lambda.env, *fun.lambda.fun).debugThrow();
            }
        }
    }

    callFunction(fun, ctx.mem.allocValue()->mkAttrs(attrs), res, pos);
}


void ExprWith::eval(EvalState & state, Env & env, Value & v)
{
    Env & env2(state.ctx.mem.allocEnv(1));
    env2.up = &env;
    env2.values[0] = attrs->maybeThunk(state, env);

    body->eval(state, env2, v);
}


void ExprIf::eval(EvalState & state, Env & env, Value & v)
{
    (state.evalBool(env, *cond) ? *then : *else_).eval(state, env, v);
}


void ExprAssert::eval(EvalState & state, Env & env, Value & v)
{
    if (!state.evalBool(env, *cond)) {
        state.ctx.errors.make<AssertionError>("assertion failed")
            .atPos(pos)
            .withFrame(env, *this)
            .debugThrow();
    }
    body->eval(state, env, v);
}


void ExprOpNot::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(!state.evalBool(env, *e));
}


void ExprOpEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    v.mkBool(state.eqValues(v1, v2, pos, "while testing two values for equality"));
}


void ExprOpNEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    v.mkBool(!state.eqValues(v1, v2, pos, "while testing two values for inequality"));
}


void ExprOpAnd::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(state.evalBool(env, *e1) && state.evalBool(env, *e2));
}


void ExprOpOr::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(state.evalBool(env, *e1) || state.evalBool(env, *e2));
}


void ExprOpImpl::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(!state.evalBool(env, *e1) || state.evalBool(env, *e2));
}


void ExprOpUpdate::eval(EvalState & state, Env & env, Value & v)
{
    Value v1, v2;
    state.evalAttrs(env, *e1, v1);
    state.evalAttrs(env, *e2, v2);

    state.ctx.stats.nrOpUpdates++;

    if (v1.attrs->size() == 0) { v = v2; return; }
    if (v2.attrs->size() == 0) { v = v1; return; }

    auto attrs = state.ctx.buildBindings(v1.attrs->size() + v2.attrs->size());

    /* Merge the sets, preferring values from the second set.  Make
       sure to keep the resulting vector in sorted order. */
    Bindings::iterator i = v1.attrs->begin();
    Bindings::iterator j = v2.attrs->begin();

    while (i != v1.attrs->end() && j != v2.attrs->end()) {
        if (i->name == j->name) {
            attrs.insert(*j);
            ++i; ++j;
        }
        else if (i->name < j->name)
            attrs.insert(*i++);
        else
            attrs.insert(*j++);
    }

    while (i != v1.attrs->end()) attrs.insert(*i++);
    while (j != v2.attrs->end()) attrs.insert(*j++);

    v.mkAttrs(attrs.alreadySorted());

    state.ctx.stats.nrOpUpdateValuesCopied += v.attrs->size();
}


void ExprOpConcatLists::eval(EvalState & state, Env & env, Value & v)
{
    state.ctx.stats.nrListConcats++;

    /* We don't call into `concatLists` as that loses the position information of the expressions. */

    Value v1; state.evalList(env, *e1, v1);
    Value v2; state.evalList(env, *e2, v2);

    size_t l1 = v1.listSize(), l2 = v2.listSize(), len = l1 + l2;

    if (l1 == 0)
        v = v2;
    else if (l2 == 0)
        v = v1;
    else {
        v = state.ctx.mem.newList(len);
        auto out = v.listElems();
        std::copy(v1.listElems(), v1.listElems() + l1, out);
        std::copy(v2.listElems(), v2.listElems() + l2, out + l1);
    }
}


void EvalState::concatLists(Value & v, size_t nrLists, Value * * lists, const PosIdx pos, std::string_view errorCtx)
{
    ctx.stats.nrListConcats++;

    Value * nonEmpty = 0;
    size_t len = 0;
    for (size_t n = 0; n < nrLists; ++n) {
        forceList(*lists[n], pos, errorCtx);
        auto l = lists[n]->listSize();
        len += l;
        if (l) nonEmpty = lists[n];
    }

    if (nonEmpty && len == nonEmpty->listSize()) {
        v = *nonEmpty;
        return;
    }

    v = ctx.mem.newList(len);
    auto out = v.listElems();
    for (size_t n = 0, pos = 0; n < nrLists; ++n) {
        auto l = lists[n]->listSize();
        if (l)
            memcpy(out + pos, lists[n]->listElems(), l * sizeof(Value *));
        pos += l;
    }
}


void ExprConcatStrings::eval(EvalState & state, Env & env, Value & v)
{
    NixStringContext context;
    std::vector<BackedStringView> s;
    size_t sSize = 0;
    NixInt n{0};
    NixFloat nf = 0;

    bool first = !forceString;
    ValueType firstType = nString;

    const auto str = [&] {
        std::string result;
        result.reserve(sSize);
        for (const auto & part : s) result += *part;
        return result;
    };
    /* c_str() is not str().c_str() because we want to create a string
       Value. allocating a GC'd string directly and moving it into a
       Value lets us avoid an allocation and copy. */
    const auto c_str = [&] {
        char * result = gcAllocString(sSize + 1);
        char * tmp = result;
        for (const auto & part : s) {
            memcpy(tmp, part->data(), part->size());
            tmp += part->size();
        }
        *tmp = 0;
        return result;
    };

    // List of returned strings. References to these Values must NOT be persisted.
    SmallTemporaryValueVector<conservativeStackReservation> values(es.size());
    Value * vTmpP = values.data();

    for (auto & [i_pos, i] : es) {
        Value & vTmp = *vTmpP++;
        i->eval(state, env, vTmp);

        /* If the first element is a path, then the result will also
           be a path, we don't copy anything (yet - that's done later,
           since paths are copied when they are used in a derivation),
           and none of the strings are allowed to have contexts. */
        if (first) {
            firstType = vTmp.type();
        }

        if (firstType == nInt) {
            if (vTmp.type() == nInt) {
                auto newN = n + vTmp.integer;
                if (auto checked = newN.valueChecked(); checked.has_value()) {
                    n = NixInt(*checked);
                } else {
                    state.ctx.errors.make<EvalError>("integer overflow in adding %1% + %2%", n, vTmp.integer).atPos(i_pos).debugThrow();
                }
            } else if (vTmp.type() == nFloat) {
                // Upgrade the type from int to float;
                firstType = nFloat;
                nf = n.value;
                nf += vTmp.fpoint;
            } else
                state.ctx.errors.make<EvalError>("cannot add %1% to an integer", showType(vTmp)).atPos(i_pos).withFrame(env, *this).debugThrow();
        } else if (firstType == nFloat) {
            if (vTmp.type() == nInt) {
                nf += vTmp.integer.value;
            } else if (vTmp.type() == nFloat) {
                nf += vTmp.fpoint;
            } else
                state.ctx.errors.make<EvalError>("cannot add %1% to a float", showType(vTmp)).atPos(i_pos).withFrame(env, *this).debugThrow();
        } else {
            if (s.empty()) s.reserve(es.size());
            /* skip canonization of first path, which would only be not
            canonized in the first place if it's coming from a ./${foo} type
            path */
            auto part = state.coerceToString(i_pos, vTmp, context,
                                             "while evaluating a path segment",
                                             false, firstType == nString, !first);
            sSize += part->size();
            s.emplace_back(std::move(part));
        }

        first = false;
    }

    if (firstType == nInt)
        v.mkInt(n);
    else if (firstType == nFloat)
        v.mkFloat(nf);
    else if (firstType == nPath) {
        if (!context.empty())
            state.ctx.errors.make<EvalError>("a string that refers to a store path cannot be appended to a path").atPos(pos).withFrame(env, *this).debugThrow();
        v.mkPath(CanonPath(canonPath(str())));
    } else
        v.mkStringMove(c_str(), context);
}


void ExprPos::eval(EvalState & state, Env & env, Value & v)
{
    state.mkPos(v, pos);
}


void ExprBlackHole::eval(EvalState & state, Env & env, Value & v)
{
    state.ctx.errors.make<InfiniteRecursionError>("infinite recursion encountered")
        .atPos(v.determinePos(noPos))
        .debugThrow();
}

// always force this to be separate, otherwise forceValue may inline it and take
// a massive perf hit
[[gnu::noinline]]
void EvalState::tryFixupBlackHolePos(Value & v, PosIdx pos)
{
    if (!v.isBlackhole())
        return;
    auto e = std::current_exception();
    try {
        std::rethrow_exception(e);
    } catch (InfiniteRecursionError & e) {
        e.atPos(ctx.positions[pos]);
    } catch (...) {
    }
}


void EvalState::forceValueDeep(Value & v)
{
    std::set<const Value *> seen;

    std::function<void(Value & v)> recurse;

    recurse = [&](Value & v) {
        if (!seen.insert(&v).second) return;

        forceValue(v, noPos);

        if (v.type() == nAttrs) {
            for (auto & i : *v.attrs)
                try {
                    // If the value is a thunk, we're evaling. Otherwise no trace necessary.
                    auto dts = ctx.debug && i.value->isThunk()
                        ? makeDebugTraceStacker(*this, *i.value->thunk.expr, *i.value->thunk.env, ctx.positions[i.pos],
                            "while evaluating the attribute '%1%'", ctx.symbols[i.name])
                        : nullptr;

                    recurse(*i.value);
                } catch (Error & e) {
                    e.addTrace(ctx.positions[i.pos], "while evaluating the attribute '%1%'", ctx.symbols[i.name]);
                    throw;
                }
        }

        else if (v.isList()) {
            for (auto v2 : v.listItems())
                recurse(*v2);
        }
    };

    recurse(v);
}


NixInt EvalState::forceInt(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() != nInt)
            ctx.errors.make<TypeError>(
                "expected an integer but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
        return v.integer;
    } catch (Error & e) {
        e.addTrace(ctx.positions[pos], errorCtx);
        throw;
    }

    return v.integer;
}


NixFloat EvalState::forceFloat(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() == nInt)
            return v.integer.value;
        else if (v.type() != nFloat)
            ctx.errors.make<TypeError>(
                "expected a float but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
        return v.fpoint;
    } catch (Error & e) {
        e.addTrace(ctx.positions[pos], errorCtx);
        throw;
    }
}


bool EvalState::forceBool(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() != nBool)
            ctx.errors.make<TypeError>(
                "expected a Boolean but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
        return v.boolean;
    } catch (Error & e) {
        e.addTrace(ctx.positions[pos], errorCtx);
        throw;
    }

    return v.boolean;
}


bool EvalState::isFunctor(Value & fun)
{
    return fun.type() == nAttrs && fun.attrs->find(ctx.s.functor) != fun.attrs->end();
}


void EvalState::forceFunction(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() != nFunction && !isFunctor(v))
            ctx.errors.make<TypeError>(
                "expected a function but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
    } catch (Error & e) {
        e.addTrace(ctx.positions[pos], errorCtx);
        throw;
    }
}


std::string_view EvalState::forceString(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() != nString)
            ctx.errors.make<TypeError>(
                "expected a string but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
        return v.string.s;
    } catch (Error & e) {
        e.addTrace(ctx.positions[pos], errorCtx);
        throw;
    }
}


void copyContext(const Value & v, NixStringContext & context)
{
    if (v.string.context)
        for (const char * * p = v.string.context; *p; ++p)
            context.insert(NixStringContextElem::parse(*p));
}


std::string_view EvalState::forceString(Value & v, NixStringContext & context, const PosIdx pos, std::string_view errorCtx)
{
    auto s = forceString(v, pos, errorCtx);
    copyContext(v, context);
    return s;
}


std::string_view EvalState::forceStringNoCtx(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    auto s = forceString(v, pos, errorCtx);
    if (v.string.context) {
        ctx.errors.make<EvalError>("the string '%1%' is not allowed to refer to a store path (such as '%2%')", v.string.s, v.string.context[0]).withTrace(pos, errorCtx).debugThrow();
    }
    return s;
}


bool EvalState::isDerivation(Value & v)
{
    if (v.type() != nAttrs) return false;
    Bindings::iterator i = v.attrs->find(ctx.s.type);
    if (i == v.attrs->end()) return false;
    forceValue(*i->value, i->pos);
    if (i->value->type() != nString) return false;
    return strcmp(i->value->string.s, "derivation") == 0;
}


std::optional<std::string> EvalState::tryAttrsToString(const PosIdx pos, Value & v,
    NixStringContext & context, bool coerceMore, bool copyToStore)
{
    auto i = v.attrs->find(ctx.s.toString);
    if (i != v.attrs->end()) {
        Value v1;
        try {
            callFunction(*i->value, v, v1, i->pos);
            return coerceToString(pos, v1, context,
                    "while evaluating the result of the `__toString` attribute",
                    coerceMore, copyToStore).toOwned();
        } catch (EvalError & e) {
            e.addTrace(ctx.positions[pos], "while converting a set to string");
            throw;
        }
    }

    return {};
}

BackedStringView EvalState::coerceToString(
    const PosIdx pos,
    Value & v,
    NixStringContext & context,
    std::string_view errorCtx,
    bool coerceMore,
    bool copyToStore,
    bool canonicalizePath)
{
    forceValue(v, pos);

    if (v.type() == nString) {
        copyContext(v, context);
        return std::string_view(v.string.s);
    }

    if (v.type() == nPath) {
        return
            !canonicalizePath && !copyToStore
            ? // FIXME: hack to preserve path literals that end in a
              // slash, as in /foo/${x}.
              v._path
            : copyToStore
            ? ctx.store->printStorePath(
                aio.blockOn(ctx.paths.copyPathToStore(context, v.path(), ctx.repair)).unwrap())
            : v.path().to_string();
    }

    if (v.type() == nAttrs) {
        auto maybeString = tryAttrsToString(pos, v, context, coerceMore, copyToStore);
        if (maybeString)
            return std::move(*maybeString);
        auto i = v.attrs->find(ctx.s.outPath);
        if (i == v.attrs->end()) {
            ctx.errors.make<TypeError>(
                "cannot coerce %1% to a string: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            )
                .withTrace(pos, errorCtx)
                .debugThrow();
        }
        return coerceToString(pos, *i->value, context, errorCtx,
                              coerceMore, copyToStore, canonicalizePath);
    }

    if (v.type() == nExternal) {
        try {
            return v.external->coerceToString(*this, pos, context, coerceMore, copyToStore);
        } catch (Error & e) {
            e.addTrace(nullptr, errorCtx);
            throw;
        }
    }

    if (coerceMore) {
        /* Note that `false' is represented as an empty string for
           shell scripting convenience, just like `null'. */
        if (v.type() == nBool && v.boolean) return "1";
        if (v.type() == nBool && !v.boolean) return "";
        if (v.type() == nInt) return std::to_string(v.integer.value);
        if (v.type() == nFloat) return std::to_string(v.fpoint);
        if (v.type() == nNull) return "";

        if (v.isList()) {
            std::string result;
            for (auto [n, v2] : enumerate(v.listItems())) {
                try {
                    result += *coerceToString(pos, *v2, context,
                            "while evaluating one element of the list",
                            coerceMore, copyToStore, canonicalizePath);
                } catch (Error & e) {
                    e.addTrace(ctx.positions[pos], errorCtx);
                    throw;
                }
                if (n < v.listSize() - 1
                    /* !!! not quite correct */
                    && (!v2->isList() || v2->listSize() != 0))
                    result += " ";
            }
            return result;
        }
    }

    ctx.errors.make<TypeError>("cannot coerce %1% to a string: %2%",
        showType(v),
        ValuePrinter(*this, v, errorPrintOptions)
    )
        .withTrace(pos, errorCtx)
        .debugThrow();
}


kj::Promise<Result<EvalPaths::PathResult<StorePath, EvalError>>>
EvalPaths::copyPathToStore(NixStringContext & context, const SourcePath & path, RepairFlag repair)
try {
    if (nix::isDerivation(path.canonical().abs()))
        co_return errors.make<EvalError>("file names are not allowed to end in '%1%'", drvExtension);

    auto i = srcToStore.find(path);

    auto dstPath = i != srcToStore.end()
        ? i->second
        : ({
            auto dstPath = TRY_AWAIT(fetchToStoreRecursive(
                *store,
                *prepareDump(checkSourcePath(path).canonical().abs()),
                path.baseName(),
                repair
            ));
            allowPath(dstPath);
            srcToStore.insert_or_assign(path, dstPath);
            printMsg(lvlChatty, "copied source '%1%' -> '%2%'", path, store->printStorePath(dstPath));
            std::move(dstPath);
        });

    context.insert(NixStringContextElem::Opaque {
        .path = dstPath
    });
    co_return dstPath;
} catch (...) {
    co_return result::current_exception();
}


SourcePath EvalState::coerceToPath(const PosIdx pos, Value & v, NixStringContext & context, std::string_view errorCtx)
{
    auto path = coerceToString(pos, v, context, errorCtx, false, false, true).toOwned();
    if (path == "" || path[0] != '/')
        ctx.errors.make<EvalError>("string '%1%' doesn't represent an absolute path", path).withTrace(pos, errorCtx).debugThrow();
    return CanonPath(path);
}


StorePath EvalState::coerceToStorePath(const PosIdx pos, Value & v, NixStringContext & context, std::string_view errorCtx)
{
    auto path = coerceToString(pos, v, context, errorCtx, false, false, true).toOwned();
    if (auto storePath = ctx.store->maybeParseStorePath(path))
        return *storePath;
    ctx.errors.make<EvalError>("path '%1%' is not in the Nix store", path).withTrace(pos, errorCtx).debugThrow();
}


std::pair<SingleDerivedPath, std::string_view> EvalState::coerceToSingleDerivedPathUnchecked(const PosIdx pos, Value & v, std::string_view errorCtx)
{
    NixStringContext context;
    auto s = forceString(v, context, pos, errorCtx);
    auto csize = context.size();
    if (csize != 1)
        ctx.errors.make<EvalError>(
            "string '%s' has %d entries in its context. It should only have exactly one entry",
            s, csize)
            .withTrace(pos, errorCtx).debugThrow();
    auto derivedPath = std::visit(overloaded {
        [&](NixStringContextElem::Opaque && o) -> SingleDerivedPath {
            return std::move(o);
        },
        [&](NixStringContextElem::DrvDeep &&) -> SingleDerivedPath {
            ctx.errors.make<EvalError>(
                "string '%s' has a context which refers to a complete source and binary closure. This is not supported at this time",
                s).withTrace(pos, errorCtx).debugThrow(always_progresses);
        },
        [&](NixStringContextElem::Built && b) -> SingleDerivedPath {
            return std::move(b);
        },
    }, ((NixStringContextElem &&) *context.begin()).raw);
    return {
        std::move(derivedPath),
        std::move(s),
    };
}


SingleDerivedPath EvalState::coerceToSingleDerivedPath(const PosIdx pos, Value & v, std::string_view errorCtx)
{
    auto [derivedPath, s_] = coerceToSingleDerivedPathUnchecked(pos, v, errorCtx);
    auto s = s_;
    auto sExpected = mkSingleDerivedPathStringRaw(derivedPath);
    if (s != sExpected) {
        /* `std::visit` is used here just to provide a more precise
           error message. */
        std::visit(overloaded {
            [&](const SingleDerivedPath::Opaque & o) {
                ctx.errors.make<EvalError>(
                    "path string '%s' has context with the different path '%s'",
                    s, sExpected)
                    .withTrace(pos, errorCtx).debugThrow(always_progresses);
            },
            [&](const SingleDerivedPath::Built & b) {
                ctx.errors.make<EvalError>(
                    "string '%s' has context with the output '%s' from derivation '%s', but the string is not the right placeholder for this derivation output. It should be '%s'",
                    s, b.output, b.drvPath->to_string(*ctx.store), sExpected)
                    .withTrace(pos, errorCtx).debugThrow(always_progresses);
            }
        }, derivedPath.raw());
    }
    return derivedPath;
}


bool EvalState::eqValues(Value & v1, Value & v2, const PosIdx pos, std::string_view errorCtx)
{
    forceValue(v1, pos);
    forceValue(v2, pos);

    /* !!! Hack to support some old broken code that relies on pointer
       equality tests between sets.  (Specifically, builderDefs calls
       uniqList on a list of sets.)  Will remove this eventually. */
    if (&v1 == &v2) return true;

    // Special case type-compatibility between float and int
    if (v1.type() == nInt && v2.type() == nFloat)
        return v1.integer.value == v2.fpoint;
    if (v1.type() == nFloat && v2.type() == nInt)
        return v1.fpoint == v2.integer.value;

    // All other types are not compatible with each other.
    if (v1.type() != v2.type()) return false;

    switch (v1.type()) {
        case nInt:
            return v1.integer == v2.integer;

        case nBool:
            return v1.boolean == v2.boolean;

        case nString:
            return strcmp(v1.string.s, v2.string.s) == 0;

        case nPath:
            return strcmp(v1._path, v2._path) == 0;

        case nNull:
            return true;

        case nList:
            if (v1.listSize() != v2.listSize()) return false;
            for (size_t n = 0; n < v1.listSize(); ++n)
                if (!eqValues(*v1.listElems()[n], *v2.listElems()[n], pos, errorCtx)) return false;
            return true;

        case nAttrs: {
            /* If both sets denote a derivation (type = "derivation"),
               then compare their outPaths. */
            if (isDerivation(v1) && isDerivation(v2)) {
                Bindings::iterator i = v1.attrs->find(ctx.s.outPath);
                Bindings::iterator j = v2.attrs->find(ctx.s.outPath);
                if (i != v1.attrs->end() && j != v2.attrs->end())
                    return eqValues(*i->value, *j->value, pos, errorCtx);
            }

            if (v1.attrs->size() != v2.attrs->size()) return false;

            /* Otherwise, compare the attributes one by one. */
            Bindings::iterator i, j;
            for (i = v1.attrs->begin(), j = v2.attrs->begin(); i != v1.attrs->end(); ++i, ++j)
                if (i->name != j->name || !eqValues(*i->value, *j->value, pos, errorCtx))
                    return false;

            return true;
        }

        /* Functions are incomparable. */
        case nFunction:
            return false;

        case nExternal:
            return *v1.external == *v2.external;

        case nFloat:
            return v1.fpoint == v2.fpoint;

        case nThunk: // Must not be left by forceValue
        default:
            ctx.errors.make<EvalError>("cannot compare %1% with %2%", showType(v1), showType(v2)).withTrace(pos, errorCtx).debugThrow();
    }
}

bool Evaluator::fullGC() {
#if HAVE_BOEHMGC
    GC_gcollect();
    // Check that it ran. We might replace this with a version that uses more
    // of the boehm API to get this reliably, at a maintenance cost.
    // We use a 1K margin because technically this has a race condtion, but we
    // probably won't encounter it in practice, because the CLI isn't concurrent
    // like that.
    return GC_get_bytes_since_gc() < 1024;
#else
    return false;
#endif
}

void Evaluator::maybePrintStats()
{
    bool showStats = getEnv("NIX_SHOW_STATS").value_or("0") != "0";

    if (showStats) {
        // Make the final heap size more deterministic.
#if HAVE_BOEHMGC
        if (!fullGC()) {
            warn("failed to perform a full GC before reporting stats");
        }
#endif
        printStatistics();
    }
}

void Evaluator::printStatistics()
{
    struct rusage buf;
    getrusage(RUSAGE_SELF, &buf);
    float cpuTime = buf.ru_utime.tv_sec + ((float) buf.ru_utime.tv_usec / 1000000);
    auto mem = this->mem.getStats();

    uint64_t bEnvs = mem.nrEnvs * sizeof(Env) + mem.nrValuesInEnvs * sizeof(Value *);
    uint64_t bLists = mem.nrListElems * sizeof(Value *);
    uint64_t bValues = mem.nrValues * sizeof(Value);
    uint64_t bAttrsets = mem.nrAttrsets * sizeof(Bindings) + mem.nrAttrsInAttrsets * sizeof(Attr);

#if HAVE_BOEHMGC
    GC_word heapSize, totalBytes;
    GC_get_heap_usage_safe(&heapSize, 0, 0, 0, &totalBytes);
#endif

    auto outPath = getEnv("NIX_SHOW_STATS_PATH").value_or("-");
    std::fstream fs;
    if (outPath != "-")
        fs.open(outPath, std::fstream::out);
    JSON topObj = JSON::object();
    topObj["cpuTime"] = cpuTime;
    topObj["envs"] = {
        {"number", mem.nrEnvs},
        {"elements", mem.nrValuesInEnvs},
        {"bytes", bEnvs},
    };
    topObj["list"] = {
        {"elements", mem.nrListElems},
        {"bytes", bLists},
        {"concats", stats.nrListConcats},
    };
    topObj["values"] = {
        {"number", mem.nrValues},
        {"bytes", bValues},
    };
    topObj["symbols"] = {
        {"number", symbols.size()},
        {"bytes", symbols.totalSize()},
    };
    topObj["sets"] = {
        {"number", mem.nrAttrsets},
        {"bytes", bAttrsets},
        {"elements", mem.nrAttrsInAttrsets},
    };
    topObj["sizes"] = {
        {"Env", sizeof(Env)},
        {"Value", sizeof(Value)},
        {"Bindings", sizeof(Bindings)},
        {"Attr", sizeof(Attr)},
    };
    topObj["nrOpUpdates"] = stats.nrOpUpdates;
    topObj["nrOpUpdateValuesCopied"] = stats.nrOpUpdateValuesCopied;
    topObj["nrThunks"] = stats.nrThunks;
    topObj["nrAvoided"] = stats.nrAvoided;
    topObj["nrLookups"] = stats.nrLookups;
    topObj["nrPrimOpCalls"] = stats.nrPrimOpCalls;
    topObj["nrFunctionCalls"] = stats.nrFunctionCalls;
#if HAVE_BOEHMGC
    topObj["gc"] = {
        {"heapSize", heapSize},
        {"totalBytes", totalBytes},
    };
#endif

    if (stats.countCalls) {
        topObj["primops"] = stats.primOpCalls;
        {
            auto& list = topObj["functions"];
            list = JSON::array();
            for (auto & [fun, count] : stats.functionCalls) {
                JSON obj = JSON::object();
                if (fun->name)
                    obj["name"] = (std::string_view) symbols[fun->name];
                else
                    obj["name"] = nullptr;
                if (auto pos = positions[fun->pos]) {
                    if (auto path = std::get_if<CheckedSourcePath>(&pos.origin))
                        obj["file"] = path->to_string();
                    obj["line"] = pos.line;
                    obj["column"] = pos.column;
                }
                obj["count"] = count;
                list.push_back(obj);
            }
        }
        {
            auto list = topObj["attributes"];
            list = JSON::array();
            for (auto & i : stats.attrSelects) {
                JSON obj = JSON::object();
                if (auto pos = positions[i.first]) {
                    if (auto path = std::get_if<CheckedSourcePath>(&pos.origin))
                        obj["file"] = path->to_string();
                    obj["line"] = pos.line;
                    obj["column"] = pos.column;
                }
                obj["count"] = i.second;
                list.push_back(obj);
            }
        }
    }

    if (getEnv("NIX_SHOW_SYMBOLS").value_or("0") != "0") {
        // XXX: overrides earlier assignment
        topObj["symbols"] = JSON::array();
        auto &list = topObj["symbols"];
        symbols.dump([&](const std::string & s) { list.emplace_back(s); });
    }
    if (outPath == "-") {
        std::cerr << topObj.dump(2) << std::endl;
    } else {
        fs << topObj.dump(2) << std::endl;
    }
}


CheckedSourcePath EvalPaths::resolveExprPath(SourcePath path_)
{
    auto path = checkSourcePath(path_);
    unsigned int followCount = 0, maxFollow = 1024;

    /* If `path' is a symlink, follow it.  This is so that relative
       path references work. */
    while (true) {
        // Basic cycle/depth limit to avoid infinite loops.
        if (++followCount >= maxFollow)
            throw Error("too many symbolic links encountered while traversing the path '%s'", path);
        if (path.lstat().type != InputAccessor::tSymlink) break;
        path = checkSourcePath(
            CanonPath(path.readLink(), path.canonical().parent().value_or(CanonPath::root))
        );
    }

    /* If `path' refers to a directory, append `/default.nix'. */
    if (path.lstat().type == InputAccessor::tDirectory)
        return checkSourcePath(path + "default.nix");

    return path;
}


Expr & Evaluator::parseExprFromFile(const CheckedSourcePath & path)
{
    return parseExprFromFile(path, builtins.staticEnv);
}


Expr & Evaluator::parseExprFromFile(const CheckedSourcePath & path, std::shared_ptr<StaticEnv> & staticEnv)
{
    auto buffer = path.readFile();
    return *parse(buffer.data(), buffer.size(), Pos::Origin(path), path.parent(), staticEnv);
}


Expr & Evaluator::parseExprFromString(
    std::string s_,
    const SourcePath & basePath,
    std::shared_ptr<StaticEnv> & staticEnv,
    const FeatureSettings & featureSettings
)
{
    auto s = make_ref<std::string>(std::move(s_));
    return *parse(s->data(), s->size(), Pos::String{.source = s}, basePath, staticEnv, featureSettings);
}


Expr & Evaluator::parseExprFromString(
    std::string s,
    const SourcePath & basePath,
    const FeatureSettings & featureSettings
)
{
    return parseExprFromString(std::move(s), basePath, builtins.staticEnv, featureSettings);
}

std::variant<std::unique_ptr<Expr>, ExprReplBindings>
Evaluator::parseReplInput(
    std::string s_,
    const SourcePath & basePath,
    std::shared_ptr<StaticEnv> & staticEnv,
    const FeatureSettings & featureSettings
)
{
    auto s = make_ref<std::string>(std::move(s_));
    return parse_repl(s->data(), s->size(), Pos::String{.source = s}, basePath, staticEnv, featureSettings);
}

Expr & Evaluator::parseStdin()
{
    //Activity act(*logger, lvlTalkative, "parsing standard input");
    auto s = make_ref<std::string>(drainFD(0));
    return *parse(
        s->data(), s->size(), Pos::Stdin{.source = s}, CanonPath::fromCwd(), builtins.staticEnv
    );
}


kj::Promise<Result<EvalPaths::PathResult<SourcePath, ThrownError>>>
EvalPaths::findFile(const std::string_view path)
{
    return findFile(searchPath_, path);
}


kj::Promise<Result<EvalPaths::PathResult<SourcePath, ThrownError>>>
EvalPaths::findFile(const SearchPath & searchPath, const std::string_view path, const PosIdx pos)
try {
    for (auto & i : searchPath.elements) {
        auto suffixOpt = i.prefix.suffixIfPotentialMatch(path);

        if (!suffixOpt) continue;
        auto suffix = *suffixOpt;

        auto rOpt = TRY_AWAIT(resolveSearchPathPath(i.path));
        if (!rOpt) continue;
        auto r = *rOpt;

        Path res = suffix == "" ? r : concatStrings(r, "/", suffix);
        if (pathExists(res)) co_return SourcePath(CanonPath(canonPath(res)));
    }

    if (path.starts_with("nix/"))
        co_return SourcePath(CanonPath(concatStrings(corepkgsPrefix, path.substr(4))));

    co_return errors.make<ThrownError>(
        evalSettings.pureEval
            ? "cannot look up '<%s>' in pure evaluation mode (use '--impure' to override)"
            : "file '%s' was not found in the Nix search path (add it using $NIX_PATH or -I)",
        path
    ).atPos(pos);
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<std::optional<std::string>>>
EvalPaths::resolveSearchPathPath(const SearchPath::Path & value0)
try {
    auto & value = value0.s;
    auto i = searchPathResolved.find(value);
    if (i != searchPathResolved.end()) co_return i->second;

    std::optional<std::string> res;

    if (EvalSettings::isPseudoUrl(value)) {
        try {
            auto storePath = TRY_AWAIT(fetchers::downloadTarball(
                store, EvalSettings::resolvePseudoUrl(value), "source", false)).tree.storePath;
            res = { store->toRealPath(storePath) };
        } catch (FileTransferError & e) {
            e.addTrace(nullptr, "while downloading %s to satisfy NIX_PATH lookup, ignoring search path entry", value);
            logWarning(e.info());
            res = std::nullopt;
        }
    }

    else if (value.starts_with("flake:")) {
        experimentalFeatureSettings.require(Xp::Flakes);
        auto flakeRef = parseFlakeRef(value.substr(6), {}, true, false);
        debug("fetching flake search path element '%s''", value);
        auto storePath =
            TRY_AWAIT(TRY_AWAIT(flakeRef.resolve(store)).fetchTree(store)).first.storePath;
        res = {store->toRealPath(storePath)};
    }

    else {
        auto path = absPath(value);
        if (pathExists(path))
            res = { path };
        else {
            logWarning({
                .msg = HintFmt("Nix search path entry '%1%' does not exist, ignoring", value)
            });
            res = std::nullopt;
        }
    }

    if (res)
        debug("resolved search path element '%s' to '%s'", value, *res);
    else
        debug("failed to resolve search path element '%s'", value);

    searchPathResolved[value] = res;
    co_return res;
} catch (...) {
    co_return result::current_exception();
}


std::string ExternalValueBase::coerceToString(EvalState & state, const PosIdx & pos, NixStringContext & context, bool copyMore, bool copyToStore) const
{
    state.ctx.errors.make<TypeError>(
        "cannot coerce %1% to a string: %2%", showType(), *this
    ).atPos(pos).debugThrow();
}


bool ExternalValueBase::operator==(const ExternalValueBase & b) const
{
    return false;
}


std::ostream & operator << (std::ostream & str, const ExternalValueBase & v) {
    return v.print(str);
}


}
