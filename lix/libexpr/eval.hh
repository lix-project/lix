#pragma once
///@file

#include "lix/libexpr/attr-set.hh"
#include "lix/libexpr/eval-error.hh"
#include "lix/libexpr/gc-alloc.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/generator.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/source-path.hh"
#include "lix/libutil/types.hh"
#include "lix/libexpr/value.hh"
#include "lix/libexpr/nixexpr.hh"
#include "lix/libexpr/symbol-table.hh"
#include "lix/libutil/config.hh"
#include "lix/libutil/experimental-features.hh"
#include "lix/libexpr/search-path.hh"
#include "lix/libexpr/repl-exit-status.hh"
#include "lix/libutil/backed-string-view.hh"

#include <concepts>
#include <map>
#include <optional>
#include <unordered_map>
#include <functional>

namespace nix {

class Store;
class EvalState;
class StorePath;
struct SingleDerivedPath;
enum RepairFlag : bool;
struct MemoryInputAccessor;
namespace eval_cache {
    class EvalCache;
}

std::ostream & operator<<(std::ostream & output, const PrimOp & primOp);

/**
 * Info about a constant
 */
struct Constant
{
    /**
     * Optional type of the constant (known since it is a fixed value).
     *
     * @todo we should use an enum for this.
     */
    ValueType type = nThunk;

    /**
     * Optional free-form documentation about the constant.
     */
    const char * doc = nullptr;

    /**
     * Whether the constant is impure, and not available in pure mode.
     */
    bool impureOnly = false;
};

using ValMap = GcMap<std::string, Value>;

struct alignas(Value::Acb::TAG_ALIGN) Env
{
    Env * up;
    Value values[0];
};

void printEnvBindings(const EvalState &es, const Expr & expr, const Env & env);
void printEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env, int lvl = 0);

std::unique_ptr<ValMap> mapStaticEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env);

void copyContext(const Value & v, NixStringContext & context);


std::string printValue(EvalState & state, Value & v);
std::ostream & operator << (std::ostream & os, const ValueType t);


/**
 * Initialise the evaluator (including Boehm GC, if applicable).
 */
void initLibExpr();


struct RegexCache;

struct DebugTrace {
    std::shared_ptr<Pos> pos;
    const Expr & expr;
    const Env & env;
    HintFmt hint;
    bool isError;
    std::shared_ptr<const DebugTrace> parent;
};

struct DebugState
{
private:
    std::weak_ptr<const DebugTrace> latestTrace;
    const PosTable & positions;
    const SymbolTable & symbols;

public:
    std::function<ReplExitStatus(ValMap const & extraEnv, NeverAsync)> errorCallback;
    bool stop = false;
    bool inDebugger = false;
    std::map<const Expr *, const std::shared_ptr<const StaticEnv>> exprEnvs;
    int trylevel = 0;

    explicit DebugState(
        const PosTable & positions,
        const SymbolTable & symbols,
        std::function<ReplExitStatus(ValMap const & extraEnv, NeverAsync)> errorCallback
    )
        : positions(positions)
        , symbols(symbols)
        , errorCallback(errorCallback)
    {
        assert(errorCallback);
    }

    void onEvalError(const EvalError * error, const Env & env, const Expr & expr, NeverAsync = {});

    const std::shared_ptr<const StaticEnv> staticEnvFor(const Expr & expr) const
    {
        if (auto i = exprEnvs.find(&expr); i != exprEnvs.end()) {
            return i->second;
        }
        return nullptr;
    }

    class TraceFrame
    {
        friend struct DebugState;
        template<std::derived_from<EvalError> T>
        friend class EvalErrorBuilder;

        // holds both the data for this frame *and* a deleter that pulls this frame
        // off the trace stack. EvalErrorBuilder uses this for withFrame fake trace
        // frames, and to avoid needing to see this class definition in its header.
        const std::shared_ptr<const DebugTrace> entry = nullptr;

        explicit TraceFrame(std::shared_ptr<const DebugTrace> entry): entry(std::move(entry)) {}

    public:
        TraceFrame(std::nullptr_t) {}
    };

    TraceFrame addTrace(DebugTrace t);

    /// Enumerates the debug frame stack, from the current frame to the root frame.
    /// All values are guaranteed to not be null, but must be pointers because C++.
    Generator<const DebugTrace *> traces()
    {
        for (auto current = latestTrace.lock(); current; current = current->parent) {
            co_yield current.get();
        }
    }
};

struct StaticSymbols
{
    const Symbol outPath, drvPath, type, meta, name, value, system, overrides, outputs, outputName,
        ignoreNulls, file, line, column, functor, toString, right, wrong, structuredAttrs,
        allowedReferences, allowedRequisites, disallowedReferences, disallowedRequisites, maxSize,
        maxClosureSize, builder, args, contentAddressed, impure, outputHash, outputHashAlgo,
        outputHashMode, recurseForDerivations, description, self, startSet, operator_, key,
        path, prefix, outputSpecified;

    const Expr::AstSymbols exprSymbols;

    explicit StaticSymbols(SymbolTable & symbols);
};

class EvalMemory
{
    static constexpr size_t CACHES = 8;
    static constexpr size_t CACHE_INCREMENT = sizeof(void *);

    /**
     * Allocation caches for small values.
     */
    void * gcCache[CACHES] = {};

public:
    struct Statistics
    {
        unsigned long nrEnvs = 0;
        unsigned long nrValuesInEnvs = 0;
        unsigned long nrAttrsets = 0;
        unsigned long nrAttrsInAttrsets = 0;
        unsigned long nrListElems = 0;
    };

    EvalMemory();
    ~EvalMemory();

    EvalMemory(const EvalMemory &) = delete;
    EvalMemory(EvalMemory &&) = delete;
    EvalMemory & operator=(const EvalMemory &) = delete;
    EvalMemory & operator=(EvalMemory &&) = delete;

    inline void * allocBytes(size_t size);
    template<typename T>
    inline T * allocType(size_t n = 1);

    inline Env & allocEnv(size_t size);

    Bindings * allocBindings(size_t capacity);
    Value::List * newList(size_t length);

    BindingsBuilder buildBindings(SymbolTable & symbols, size_t capacity)
    {
        return BindingsBuilder(*this, symbols, allocBindings(capacity), capacity);
    }

    const Statistics getStats() const { return stats; }

private:
    Statistics stats;
};

class EvalBuiltins
{
    EvalMemory & mem;
    SymbolTable & symbols;

public:
    explicit EvalBuiltins(
        EvalMemory & mem,
        SymbolTable & symbols,
        const SearchPath & searchPath,
        const Path & storeDir,
        size_t size = 128
    );

    /**
     * The base environment, containing the builtin functions and
     * values.
     */
    Env & env;

    /**
     * The same, but used during parsing to resolve variables.
     */
    std::shared_ptr<StaticEnv> staticEnv; // !!! should be private

    /**
     * Name and documentation about every constant.
     *
     * Constants from primops are hard to crawl, and their docs will go
     * here too.
     */
    std::vector<std::pair<std::string, Constant>> constantInfos;

private:
    unsigned int baseEnvDispl = 0;

    void createBaseEnv(const SearchPath & searchPath, const Path & storeDir);

    void addConstant(const std::string & name, const Value & v, Constant info);

    void addPrimOp(PrimOpDetails primOp);

    Value prepareNixPath(const SearchPath & searchPath);

public:
    Value & get(const std::string & name);

    struct Doc
    {
        Pos pos;
        std::optional<std::string> name;
        size_t arity;
        std::vector<std::string> args;
        /**
         * Unlike the other `doc` fields in this file, this one should never be
         * `null`.
         */
        const char * doc;
    };

    std::optional<Doc> getDoc(Value & v);
};

struct CachedEvalFile;

struct EvalRuntimeCaches
{
    RootValue vCallFlake;
    RootValue vImportedDrvToDerivation;

    /**
     * Cache used by prim_match() and other regex functions.
     */
    std::shared_ptr<RegexCache> regexes;

    /**
     * A cache from path names to values for evalFile().
     */
    std::map<SourcePath, std::shared_ptr<CachedEvalFile>> fileEval;
};

struct EvalErrorContext
{
    const PosTable & positions;
    DebugState * debug;

    template<std::derived_from<EvalError> T, typename... Args>
    [[gnu::noinline]]
    EvalErrorBuilder<T> make(const Args & ... args) {
        return EvalErrorBuilder<T>(positions, debug, args...);
    }
};

class EvalPaths
{
    ref<Store> store;
    SearchPath searchPath_;
    EvalErrorContext & errors;

public:
    EvalPaths(
        AsyncIoRoot & aio,
        const ref<Store> & store,
        SearchPath searchPath,
        EvalErrorContext & errors
    );

    const SearchPath & searchPath() const { return searchPath_; }

private:
    struct AllowedPath
    {
        struct ComponentLess : std::less<>
        {
            // we'll only use this for string-likes, it's fine. trust me sis.
            using is_transparent = void;
        };

        std::map<std::string, AllowedPath, ComponentLess> children;

        bool allowAllChildren = false;
    };

    /**
     * The allowed filesystem paths in restricted or pure evaluation
     * mode.
     */
    std::optional<AllowedPath> allowedPaths;


    /* Cache for calls to addToStore(); maps source paths to the store
       paths. */
    std::map<SourcePath, StorePath> srcToStore;

    std::map<std::string, std::optional<std::string>> searchPathResolved;

    /**
     * Cache used by checkSourcePath().
     */
    std::unordered_map<Path, CheckedSourcePath> resolvedPaths;

public:
    /**
     * Allow access to a path.
     */
    void allowPath(const Path & path);

    /**
     * Allow access to a store path. Note that this gets remapped to
     * the real store path if `store` is a chroot store.
     */
    void allowPath(const StorePath & storePath);

    /**
     * Allow access to a store path and return it as a string.
     */
    void allowAndSetStorePathString(const StorePath & storePath, Value & v);

    /**
     * Check whether access to a path is allowed and throw an error if
     * not. Otherwise return the canonicalised path.
     */
    CheckedSourcePath checkSourcePath(const SourcePath & path);

    /**
     * If `path` refers to a directory, then append "/default.nix".
     */
    CheckedSourcePath resolveExprPath(SourcePath path);

    void checkURI(const std::string & uri);

    /**
     * When using a diverted store and 'path' is in the Nix store, map
     * 'path' to the diverted location (e.g. /nix/store/foo is mapped
     * to /home/alice/my-nix/nix/store/foo). However, this is only
     * done if the context is not empty, since otherwise we're
     * probably trying to read from the actual /nix/store. This is
     * intended to distinguish between import-from-derivation and
     * sources stored in the actual /nix/store.
     */
    Path toRealPath(const Path & path, const NixStringContext & context);

    /**
     * findFile wants to throw a debuggable error when the requested file
     * is not found, but it can't invoke the debugger itself because it's
     * async code. This wraps the result-or-error to allow it regardless.
     * This happens for copyPathToStore as well, with another error type.
     */
    template<typename T, typename E>
    struct PathResult : private std::variant<T, EvalErrorBuilder<E>>
    {
        PathResult(T p) : std::variant<T, EvalErrorBuilder<E>>(std::move(p)) {}
        PathResult(EvalErrorBuilder<E> e) : std::variant<T, EvalErrorBuilder<E>>(std::move(e)) {}

        T unwrap(NeverAsync = {}) &&
        {
            return std::visit(
                overloaded{
                    [](T & p) -> T { return std::move(p); },
                    [](EvalErrorBuilder<E> & e) -> T {
                        std::move(e).debugThrow(always_progresses);
                    }
                },
                static_cast<std::variant<T, EvalErrorBuilder<E>> &>(*this)
            );
        }
    };

    /**
     * Look up a file in the search path.
     */
    kj::Promise<Result<PathResult<SourcePath, ThrownError>>> findFile(const std::string_view path);
    kj::Promise<Result<PathResult<SourcePath, ThrownError>>>
    findFile(const SearchPath & searchPath, const std::string_view path, const PosIdx pos = noPos);

    /**
     * Try to resolve a search path value (not the optinal key part)
     *
     * If the specified search path element is a URI, download it.
     *
     * If it is not found, return `std::nullopt`
     */
    kj::Promise<Result<std::optional<std::string>>>
    resolveSearchPathPath(const SearchPath::Path & path);

    kj::Promise<Result<PathResult<StorePath, EvalError>>> copyPathToStore(
        NixStringContext & context, const SourcePath & path, RepairFlag repair = NoRepair
    );

    /**
     * Create a string representing a store path.
     *
     * The string is the printed store path with a context containing a
     * single `NixStringContextElem::Opaque` element of that store path.
     */
    void mkStorePathString(const StorePath & storePath, Value & v);
};

struct EvalStatistics
{
    unsigned long nrLookups = 0;
    unsigned long nrAvoided = 0;
    unsigned long nrOpUpdates = 0;
    unsigned long nrOpUpdateValuesCopied = 0;
    unsigned long nrListConcats = 0;
    unsigned long nrPrimOpCalls = 0;
    unsigned long nrFunctionCalls = 0;
    unsigned long nrThunks = 0;

    bool countCalls = false;

    std::map<std::string, size_t> primOpCalls;
    std::map<ExprLambda *, size_t> functionCalls;
    std::map<PosIdx, size_t> attrSelects;

    void addCall(ExprLambda & fun);
};

class Evaluator
{
    friend class EvalBuiltins;
    friend class EvalState;

    EvalState * activeEval = nullptr;

public:
    SymbolTable symbols;
    PosTable positions;
    const StaticSymbols s;
    EvalMemory mem;
    EvalRuntimeCaches caches;
    EvalPaths paths;
    EvalBuiltins builtins;
    EvalStatistics stats;

    /**
     * If set, force copying files to the Nix store even if they
     * already exist there.
     */
    RepairFlag repair;

    /**
     * Store used to materialise .drv files.
     */
    const ref<Store> store;

    /**
     * Store used to build stuff.
     */
    ref<Store> buildStore;

    std::unique_ptr<DebugState> debug;
    EvalErrorContext errors;

    Evaluator(
        AsyncIoRoot & aio,
        const SearchPath & _searchPath,
        ref<Store> store,
        std::shared_ptr<Store> buildStore = nullptr,
        std::function<ReplExitStatus(EvalState & es, ValMap const & extraEnv)> debugRepl = nullptr
    );

    Evaluator(const Evaluator &) = delete;
    Evaluator(Evaluator &&) = delete;
    Evaluator & operator=(const Evaluator &) = delete;
    Evaluator & operator=(Evaluator &&) = delete;

    /**
     * Parse a Nix expression from the specified file.
     */
    Expr & parseExprFromFile(const CheckedSourcePath & path);
    Expr & parseExprFromFile(const CheckedSourcePath & path, std::shared_ptr<StaticEnv> & staticEnv);

    /**
     * Parse a Nix expression from the specified string.
     */
    Expr & parseExprFromString(
        std::string s,
        const SourcePath & basePath,
        std::shared_ptr<StaticEnv> & staticEnv,
        const FeatureSettings & xpSettings = featureSettings
    );
    Expr & parseExprFromString(
        std::string s,
        const SourcePath & basePath,
        const FeatureSettings & xpSettings = featureSettings
    );

    std::variant<std::unique_ptr<Expr>, ExprReplBindings>
    parseReplInput(
        std::string s,
        const SourcePath & basePath,
        std::shared_ptr<StaticEnv> & staticEnv,
        const FeatureSettings & xpSettings = featureSettings
    );

    Expr & parseStdin();

    /**
     * Creates a thunk that will evaluate the given expression when forced.
     */
    void evalLazily(Expr & e, Value & v);

private:
    Expr * parse(
        char * text,
        size_t length,
        Pos::Origin origin,
        const SourcePath & basePath,
        std::shared_ptr<StaticEnv> & staticEnv,
        const FeatureSettings & xpSettings = featureSettings);

    std::variant<std::unique_ptr<Expr>, ExprReplBindings>
    parse_repl(
        char * text,
        size_t length,
        Pos::Origin origin,
        const SourcePath & basePath,
        std::shared_ptr<StaticEnv> & staticEnv,
        const FeatureSettings & xpSettings = featureSettings
    );

public:
    BindingsBuilder buildBindings(size_t capacity)
    {
        return mem.buildBindings(symbols, capacity);
    }

    /**
     * Print statistics, if enabled.
     *
     * Performs a full memory GC before printing the statistics, so that the
     * GC statistics are more accurate.
     */
    void maybePrintStats();

    /**
     * Print statistics, unconditionally, cheaply, without performing a GC first.
     */
    void printStatistics();

    /**
     * Perform a full memory garbage collection - not incremental.
     *
     * @return true if Nix was built with GC and a GC was performed, false if not.
     *              The return value is currently not thread safe - just the return value.
     */
    bool fullGC();

    /**
     * Create an `EvalState` in prepation to evaluate some amount of Nix code.
     *
     * While preparation of evaluation can be done with Evaluator itself only,
     * actually evaluating things requires an EvalState. This function creates
     * an EvalState and returns it. At most one EvalState per Evaluator may be
     * live at any given point, and references to this EvalState must not live
     * anywhere except in the returned box, local variables, or arguments. Any
     * reference held in an object type is illegal, be it in a lambda capture,
     * a pointer member of an object, a hidden member such as arguments passed
     * to functions by `std::thread` or `std::async`—all references held where
     * they could be copied are moved from are disallowed. EvalState is thus a
     * witness type that a given thread may evaluate nix code and must *never*
     * be run inside `kj::Promise` context. This is due to a kj limitation, in
     * which it is not possible to block on a promise while already running in
     * a promise without doing this blocking on a different event loop/thread.
     */
    box_ptr<EvalState> begin(AsyncIoRoot & aio);
};


class EvalState
{
    friend class Evaluator;

    explicit EvalState(AsyncIoRoot & aio, Evaluator & ctx);

public:
    Evaluator & ctx;
    AsyncIoRoot & aio;

    EvalState(const EvalState &) = delete;
    EvalState(EvalState &&) = delete;
    EvalState & operator=(const EvalState &) = delete;
    EvalState & operator=(EvalState &&) = delete;

    ~EvalState();

    /**
     * Evaluate an expression read from the given file to normal form.
     */
    void evalFile(const SourcePath & path, Value & v);

    void resetFileCache();

    /**
     * Evaluate an expression to normal form
     *
     * @param [out] v The resulting is stored here.
     */
    void eval(Expr & e, Value & v);

    /**
     * Evaluation the expression, then verify that it has the expected
     * type.
     */
    inline bool evalBool(Env & env, Expr & e);
    inline void evalAttrs(Env & env, Expr & e, Value & v);
    inline void evalList(Env & env, Expr & e, Value & v);

    /**
     * If `v` is a thunk, enter it and overwrite `v` with the result
     * of the evaluation of the thunk.  If `v` is a delayed function
     * application, call the function and overwrite `v` with the
     * result.  Otherwise, this is a no-op.
     */
    inline void forceValue(Value & v, const PosIdx pos);

    void tryFixupBlackHolePos(Value & v, PosIdx pos);

    /**
     * Force a value, then recursively force list elements and
     * attributes.
     */
    void forceValueDeep(Value & v);

    /**
     * Force `v`, and then verify that it has the expected type.
     */
    NixInt forceInt(Value & v, const PosIdx pos, std::string_view errorCtx);
    NixFloat forceFloat(Value & v, const PosIdx pos, std::string_view errorCtx);
    bool forceBool(Value & v, const PosIdx pos, std::string_view errorCtx);

    void forceAttrs(Value & v, const PosIdx pos, std::string_view errorCtx);
    inline void forceList(Value & v, const PosIdx pos, std::string_view errorCtx);
    /**
     * @param v either lambda or primop
     */
    void forceFunction(Value & v, const PosIdx pos, std::string_view errorCtx);
    std::string_view forceString(Value & v, const PosIdx pos, std::string_view errorCtx);
    std::string_view forceString(Value & v, NixStringContext & context, const PosIdx pos, std::string_view errorCtx);
    std::string_view forceStringNoCtx(Value & v, const PosIdx pos, std::string_view errorCtx);

    /**
     * Realise the given context, and return a mapping from the placeholders
     * used to construct the associated value to their final store path
     */
    [[nodiscard]] StringMap realiseContext(const NixStringContext & context);

public:
    /**
     * @return true iff the value `v` denotes a derivation (i.e. a
     * set with attribute `type = "derivation"`).
     */
    bool isDerivation(Value & v);

    std::optional<std::string> tryAttrsToString(const PosIdx pos, Value & v,
        NixStringContext & context, StringCoercionMode mode = StringCoercionMode::Strict, bool copyToStore = true);

    /**
     * String coercion.
     *
     * Converts strings, paths and derivations to a
     * string. If `copyToStore` is set,
     * referenced paths are copied to the Nix store as a side effect.
     */
    BackedStringView coerceToString(const PosIdx pos, Value & v, NixStringContext & context,
        std::string_view errorCtx,
        StringCoercionMode mode = StringCoercionMode::Strict, bool copyToStore = true,
        bool canonicalizePath = true);

    /**
     * Path coercion.
     *
     * Converts strings, paths and derivations to a
     * path.  The result is guaranteed to be a canonicalised, absolute
     * path.  Nothing is copied to the store.
     */
    SourcePath coerceToPath(const PosIdx pos, Value & v, NixStringContext & context, std::string_view errorCtx);

    /**
     * Like coerceToPath, but the result must be a store path.
     */
    StorePath coerceToStorePath(const PosIdx pos, Value & v, NixStringContext & context, std::string_view errorCtx);

    /**
     * Part of `coerceToSingleDerivedPath()` without any store IO which is exposed for unit testing only.
     */
    std::pair<SingleDerivedPath, std::string_view> coerceToSingleDerivedPathUnchecked(const PosIdx pos, Value & v, std::string_view errorCtx);

    /**
     * Coerce to `SingleDerivedPath`.
     *
     * Must be a string which is either a literal store path.
     *
     * Even more importantly, the string context must be exactly one
     * element, which is either a `NixStringContextElem::Opaque` or
     * `NixStringContextElem::Built`. (`NixStringContextEleme::DrvDeep`
     * is not permitted).
     *
     * The string is parsed based on the context --- the context is the
     * source of truth, and ultimately tells us what we want, and then
     * we ensure the string corresponds to it.
     */
    SingleDerivedPath coerceToSingleDerivedPath(const PosIdx pos, Value & v, std::string_view errorCtx);

private:

    inline Value * lookupVar(Env * env, const ExprVar & var, bool noEval);

    friend struct ExprVar;
    friend struct ExprSet;
    friend struct ExprLet;

    /**
     * Current Nix call stack depth, used with `max-call-depth` setting to throw stack overflow hopefully before we run out of system stack.
     */
    size_t callDepth = 0;

public:

    /**
     * Do a deep equality test between two values.  That is, list
     * elements and attributes are compared recursively.
     */
    bool eqValues(Value & v1, Value & v2, const PosIdx pos, std::string_view errorCtx);

    bool isFunctor(Value & fun);

    void callFunction(Value & fun, std::span<Value> args, Value & vRes, const PosIdx pos);

    void callFunction(Value & fun, Value & arg, Value & vRes, const PosIdx pos)
    {
        callFunction(fun, {&arg, 1}, vRes, pos);
    }

    /**
     * Automatically call a function for which each argument has a
     * default value or has a binding in the `args` map.
     */
    void autoCallFunction(Bindings & args, Value & fun, Value & res, PosIdx pos);

    void mkPos(Value & v, PosIdx pos);

    /**
     * Create a string representing a `SingleDerivedPath::Built`.
     *
     * The string is the printed store path with a context containing a
     * single `NixStringContextElem::Built` element of the drv path and
     * output name.
     *
     * @param value Value we are settings
     *
     * @param b the drv whose output we are making a string for, and the
     * output
     *
     * @param staticOutputPath Output path for that string.
     * Will be printed to form string.
     */
    void mkOutputString(
        Value & value,
        const SingleDerivedPath::Built & b,
        const StorePath & staticOutputPath);

    /**
     * Create a string representing a `SingleDerivedPath`.
     *
     * A combination of `mkStorePathString` and `mkOutputString`.
     */
    void mkSingleDerivedPathString(
        const SingleDerivedPath & p,
        Value & v);

    void
    concatLists(Value & v, std::span<Value> lists, const PosIdx pos, std::string_view errorCtx);

private:

    /**
     * Like `mkOutputString` but just creates a raw string, not an
     * string Value, which would also have a string context.
     */
    std::string mkOutputStringRaw(
        const StorePath & staticOutputPath);

    /**
     * Like `mkSingleDerivedPathStringRaw` but just creates a raw string
     * Value, which would also have a string context.
     */
    std::string mkSingleDerivedPathStringRaw(
        const SingleDerivedPath & p);
};

/**
 * @return A string representing the type of the value `v`.
 *
 * @param withArticle Whether to begin with an english article, e.g. "an
 * integer" vs "integer".
 */
std::string_view showType(ValueType type, bool withArticle = true);
std::string showType(const Value & v);

static constexpr std::string_view corepkgsPrefix{"/__corepkgs__/"};


}

#include "lix/libexpr/eval-inline.hh" // IWYU pragma: keep
