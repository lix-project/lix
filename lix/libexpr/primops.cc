#include "lix/libutil/archive.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libexpr/extra-primops.hh"
#include "lix/libexpr/gc-small-vector.hh"
#include "lix/libstore/globals.hh"
#include "lix/libexpr/json-to-value.hh"
#include "lix/libstore/names.hh"
#include "lix/libstore/path-references.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/processes.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libexpr/value-to-json.hh"
#include "lix/libexpr/value-to-xml.hh"
#include "lix/libexpr/primops.hh"
#include "lix/libfetchers/fetch-to-store.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/types.hh"
#include "value.hh"

#include <boost/container/small_vector.hpp>
#include <kj/async.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <regex>
#include <dlfcn.h>

#include <cmath>

namespace nix {

/*************************************************************
 * Miscellaneous
 *************************************************************/

StringMap EvalState::realiseContext(const NixStringContext & context)
{
    std::vector<DerivedPath::Built> drvs;
    StringMap res;

    for (auto & c : context) {
        auto ensureValid = [&](const StorePath & p) {
            if (!aio.blockOn(ctx.store->isValidPath(p)))
                ctx.errors.make<InvalidPathError>(ctx.store->printStorePath(p)).debugThrow(always_progresses);
        };
        std::visit(overloaded {
            [&](const NixStringContextElem::Built & b) {
                drvs.push_back(DerivedPath::Built {
                    .drvPath = b.drvPath,
                    .outputs = OutputsSpec::Names { b.output },
                });
                return ensureValid(b.drvPath.path);
            },
            [&](const NixStringContextElem::Opaque & o) {
                auto ctxS = ctx.store->printStorePath(o.path);
                res.insert_or_assign(ctxS, ctxS);
                return ensureValid(o.path);
            },
            [&](const NixStringContextElem::DrvDeep & d) {
                /* Treat same as Opaque */
                auto ctxS = ctx.store->printStorePath(d.drvPath);
                res.insert_or_assign(ctxS, ctxS);
                return ensureValid(d.drvPath);
            },
        }, c.raw);
    }

    if (drvs.empty()) return StringMap{};

    if (!evalSettings.enableImportFromDerivation)
        ctx.errors.make<EvalError>(
            "cannot build '%1%' during evaluation because the option 'allow-import-from-derivation' is disabled",
            drvs.begin()->to_string(*ctx.store)
        ).debugThrow();

    /* Build/substitute the context. */
    std::vector<DerivedPath> buildReqs;
    for (auto & d : drvs) buildReqs.emplace_back(DerivedPath { d });
    aio.blockOn(ctx.buildStore->buildPaths(buildReqs, bmNormal, ctx.store));

    StorePathSet outputsToCopyAndAllow;

    for (auto & drv : drvs) {
        auto outputs = aio.blockOn(resolveDerivedPath(*ctx.buildStore, drv, &*ctx.store));
        for (auto & [outputName, outputPath] : outputs) {
            outputsToCopyAndAllow.insert(outputPath);
        }
    }

    if (ctx.store != ctx.buildStore) {
        aio.blockOn(copyClosure(*ctx.buildStore, *ctx.store, outputsToCopyAndAllow));
    }
    for (auto & outputPath : outputsToCopyAndAllow) {
        /* Add the output of this derivations to the allowed
            paths. */
        ctx.paths.allowPath(outputPath);
    }

    return res;
}

static auto realisePath(EvalState & state, Value & v, auto checkFn)
{
    NixStringContext context;

    auto path = state.coerceToPath(noPos, v, context, "while realising the context of a path");

    try {
        StringMap rewrites = state.realiseContext(context);

        return checkFn(SourcePath(CanonPath(
            state.ctx.paths.toRealPath(rewriteStrings(path.canonical().abs(), rewrites), context)
        )));
    } catch (Error & e) {
        e.addTrace(nullptr, "while realising the context of path '%s'", path);
        throw;
    }
}

static CheckedSourcePath realisePath(EvalState & state, Value & v)
{
    return realisePath(state, v, [&](auto p) { return state.ctx.paths.checkSourcePath(p); });
}

/**
 * Add and attribute to the given attribute map from the output name to
 * the output path, or a placeholder.
 *
 * Where possible the path is used, but for floating CA derivations we
 * may not know it. For sake of determinism we always assume we don't
 * and instead put in a place holder. In either case, however, the
 * string context will contain the drv path and output name, so
 * downstream derivations will have the proper dependency, and in
 * addition, before building, the placeholder will be rewritten to be
 * the actual path.
 *
 * The 'drv' and 'drvPath' outputs must correspond.
 */
static void mkOutputString(
    EvalState & state,
    BindingsBuilder & attrs,
    const StorePath & drvPath,
    const std::pair<std::string, DerivationOutput> & o)
{
    state.mkOutputString(
        attrs.alloc(o.first),
        SingleDerivedPath::Built {
            .drvPath = makeConstantStorePath(drvPath),
            .output = o.first,
        },
        o.second.path(*state.ctx.store, Derivation::nameFromPath(drvPath), o.first));
}

/* Load and evaluate an expression from path specified by the
   argument. */
static void import(EvalState & state, Value & vPath, Value * vScope, Value & v)
{
    auto path = realisePath(state, vPath);
    auto path2 = path.canonical().abs();

    // FIXME
    auto isValidDerivationInStore = [&]() -> std::optional<StorePath> {
        if (!state.ctx.store->isStorePath(path2))
            return std::nullopt;
        auto storePath = state.ctx.store->parseStorePath(path2);
        if (!(state.aio.blockOn(state.ctx.store->isValidPath(storePath)) && isDerivation(path2)))
            return std::nullopt;
        return storePath;
    };

    if (auto storePath = isValidDerivationInStore()) {
        Derivation drv = state.aio.blockOn(state.ctx.store->readDerivation(*storePath));
        auto attrs = state.ctx.buildBindings(3 + drv.outputs.size());
        attrs.alloc(state.ctx.s.drvPath).mkString(path2, {
            NixStringContextElem::DrvDeep { .drvPath = *storePath },
        });
        attrs.alloc(state.ctx.s.name).mkString(drv.env["name"]);
        auto & outputsVal = attrs.alloc(state.ctx.s.outputs);
        auto outputsList = state.ctx.mem.newList(drv.outputs.size());
        outputsVal = {NewValueAs::list, outputsList};

        for (const auto & [i, o] : enumerate(drv.outputs)) {
            mkOutputString(state, attrs, *storePath, o);
            outputsList->elems[i].mkString(o.first);
        }

        Value w{NewValueAs::attrs, attrs.finish()};

        if (!state.ctx.caches.vImportedDrvToDerivation) {
            state.ctx.caches.vImportedDrvToDerivation = allocRootValue({});
            state.eval(
                state.ctx.parseExprFromString(
#include "imported-drv-to-derivation.nix.gen.hh"
                    , CanonPath::root
                ),
                *state.ctx.caches.vImportedDrvToDerivation
            );
        }

        state.forceFunction(
            *state.ctx.caches.vImportedDrvToDerivation,
            noPos,
            "while evaluating imported-drv-to-derivation.nix.gen.hh"
        );
        v = {NewValueAs::app, state.ctx.mem, *state.ctx.caches.vImportedDrvToDerivation, w};
        state.forceAttrs(v, noPos, "while calling imported-drv-to-derivation.nix.gen.hh");
    }

    else if (path2 == corepkgsPrefix + "fetchurl.nix") {
        state.eval(state.ctx.parseExprFromString(
            #include "fetchurl.nix.gen.hh"
            , CanonPath::root), v);
    }

    else {
        if (!vScope)
            state.evalFile(path, v);
        else {
            state.forceAttrs(*vScope, noPos, "while evaluating the first argument passed to builtins.scopedImport");

            Env * env = &state.ctx.mem.allocEnv(vScope->attrs()->size());
            env->up = &state.ctx.builtins.env;

            auto staticEnv = std::make_shared<StaticEnv>(
                nullptr, state.ctx.builtins.staticEnv.get(), vScope->attrs()->size()
            );

            staticEnv->vars.unsafe_insert_bulk([&] (auto & map) {
                unsigned int displ = 0;
                for (auto & attr : *vScope->attrs()) {
                    // safety: args[0]->attrs is already sorted.
                    map.emplace_back(attr.name, displ);
                    env->values[displ++] = attr.value;
                }
            });

            debug("evaluating file '%1%'", path);
            Expr & e = state.ctx.parseExprFromFile(state.ctx.paths.resolveExprPath(path), staticEnv);

            e.eval(state, *env, v);
        }
    }
}

static RegisterPrimOp primop_scopedImport(PrimOp{
    {.name = "scopedImport",
     .arity = 2,
     .fun = [](EvalState & state, Value ** args, Value & v) { import(state, *args[1], args[0], v); }
    }
});

static void prim_import(EvalState & state, Value * * args, Value & v)
{
    import(state, *args[0], nullptr, v);
}

/* Want reasonable symbol names, so extern C */
/* !!! Should we pass the Pos or the file name too? */
extern "C" typedef void (*ValueInitializer)(EvalState & state, Value & v);

/* Load a ValueInitializer from a DSO and return whatever it initializes */
void prim_importNative(EvalState & state, Value * * args, Value & v)
{
    auto path = realisePath(state, *args[0]);

    std::string sym(state.forceStringNoCtx(*args[1], noPos, "while evaluating the second argument passed to builtins.importNative"));

    void *handle = dlopen(path.canonical().c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
        state.ctx.errors.make<EvalError>("could not open '%1%': %2%", path, dlerror()).debugThrow();

    dlerror();
    ValueInitializer func = reinterpret_cast<ValueInitializer>(dlsym(handle, sym.c_str()));
    if(!func) {
        char *message = dlerror();
        if (message)
            state.ctx.errors.make<EvalError>("could not load symbol '%1%' from '%2%': %3%", sym, path, message).debugThrow();
        else
            state.ctx.errors.make<EvalError>("symbol '%1%' from '%2%' resolved to NULL when a function pointer was expected", sym, path).debugThrow();
    }

    (func)(state, v);

    /* We don't dlclose because v may be a primop referencing a function in the shared object file */
}


/* Execute a program and parse its output */
void prim_exec(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[0], noPos, "while evaluating the first argument passed to builtins.exec");
    auto elems = args[0]->listElems();
    auto count = args[0]->listSize();
    if (count == 0)
        state.ctx.errors.make<EvalError>("at least one argument to 'exec' required").debugThrow();
    NixStringContext context;
    auto program =
        state
            .coerceToString(
                noPos,
                elems[0],
                context,
                "while evaluating the first element of the argument passed to builtins.exec",
                StringCoercionMode::Strict,
                false
            )
            .toOwned();
    Strings commandArgs;
    for (size_t i = 1; i < count; ++i) {
        commandArgs.push_back(
            state
                .coerceToString(
                    noPos,
                    elems[i],
                    context,
                    "while evaluating an element of the argument passed to builtins.exec",
                    StringCoercionMode::Strict,
                    false
                )
                .toOwned()
        );
    }
    try {
        auto _ = state.realiseContext(context); // FIXME: Handle CA derivations
    } catch (InvalidPathError & e) {
        e.addTrace(nullptr, "while realising the context for builtins.exec");
        throw;
    }

    auto output = state.aio.blockOn(runProgram(program, true, commandArgs));
    Expr * parsed;
    try {
        parsed = &state.ctx.parseExprFromString(std::move(output), CanonPath::root);
    } catch (Error & e) {
        e.addTrace(nullptr, "while parsing the output from '%1%'", program);
        throw;
    }
    try {
        state.eval(*parsed, v);
    } catch (Error & e) {
        e.addTrace(nullptr, "while evaluating the output from '%1%'", program);
        throw;
    }
}

/* Return a string representing the type of the expression. */
static void prim_typeOf(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    std::string t;
    switch (args[0]->type()) {
        case nInt: t = "int"; break;
        case nBool: t = "bool"; break;
        case nString: t = "string"; break;
        case nPath: t = "path"; break;
        case nNull: t = "null"; break;
        case nAttrs: t = "set"; break;
        case nList: t = "list"; break;
        case nFunction: t = "lambda"; break;
        case nExternal:
            t = args[0]->external()->typeOf();
            break;
        case nFloat: t = "float"; break;
        case nThunk: abort();
    }
    v.mkString(t);
}

/* Determine whether the argument is the null value. */
static void prim_isNull(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    v.mkBool(args[0]->type() == nNull);
}

/* Determine whether the argument is a function. */
static void prim_isFunction(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    v.mkBool(args[0]->type() == nFunction);
}

/* Determine whether the argument is an integer. */
static void prim_isInt(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    v.mkBool(args[0]->type() == nInt);
}

/* Determine whether the argument is a float. */
static void prim_isFloat(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    v.mkBool(args[0]->type() == nFloat);
}

/* Determine whether the argument is a string. */
static void prim_isString(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    v.mkBool(args[0]->type() == nString);
}

/* Determine whether the argument is a Boolean. */
static void prim_isBool(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    v.mkBool(args[0]->type() == nBool);
}

/* Determine whether the argument is a path. */
static void prim_isPath(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    v.mkBool(args[0]->type() == nPath);
}

template<typename Callable>
 static inline void withExceptionContext(Trace trace, Callable&& func)
{
    try
    {
        func();
    }
    catch(Error & e)
    {
        e.pushTrace(trace);
        throw;
    }
}

struct CompareValues : NeverAsync
{
    EvalState & state;
    const std::string_view errorCtx;

    CompareValues(EvalState & state, const std::string_view && errorCtx) : state(state), errorCtx(errorCtx) { };

    bool operator()(Value * v1, Value * v2) const
    {
        return (*this)(*v1, *v2, errorCtx);
    }

    bool operator()(Value & v1, Value & v2) const
    {
        return (*this)(v1, v2, errorCtx);
    }

    bool operator()(Value & v1, Value & v2, std::string_view errorCtx) const
    {
        try {
            if (v1.type() == nFloat && v2.type() == nInt) {
                return v1.fpoint() < v2.integer().value;
            }
            if (v1.type() == nInt && v2.type() == nFloat) {
                return v1.integer().value < v2.fpoint();
            }
            if (v1.type() != v2.type()) {
                state.ctx.errors
                    .make<EvalError>("cannot compare %s with %s", showType(v1), showType(v2))
                    .debugThrow();
            }
            // Allow selecting a subset of enum values
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wswitch-enum"
            switch (v1.type()) {
            case nInt:
                return v1.integer() < v2.integer();
            case nFloat:
                return v1.fpoint() < v2.fpoint();
            case nString:
                return v1.str() < v2.str();
            case nPath:
                return strcmp(v1.string().content, v2.string().content) < 0;
            case nList:
                // Lexicographic comparison
                for (size_t i = 0;; i++) {
                    if (i == v2.listSize()) {
                        return false;
                    } else if (i == v1.listSize()) {
                        return true;
                    } else if (!state.eqValues(
                                   v1.listElems()[i], v2.listElems()[i], noPos, errorCtx
                               ))
                    {
                        return (*this)(
                            v1.listElems()[i],
                            v2.listElems()[i],
                            "while comparing two list elements"
                        );
                    }
                }
                default:
                    state.ctx.errors
                        .make<EvalError>(
                            "cannot compare %s with %s; values of that type are incomparable",
                            showType(v1),
                            showType(v2)
                        )
                        .debugThrow();
#pragma GCC diagnostic pop
                }
        } catch (Error & e) {
            if (!errorCtx.empty())
                e.addTrace(nullptr, errorCtx);
            throw;
        }
    }
};

/// NOTE: this type must NEVER be outside of GC-scanned memory.
#if HAVE_BOEHMGC
using UnsafeValueList = std::list<Value *, gc_allocator<Value *>>;
#else
using UnsafeValueList = std::list<Value *>;
#endif

static const Attr *
getAttr(EvalState & state, Symbol attrSym, Bindings * attrSet, std::string_view errorCtx)
{
    auto value = attrSet->get(attrSym);
    if (!value) {
        state.ctx.errors.make<TypeError>("attribute '%s' missing", state.ctx.symbols[attrSym]).withTrace(noPos, errorCtx).debugThrow();
    }
    return value;
}

static void prim_genericClosure(EvalState & state, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], noPos, "while evaluating the first argument passed to builtins.genericClosure");

    /* Get the start set. */
    auto startSet = getAttr(
        state,
        state.ctx.s.startSet,
        args[0]->attrs(),
        "in the attrset passed as argument to builtins.genericClosure"
    );

    state.forceList(
        startSet->value,
        noPos,
        "while evaluating the 'startSet' attribute passed as argument to builtins.genericClosure"
    );

    UnsafeValueList workSet;
    for (auto & elem : startSet->value.listItems()) {
        workSet.push_back(&elem);
    }

    if (startSet->value.listSize() == 0) {
        v = startSet->value;
        return;
    }

    /* Get the operator. */
    auto op = getAttr(
        state,
        state.ctx.s.operator_,
        args[0]->attrs(),
        "in the attrset passed as argument to builtins.genericClosure"
    );
    state.forceFunction(
        op->value,
        noPos,
        "while evaluating the 'operator' attribute passed as argument to builtins.genericClosure"
    );

    /* Construct the closure by applying the operator to elements of
       `workSet', adding the result to `workSet', continuing until
       no new elements are found. */
    UnsafeValueList res;
    // `doneKeys' doesn't need to be a GC root, because its values are
    // reachable from res.
    auto cmp = CompareValues(state, "while comparing the `key` attributes of two genericClosure elements");
    std::set<Value *, decltype(cmp)> doneKeys(cmp);
    while (!workSet.empty()) {
        Value * e = *(workSet.begin());
        workSet.pop_front();

        state.forceAttrs(*e, noPos, "while evaluating one of the elements generated by (or initially passed to) builtins.genericClosure");

        auto key = getAttr(
            state,
            state.ctx.s.key,
            e->attrs(),
            "in one of the attrsets generated by (or initially passed to) builtins.genericClosure"
        );
        state.forceValue(key->value, noPos);

        if (!doneKeys.insert(&key->value).second) {
            continue;
        }
        res.push_back(e);

        /* Call the `operator' function with `e' as argument. */
        Value newElements;
        state.callFunction(op->value, {e, 1}, newElements, noPos);
        state.forceList(newElements, noPos, "while evaluating the return value of the `operator` passed to builtins.genericClosure");

        /* Add the values returned by the operator to the work set. */
        for (auto & elem : newElements.listItems()) {
            state.forceValue(elem, noPos); // "while evaluating one one of the elements returned by
                                           // the `operator` passed to builtins.genericClosure");
            workSet.push_back(&elem);
        }
    }

    /* Create the result list. */
    auto result = state.ctx.mem.newList(res.size());
    v = {NewValueAs::list, result};
    unsigned int n = 0;
    for (auto & i : res)
        result->elems[n++] = *i;
}


static void prim_break(EvalState & state, Value * * args, Value & v)
{
    if (auto trace = state.ctx.debug ? state.ctx.debug->traces().next() : std::nullopt) {
        auto error = EvalError(ErrorInfo {
            .level = lvlInfo,
            .msg = HintFmt("breakpoint reached"),
        });

        state.ctx.debug->onEvalError(&error, (*trace)->env, (*trace)->expr);
    }

    // Return the value we were passed.
    v = *args[0];
}

static void prim_abort(EvalState & state, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(noPos, *args[0], context,
            "while evaluating the error message passed to builtins.abort").toOwned();
    state.ctx.errors.make<Abort>("evaluation aborted with the following error message: '%1%'", s).debugThrow();
}

static void prim_throw(EvalState & state, Value * * args, Value & v)
{
  NixStringContext context;
  auto s = state.coerceToString(noPos, *args[0], context,
          "while evaluating the error message passed to builtin.throw").toOwned();
  state.ctx.errors.make<ThrownError>(s).debugThrow();
}

static void prim_addErrorContext(EvalState & state, Value * * args, Value & v)
{
    try {
        state.forceValue(*args[1], noPos);
        v = *args[1];
    } catch (Error & e) {
        NixStringContext context;
        auto message = state.coerceToString(noPos, *args[0], context,
                "while evaluating the error message passed to builtins.addErrorContext",
                StringCoercionMode::Strict, false).toOwned();
        e.addTrace(nullptr, HintFmt(message));
        throw;
    }
}

static RegisterPrimOp primop_addErrorContext(PrimOp{{
    .name = "__addErrorContext",
    .arity = 2,
    .fun = prim_addErrorContext,
}});

static void prim_ceil(EvalState & state, Value * * args, Value & v)
{
    auto value = state.forceFloat(*args[0], noPos,
            "while evaluating the first argument passed to builtins.ceil");
    v.mkInt(ceil(value));
}

static void prim_floor(EvalState & state, Value * * args, Value & v)
{
    auto value = state.forceFloat(*args[0], noPos, "while evaluating the first argument passed to builtins.floor");
    v.mkInt(floor(value));
}

/* Try evaluating the argument. Success => {success=true; value=something;},
 * else => {success=false; value=false;} */
static void prim_tryEval(EvalState & state, Value * * args, Value & v)
{
    auto attrs = state.ctx.buildBindings(2);

    const bool success = [&] {
        std::optional<MaintainCount<int>> trylevel;
        DebugState * savedDebug = nullptr;
        KJ_DEFER({
            if (savedDebug) {
                state.ctx.errors.debug = savedDebug;
            }
        });
        if (state.ctx.errors.debug != nullptr) {
            trylevel.emplace(state.ctx.errors.debug->trylevel);
            if (evalSettings.ignoreExceptionsDuringTry) {
                /* to prevent starting the repl from exceptions within a tryEval, null it. */
                savedDebug = state.ctx.errors.debug;
                state.ctx.errors.debug = nullptr;
            }
        }

        try {
            state.forceValue(*args[0], noPos);
        } catch (AssertionError & e) {
            return false;
        }
        return true;
    }();
    if (success)
        attrs.insert(state.ctx.s.value, *args[0]);
    else
        attrs.alloc(state.ctx.s.value).mkBool(false);
    attrs.alloc("success").mkBool(success);

    v.mkAttrs(attrs);
}

/* Return an environment variable.  Use with care. */
static void prim_getEnv(EvalState & state, Value * * args, Value & v)
{
    std::string name(state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.getEnv"));
    v.mkString(evalSettings.restrictEval || evalSettings.pureEval ? "" : getEnv(name).value_or(""));
}

/* Evaluate the first argument, then return the second argument. */
static void prim_seq(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    state.forceValue(*args[1], noPos);
    v = *args[1];
}

/* Evaluate the first argument deeply (i.e. recursing into lists and
   attrsets), then return the second argument. */
static void prim_deepSeq(EvalState & state, Value * * args, Value & v)
{
    state.forceValueDeep(*args[0]);
    state.forceValue(*args[1], noPos);
    v = *args[1];
}

/* Evaluate the first expression and print it on standard error.  Then
   return the second expression.  Useful for debugging. */
static void prim_trace(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    if (args[0]->type() == nString)
        printError("trace: %1%", Uncolored(args[0]->str()));
    else
        printError("trace: %1%", Uncolored(ValuePrinter(state, *args[0])));
    if (auto last = evalSettings.builtinsTraceDebugger && state.ctx.debug
            ? state.ctx.debug->traces().next()
            : std::nullopt)
    {
        state.ctx.debug->onEvalError(nullptr, (*last)->env, (*last)->expr);
    }
    state.forceValue(*args[1], noPos);
    v = *args[1];
}


/* Takes two arguments and evaluates to the second one. Used as the
 * builtins.traceVerbose implementation when --trace-verbose is not enabled
 */
static void prim_second(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[1], noPos);
    v = *args[1];
}

/*************************************************************
 * Derivations
 *************************************************************/

static void derivationStrictInternal(EvalState & state, const std::string & name, Bindings * attrs, Value & v);

/* Construct (as a unobservable side effect) a Nix derivation
   expression that performs the derivation described by the argument
   set.  Returns the original set extended with the following
   attributes: `outPath' containing the primary output path of the
   derivation; `drvPath' containing the path of the Nix expression;
   and `type' set to `derivation' to indicate that this is a
   derivation. */
static void prim_derivationStrict(EvalState & state, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], noPos, "while evaluating the argument passed to builtins.derivationStrict");

    Bindings * attrs = args[0]->attrs();

    /* Figure out the name first (for stack backtraces). */
    auto nameAttr = getAttr(
        state,
        state.ctx.s.name,
        attrs,
        "in the attrset passed as argument to builtins.derivationStrict"
    );

    std::string drvName;
    try {
        drvName = state.forceStringNoCtx(
            nameAttr->value,
            noPos,
            "while evaluating the `name` attribute passed to builtins.derivationStrict"
        );
    } catch (Error & e) {
        e.addTrace(
            state.ctx.positions[nameAttr->pos], "while evaluating the derivation attribute 'name'"
        );
        throw;
    }

    try {
        derivationStrictInternal(state, drvName, attrs, v);
    } catch (Error & e) {
        Pos pos = state.ctx.positions[nameAttr->pos];
        /*
         * Here we make two abuses of the error system
         *
         * 1. We print the location as a string to avoid a code snippet being
         * printed. While the location of the name attribute is a good hint, the
         * exact code there is irrelevant.
         *
         * 2. We mark this trace as a frame trace, meaning that we stop printing
         * less important traces from now on. In particular, this prevents the
         * display of the automatic "while calling builtins.derivationStrict"
         * trace, which is of little use for the public we target here.
         *
         * Please keep in mind that error reporting is done on a best-effort
         * basis in nix. There is no accurate location for a derivation, as it
         * often results from the composition of several functions
         * (derivationStrict, derivation, mkDerivation, mkPythonModule, etc.)
         */
        e.addTrace(nullptr, HintFmt(
                "while evaluating derivation '%s'\n"
                "  whose name attribute is located at %s",
                drvName, pos));
        throw;
    }
}

static void derivationStrictInternal(EvalState & state, const std::string &
drvName, Bindings * attrs, Value & v)
{
    /* Check whether attributes should be passed as a JSON file. */
    std::optional<JSON> jsonObject;
    auto attr = attrs->get(state.ctx.s.structuredAttrs);
    if (attr
        && state.forceBool(
            attr->value,
            attr->pos,
            "while evaluating the `__structuredAttrs` "
            "attribute passed to builtins.derivationStrict"
        ))
    {
        jsonObject = JSON::object();
    }

    /* Check whether null attributes should be ignored. */
    bool ignoreNulls = false;
    attr = attrs->get(state.ctx.s.ignoreNulls);
    if (attr) {
        ignoreNulls = state.forceBool(
            attr->value,
            attr->pos,
            "while evaluating the `__ignoreNulls` attribute "
            "passed to builtins.derivationStrict"
        );
    }

    /* Build the derivation expression by processing the attributes. */
    Derivation drv;
    drv.name = drvName;

    NixStringContext context;

    std::optional<std::string> outputHash;
    std::string outputHashAlgo;
    std::optional<ContentAddressMethod> ingestionMethod;

    StringSet outputs;
    outputs.insert("out");

    for (auto & i : attrs->lexicographicOrder(state.ctx.symbols)) {
        if (i->name == state.ctx.s.ignoreNulls) continue;
        auto & key = state.ctx.symbols[i->name];
        vomit("processing attribute '%1%'", key);

        auto handleHashMode = [&](const std::string_view s, NeverAsync = {}) {
            if (s == "recursive") ingestionMethod = FileIngestionMethod::Recursive;
            else if (s == "flat") ingestionMethod = FileIngestionMethod::Flat;
            else
                state.ctx.errors.make<EvalError>(
                    "invalid value '%s' for 'outputHashMode' attribute", s
                ).debugThrow();
        };

        auto handleOutputs = [&](const Strings & ss, NeverAsync = {}) {
            outputs.clear();
            for (auto & j : ss) {
                if (outputs.find(j) != outputs.end())
                    state.ctx.errors.make<EvalError>("duplicate derivation output '%1%'", j)
                        .debugThrow();
                /* !!! Check whether j is a valid attribute
                   name. */
                /* Derivations cannot be named ‘drv’, because
                   then we'd have an attribute ‘drvPath’ in
                   the resulting set. */
                if (j == "drv")
                    state.ctx.errors.make<EvalError>("invalid derivation output name 'drv'")
                        .debugThrow();
                outputs.insert(j);
            }
            if (outputs.empty())
                state.ctx.errors.make<EvalError>("derivation cannot have an empty set of outputs")
                    .debugThrow();
        };

        try {
            // This try-catch block adds context for most errors.
            // Use this empty error context to signify that we defer to it.
            const std::string_view context_below("");

            if (ignoreNulls) {
                state.forceValue(i->value, noPos);
                if (i->value.type() == nNull) {
                    continue;
                }
            }

            if (i->name == state.ctx.s.contentAddressed
                && state.forceBool(i->value, noPos, context_below))
            {
                state.ctx.errors.make<EvalError>("ca derivations are not supported in Lix")
                    .debugThrow();
            }

            else if (i->name == state.ctx.s.impure
                     && state.forceBool(i->value, noPos, context_below))
            {
                state.ctx.errors.make<EvalError>("impure derivations are not supported in Lix")
                    .debugThrow();
            }

            /* The `args' attribute is special: it supplies the
               command-line arguments to the builder. */
            else if (i->name == state.ctx.s.args)
            {
                state.forceList(i->value, noPos, context_below);
                for (auto & elem : i->value.listItems()) {
                    auto s = state
                                 .coerceToString(
                                     noPos,
                                     elem,
                                     context,
                                     "while evaluating an element of the argument list",
                                     StringCoercionMode::ToString
                                 )
                                 .toOwned();
                    drv.args.push_back(s);
                }
            }

            /* All other attributes are passed to the builder through
               the environment. */
            else
            {

                if (jsonObject) {

                    if (i->name == state.ctx.s.structuredAttrs) continue;

                    (*jsonObject)[std::string(key)] =
                        printValueAsJSON(state, true, i->value, noPos, context);

                    if (i->name == state.ctx.s.builder)
                        drv.builder = state.forceString(i->value, context, noPos, context_below);
                    else if (i->name == state.ctx.s.system)
                        drv.platform = state.forceStringNoCtx(i->value, noPos, context_below);
                    else if (i->name == state.ctx.s.outputHash)
                        outputHash = state.forceStringNoCtx(i->value, noPos, context_below);
                    else if (i->name == state.ctx.s.outputHashAlgo)
                        outputHashAlgo = state.forceStringNoCtx(i->value, noPos, context_below);
                    else if (i->name == state.ctx.s.outputHashMode)
                        handleHashMode(state.forceStringNoCtx(i->value, noPos, context_below));
                    else if (i->name == state.ctx.s.outputs) {
                        /* Require ‘outputs’ to be a list of strings. */
                        state.forceList(i->value, noPos, context_below);
                        Strings ss;
                        for (auto & elem : i->value.listItems()) {
                            ss.emplace_back(state.forceStringNoCtx(elem, noPos, context_below));
                        }
                        handleOutputs(ss);
                    }

                    if (i->name == state.ctx.s.allowedReferences)
                        printTaggedWarning(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of "
                            "the derivation attribute 'allowedReferences'; use "
                            "'outputChecks.<output>.allowedReferences' instead",
                            drvName
                        );
                    if (i->name == state.ctx.s.allowedRequisites)
                        printTaggedWarning(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of "
                            "the derivation attribute 'allowedRequisites'; use "
                            "'outputChecks.<output>.allowedRequisites' instead",
                            drvName
                        );
                    if (i->name == state.ctx.s.disallowedReferences)
                        printTaggedWarning(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of "
                            "the derivation attribute 'disallowedReferences'; use "
                            "'outputChecks.<output>.disallowedReferences' instead",
                            drvName
                        );
                    if (i->name == state.ctx.s.disallowedRequisites)
                        printTaggedWarning(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of "
                            "the derivation attribute 'disallowedRequisites'; use "
                            "'outputChecks.<output>.disallowedRequisites' instead",
                            drvName
                        );
                    if (i->name == state.ctx.s.maxSize)
                        printTaggedWarning(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of "
                            "the derivation attribute 'maxSize'; use "
                            "'outputChecks.<output>.maxSize' instead",
                            drvName
                        );
                    if (i->name == state.ctx.s.maxClosureSize)
                        printTaggedWarning(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of "
                            "the derivation attribute 'maxClosureSize'; use "
                            "'outputChecks.<output>.maxClosureSize' instead",
                            drvName
                        );

                } else {
                    auto s = state
                                 .coerceToString(
                                     noPos,
                                     i->value,
                                     context,
                                     context_below,
                                     StringCoercionMode::ToString
                                 )
                                 .toOwned();
                    drv.env.emplace(key, s);
                    if (i->name == state.ctx.s.builder) {
                        drv.builder = std::move(s);
                    } else if (i->name == state.ctx.s.system)
                        drv.platform = std::move(s);
                    else if (i->name == state.ctx.s.outputHash) outputHash = std::move(s);
                    else if (i->name == state.ctx.s.outputHashAlgo) outputHashAlgo = std::move(s);
                    else if (i->name == state.ctx.s.outputHashMode) handleHashMode(s);
                    else if (i->name == state.ctx.s.outputs)
                        handleOutputs(tokenizeString<Strings>(s));
                }
            }

        } catch (Error & e) {
            e.addTrace(state.ctx.positions[i->pos],
                HintFmt("while evaluating attribute '%1%' of derivation '%2%'", key, drvName));
            throw;
        }
    }

    if (jsonObject) {
        drv.env.emplace("__json", jsonObject->dump());
        jsonObject.reset();
    }

    /* Everything in the context of the strings in the derivation
       attributes should be added as dependencies of the resulting
       derivation. */
    for (auto & c : context) {
        std::visit(overloaded {
            /* Since this allows the builder to gain access to every
               path in the dependency graph of the derivation (including
               all outputs), all paths in the graph must be added to
               this derivation's list of inputs to ensure that they are
               available when the builder runs. */
            [&](const NixStringContextElem::DrvDeep & d) {
                /* !!! This doesn't work if readOnlyMode is set. */
                StorePathSet refs;
                state.aio.blockOn(state.ctx.store->computeFSClosure(d.drvPath, refs));
                for (auto & j : refs) {
                    drv.inputSrcs.insert(j);
                    if (j.isDerivation()) {
                        drv.inputDrvs[j] =
                            state.aio.blockOn(state.ctx.store->readDerivation(j)).outputNames();
                    }
                }
            },
            [&](const NixStringContextElem::Built & b) {
                drv.inputDrvs[b.drvPath.path].insert(b.output);
            },
            [&](const NixStringContextElem::Opaque & o) {
                drv.inputSrcs.insert(o.path);
            },
        }, c.raw);
    }

    /* Do we have all required attributes? */
    if (drv.builder == "")
        state.ctx.errors.make<EvalError>("required attribute 'builder' missing")
            .debugThrow();

    if (drv.platform == "")
        state.ctx.errors.make<EvalError>("required attribute 'system' missing")
            .debugThrow();

    /* Check whether the derivation name is valid. */
    if (isDerivation(drvName)) {
        state.ctx.errors
            .make<EvalError>("derivation names are not allowed to end in '%s'", drvExtension)
            .debugThrow();
    }

    if (outputHash) {
        /* Handle fixed-output derivations.

           Ignore `__contentAddressed` because fixed output derivations are
           already content addressed. */
        if (outputs.size() != 1 || *(outputs.begin()) != "out")
            state.ctx.errors.make<EvalError>(
                "multiple outputs are not supported in fixed-output derivations"
            ).debugThrow();

        auto h = newHashAllowEmpty(*outputHash, parseHashTypeOpt(outputHashAlgo));

        auto method = ingestionMethod.value_or(FileIngestionMethod::Flat);

        DerivationOutput::CAFixed dof {
            .ca = ContentAddress {
                .method = std::move(method),
                .hash = std::move(h),
            },
        };

        drv.env["out"] = state.ctx.store->printStorePath(dof.path(*state.ctx.store, drvName, "out"));
        drv.outputs.insert_or_assign("out", std::move(dof));
    }

    else {
        /* Compute a hash over the "masked" store derivation, which is
           the final one except that in the list of outputs, the
           output paths are empty strings, and the corresponding
           environment variables have an empty value.  This ensures
           that changes in the set of output names do get reflected in
           the hash. */
        for (auto & i : outputs) {
            drv.env[i] = "";
            drv.outputs.insert_or_assign(i,
                DerivationOutput::InputAddressed { .path = StorePath::dummy });
        }

        auto hashModulo =
            state.aio.blockOn(hashDerivationModulo(*state.ctx.store, Derivation(drv), true));
        for (auto & i : outputs) {
            auto h = get(hashModulo.hashes, i);
            if (!h)
                state.ctx.errors.make<AssertionError>(
                    "derivation produced no hash for output '%s'",
                    i
                ).debugThrow();
            auto outPath = state.ctx.store->makeOutputPath(i, *h, drvName);
            drv.env[i] = state.ctx.store->printStorePath(outPath);
            drv.outputs.insert_or_assign(
                i,
                DerivationOutput::InputAddressed {
                    .path = std::move(outPath),
                });
        }
    }

    /* Write the resulting term into the Nix store directory. */
    auto drvPath = state.aio.blockOn(writeDerivation(*state.ctx.store, drv, state.ctx.repair));
    auto drvPathS = state.ctx.store->printStorePath(drvPath);

    printMsg(lvlChatty, "instantiated '%1%' -> '%2%'", drvName, drvPathS);

    /* Optimisation, but required in read-only mode! because in that
       case we don't actually write store derivations, so we can't
       read them later. */
    {
        auto h = state.aio.blockOn(hashDerivationModulo(*state.ctx.store, drv, false));
        drvHashes.lock()->insert_or_assign(drvPath, h);
    }

    auto result = state.ctx.buildBindings(1 + drv.outputs.size());
    result.alloc(state.ctx.s.drvPath).mkString(drvPathS, {
        NixStringContextElem::DrvDeep { .drvPath = drvPath },
    });
    for (auto & i : drv.outputs)
        mkOutputString(state, result, drvPath, i);

    v.mkAttrs(result);
}

static RegisterPrimOp primop_derivationStrict(PrimOp{{
    .name = "derivationStrict",
    .arity = 1,
    .fun = prim_derivationStrict,
}});

/* Return a placeholder string for the specified output that will be
   substituted by the corresponding output path at build time. For
   example, 'placeholder "out"' returns the string
   /1rz4g4znpzjwh1xymhjpm42vipw92pr73vdgl6xs1hycac8kf2n9. At build
   time, any occurrence of this string in an derivation attribute will
   be replaced with the concrete path in the Nix store of the output
   ‘out’. */
static void prim_placeholder(EvalState & state, Value * * args, Value & v)
{
    v.mkString(hashPlaceholder(state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.placeholder")));
}


/*************************************************************
 * Paths
 *************************************************************/


/* Convert the argument to a path.  !!! obsolete? */
static void prim_toPath(EvalState & state, Value * * args, Value & v)
{
    NixStringContext context;
    auto path = state.coerceToPath(noPos, *args[0], context, "while evaluating the first argument passed to builtins.toPath");
    v.mkString(path.to_string(), context);
}

/* Allow a valid store path to be used in an expression.  This is
   useful in some generated expressions such as in nix-push, which
   generates a call to a function with an already existing store path
   as argument.  You don't want to use `toPath' here because it copies
   the path to the Nix store, which yields a copy like
   /nix/store/newhash-oldhash-oldname.  In the past, `toPath' had
   special case behaviour for store paths, but that created weird
   corner cases. */
static void prim_storePath(EvalState & state, Value * * args, Value & v)
{
    if (evalSettings.pureEval)
        state.ctx.errors.make<EvalError>(
            "'%s' is not allowed in pure evaluation mode",
            "builtins.storePath"
        ).debugThrow();

    NixStringContext context;
    auto path = state.ctx.paths.checkSourcePath(state.coerceToPath(noPos, *args[0], context, "while evaluating the first argument passed to builtins.storePath")).canonical();
    /* Resolve symlinks in ‘path’, unless ‘path’ itself is a symlink
       directly in the store.  The latter condition is necessary so
       e.g. nix-push does the right thing. */
    if (!state.ctx.store->isStorePath(path.abs()))
        path = CanonPath(canonPath(path.abs(), true));
    if (!state.ctx.store->isInStore(path.abs()))
        state.ctx.errors.make<EvalError>("path '%1%' is not in the Nix store", path)
            .debugThrow();
    auto path2 = state.ctx.store->toStorePath(path.abs()).first;
    if (!settings.readOnlyMode)
        state.aio.blockOn(state.ctx.store->ensurePath(path2));
    context.insert(NixStringContextElem::Opaque { .path = path2 });
    v.mkString(path.abs(), context);
}

static void prim_pathExists(EvalState & state, Value * * args, Value & v)
{
    auto & arg = *args[0];

    /* We don’t check the path right now, because we don’t want to
       throw if the path isn’t allowed, but just return false (and we
       can’t just catch the exception here because we still want to
       throw if something in the evaluation of `arg` tries to
       access an unauthorized path). */
    auto path = realisePath(state, arg, std::identity{});

    /* SourcePath doesn't know about trailing slash. */
    auto mustBeDir = arg.type() == nString
        && (arg.str().ends_with("/")
            || arg.str().ends_with("/."));

    try {
        auto checked = state.ctx.paths.checkSourcePath(path);

        // previously we fully resolved symlinks in the mustBeDir case or in pure eval
        // mode (by accident, since checkSourcePath does this in that case), and up to
        // the last component otherwise. this is equivalent to calling stat and lstat,
        // respectively. (in neither case do intermediate symlinks affect the result.)
        auto st = mustBeDir ? checked.maybeStat() : checked.maybeLstat();
        auto exists = st && (!mustBeDir || st->type == InputAccessor::tDirectory);
        v.mkBool(exists);
    } catch (SysError & e) {
        /* Don't give away info from errors while canonicalising
           ‘path’ in restricted mode. */
        v.mkBool(false);
    } catch (RestrictedPathError & e) {
        v.mkBool(false);
    }
}

/* Return the base name of the given string, i.e., everything
   following the last slash. */
static void prim_baseNameOf(EvalState & state, Value * * args, Value & v)
{
    NixStringContext context;
    v.mkString(baseNameOf(*state.coerceToString(noPos, *args[0], context,
            "while evaluating the first argument passed to builtins.baseNameOf",
            StringCoercionMode::Strict, false)), context);
}

/* Return the directory of the given path, i.e., everything before the
   last slash.  Return either a path or a string depending on the type
   of the argument. */
static void prim_dirOf(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    if (args[0]->type() == nPath) {
        auto path = args[0]->path();
        v.mkPath(path.canonical().isRoot() ? path : path.parent());
    } else {
        NixStringContext context;
        auto path = state.coerceToString(noPos, *args[0], context,
            "while evaluating the first argument passed to 'builtins.dirOf'",
            StringCoercionMode::Strict, false);
        auto dir = dirOf(*path);
        v.mkString(dir, context);
    }
}

/* Return the contents of a file as a string. */
static void prim_readFile(EvalState & state, Value * * args, Value & v)
{
    auto path = realisePath(state, *args[0]);
    auto s = path.readFile();
    if (s.find((char) 0) != std::string::npos)
        state.ctx.errors.make<EvalError>(
            "the contents of the file '%1%' cannot be represented as a Nix string",
            path
        ).debugThrow();
    StorePathSet refs;
    if (state.ctx.store->isInStore(path.canonical().abs())) {
        try {
            refs = state.aio
                       .blockOn(state.ctx.store->queryPathInfo(
                           state.ctx.store->toStorePath(path.canonical().abs()).first
                       ))
                       ->references;
        } catch (Error &) { // FIXME: should be InvalidPathError
        }
        // Re-scan references to filter down to just the ones that actually occur in the file.
        auto refsSink = PathRefScanSink::fromPaths(refs);
        refsSink << s;
        refs = refsSink.getResultPaths();
    }
    NixStringContext context;
    for (auto && p : std::move(refs)) {
        context.insert(NixStringContextElem::Opaque {
            .path = std::move((StorePath &&)p),
        });
    }
    v.mkString(s, context);
}

/* Find a file in the Nix search path. Used to implement <x> paths,
   which are desugared to 'findFile __nixPath "x"'. */
static void prim_findFile(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[0], noPos, "while evaluating the first argument passed to builtins.findFile");

    SearchPath searchPath;

    for (auto & v2 : args[0]->listItems()) {
        state.forceAttrs(
            v2, noPos, "while evaluating an element of the list passed to builtins.findFile"
        );

        std::string prefix;
        auto i = v2.attrs()->get(state.ctx.s.prefix);
        if (i) {
            prefix = state.forceStringNoCtx(
                i->value,
                noPos,
                "while evaluating the `prefix` attribute of an element of the list passed to "
                "builtins.findFile"
            );
        }

        i = getAttr(state, state.ctx.s.path, v2.attrs(), "in an element of the __nixPath");

        NixStringContext context;
        auto path = state
                        .coerceToString(
                            noPos,
                            i->value,
                            context,
                            "while evaluating the `path` attribute of an element of the list "
                            "passed to builtins.findFile",
                            StringCoercionMode::Strict,
                            false
                        )
                        .toOwned();

        try {
            auto rewrites = state.realiseContext(context);
            path = rewriteStrings(path, rewrites);
        } catch (InvalidPathError & e) {
            state.ctx.errors.make<EvalError>(
                "cannot find '%1%', since path '%2%' is not valid",
                path,
                e.path
            ).debugThrow();
        }

        searchPath.elements.emplace_back(SearchPath::Elem {
            .prefix = SearchPath::Prefix { .s = prefix },
            .path = SearchPath::Path { .s = path },
        });
    }

    auto path = state.forceStringNoCtx(*args[1], noPos, "while evaluating the second argument passed to builtins.findFile");

    v.mkPath(state.ctx.paths.checkSourcePath(
        state.aio.blockOn(state.ctx.paths.findFile(searchPath, path, noPos)).unwrap()
    ));
}

/* Return the cryptographic hash of a file in base-16. */
static void prim_hashFile(EvalState & state, Value * * args, Value & v)
{
    auto type = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.hashFile");
    std::optional<HashType> ht = parseHashType(type);
    if (!ht)
        state.ctx.errors.make<EvalError>("unknown hash type '%1%'", type).debugThrow();

    auto path = realisePath(state, *args[1]);

    v.mkString(hashString(*ht, path.readFile()).to_string(Base::Base16, false));
}

static std::string_view fileTypeToString(InputAccessor::Type type)
{
    return
        type == InputAccessor::Type::tRegular ? "regular" :
        type == InputAccessor::Type::tDirectory ? "directory" :
        type == InputAccessor::Type::tSymlink ? "symlink" :
        "unknown";
}

static void prim_readFileType(EvalState & state, Value * * args, Value & v)
{
    auto path = realisePath(state, *args[0]);
    /* Retrieve the directory entry type and stringize it. */
    v.mkString(fileTypeToString(path.lstat().type));
}

/* Read a directory (without . or ..) */
static void prim_readDir(EvalState & state, Value * * args, Value & v)
{
    auto path = realisePath(state, *args[0]);

    // Retrieve directory entries for all nodes in a directory.
    // This is similar to `getFileType` but is optimized to reduce system calls
    // on many systems.
    auto entries = path.readDirectory();
    auto attrs = state.ctx.buildBindings(entries.size());

    // If we hit unknown directory entry types we may need to fallback to
    // using `getFileType` on some systems.
    // In order to reduce system calls we make each lookup lazy by using
    // `builtins.readFileType` application.
    Value * readFileType = nullptr;

    for (auto & [name, type] : entries) {
        auto & attr = attrs.alloc(name);
        if (!type) {
            // Some filesystems or operating systems may not be able to return
            // detailed node info quickly in this case we produce a thunk to
            // query the file type lazily.
            Value epath;
            epath.mkPath(path + name);
            if (!readFileType)
                readFileType = &state.ctx.builtins.get("readFileType");
            attr = {NewValueAs::app, state.ctx.mem, *readFileType, epath};
        } else {
            // This branch of the conditional is much more likely.
            // Here we just stringize the directory entry type.
            attr.mkString(fileTypeToString(*type));
        }
    }

    v.mkAttrs(attrs);
}

/*************************************************************
 * Creating files
 *************************************************************/


/* Convert the argument (which can be any Nix expression) to an XML
   representation returned in a string.  Not all Nix expressions can
   be sensibly or completely represented (e.g., functions). */
static void prim_toXML(EvalState & state, Value * * args, Value & v)
{
    std::ostringstream out;
    NixStringContext context;
    printValueAsXML(state, true, false, *args[0], out, context, noPos);
    v.mkString(out.str(), context);
}

/* Convert the argument (which can be any Nix expression) to a JSON
   string.  Not all Nix expressions can be sensibly or completely
   represented (e.g., functions). */
static void prim_toJSON(EvalState & state, Value * * args, Value & v)
{
    std::ostringstream out;
    NixStringContext context;
    printValueAsJSON(state, true, *args[0], noPos, out, context);
    v.mkString(out.str(), context);
}

/* Parse a JSON string to a value. */
static void prim_fromJSON(EvalState & state, Value * * args, Value & v)
{
    auto s = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.fromJSON");
    try {
        parseJSON(state, s, v);
    } catch (JSONParseError &e) {
        e.addTrace(nullptr, "while decoding a JSON string");
        throw;
    }
}

/* Store a string in the Nix store as a source file that can be used
   as an input by derivations. */
static void prim_toFile(EvalState & state, Value * * args, Value & v)
{
    NixStringContext context;
    std::string name(state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.toFile"));
    std::string contents(state.forceString(*args[1], context, noPos, "while evaluating the second argument passed to builtins.toFile"));

    StorePathSet refs;

    for (auto c : context) {
        if (auto p = std::get_if<NixStringContextElem::Opaque>(&c.raw))
            refs.insert(p->path);
        else
            state.ctx.errors.make<EvalError>(
                "files created by %1% may not reference derivations, but %2% references %3%",
                "builtins.toFile",
                name,
                c.to_string()
            ).debugThrow();
    }

    auto storePath = settings.readOnlyMode
        ? state.ctx.store->computeStorePathForText(name, contents, refs)
        : state.aio.blockOn(state.ctx.store->addTextToStore(name, contents, refs, state.ctx.repair));

    /* Note: we don't need to add `context' to the context of the
       result, since `storePath' itself has references to the paths
       used in args[1]. */

    /* Add the output of this to the allowed paths. */
    state.ctx.paths.allowAndSetStorePathString(storePath, v);
}

static void addPath(
    EvalState & state,
    std::string_view name,
    Path path,
    Value * filterFun,
    FileIngestionMethod method,
    const std::optional<Hash> expectedHash,
    Value & v,
    const NixStringContext & context)
{
    try {
        // FIXME: handle CA derivation outputs (where path needs to
        // be rewritten to the actual output).
        auto rewrites = state.realiseContext(context);
        path = rewriteStrings(path, rewrites);

        Path realPath = path;

        StorePathSet refs;

        // If the path is in the store, it can mean either a physical path or a logical path in a
        // chroot store. Query the chroot store for its presence to find out which is the case.
        if (state.ctx.store->isInStore(path)) {
            try {
                auto [storePath, subPath] = state.ctx.store->toStorePath(path);
                // FIXME: we should scanForReferences on the path before adding it
                refs = state.aio.blockOn(state.ctx.store->queryPathInfo(storePath))->references;
                realPath = state.ctx.store->toRealPath(path);
            } catch (Error &) { // FIXME: should be InvalidPathError
            }
        }

        realPath = evalSettings.pureEval && expectedHash
            ? realPath
            : state.ctx.paths.checkSourcePath(CanonPath(realPath)).canonical().abs();

        PathFilter filter = filterFun ? ([&](const Path & p) {
            auto st = lstat(p);

            /* Call the filter function.  The first argument is the path,
               the second is a string indicating the type of the file. */
            Value arg1;
            if (isInDir(p, realPath))
                arg1.mkString(path + "/" + std::string(p, realPath.size() + 1));
            else
                arg1.mkString(p);

            Value arg2;
            arg2.mkString(
                S_ISREG(st.st_mode) ? "regular" :
                S_ISDIR(st.st_mode) ? "directory" :
                S_ISLNK(st.st_mode) ? "symlink" :
                "unknown" /* not supported, will fail! */);

            Value args[]{arg1, arg2};
            Value res;
            state.callFunction(*filterFun, args, res, noPos);

            return state.forceBool(res, noPos, "while evaluating the return value of the path filter function");
        }) : defaultPathFilter;

        std::optional<StorePath> expectedStorePath;
        if (expectedHash)
            expectedStorePath = state.ctx.store->makeFixedOutputPath(name, FixedOutputInfo {
                .method = method,
                .hash = *expectedHash,
                .references = {},
            });

        if (!expectedHash || !state.aio.blockOn(state.ctx.store->isValidPath(*expectedStorePath))) {
            auto checkedPath = state.ctx.paths.checkSourcePath(CanonPath(realPath));
            auto dstPath = state.aio.blockOn(
                method == FileIngestionMethod::Flat
                    ? fetchToStoreFlat(*state.ctx.store, checkedPath, name, state.ctx.repair)
                    : fetchToStoreRecursive(
                          *state.ctx.store,
                          *prepareDump(checkedPath.canonical().abs(), filter),
                          name,
                          state.ctx.repair
                      )
            );
            if (expectedHash && expectedStorePath != dstPath)
                state.ctx.errors.make<EvalError>(
                    "store path mismatch in (possibly filtered) path added from '%s'",
                    path
                ).debugThrow();
            state.ctx.paths.allowAndSetStorePathString(dstPath, v);
        } else
            state.ctx.paths.allowAndSetStorePathString(*expectedStorePath, v);
    } catch (Error & e) {
        e.addTrace(nullptr, "while adding path '%s'", path);
        throw;
    }
}


static void prim_filterSource(EvalState & state, Value * * args, Value & v)
{
    NixStringContext context;
    auto path = state.coerceToPath(noPos, *args[1], context,
        "while evaluating the second argument (the path to filter) passed to builtins.filterSource");
    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.filterSource");
    addPath(state, path.baseName(), path.canonical().abs(), args[0], FileIngestionMethod::Recursive, std::nullopt, v, context);
}

static void prim_path(EvalState & state, Value * * args, Value & v)
{
    std::optional<SourcePath> path;
    std::string name;
    Value * filterFun = nullptr;
    auto method = FileIngestionMethod::Recursive;
    std::optional<Hash> expectedHash;
    NixStringContext context;

    state.forceAttrs(*args[0], noPos, "while evaluating the argument passed to 'builtins.path'");

    for (auto & attr : *args[0]->attrs()) {
        auto & n = state.ctx.symbols[attr.name];
        if (n == "path") {
            path.emplace(state.coerceToPath(
                attr.pos,
                attr.value,
                context,
                "while evaluating the 'path' attribute passed to 'builtins.path'"
            ));
        } else if (attr.name == state.ctx.s.name) {
            name = state.forceStringNoCtx(
                attr.value,
                attr.pos,
                "while evaluating the `name` attribute passed to builtins.path"
            );
        } else if (n == "filter") {
            state.forceFunction(
                *(filterFun = &attr.value),
                attr.pos,
                "while evaluating the `filter` parameter passed to builtins.path"
            );
        } else if (n == "recursive") {
            method = FileIngestionMethod{state.forceBool(
                attr.value,
                attr.pos,
                "while evaluating the `recursive` attribute passed to builtins.path"
            )};
        } else if (n == "sha256") {
            expectedHash = newHashAllowEmpty(
                state.forceStringNoCtx(
                    attr.value,
                    attr.pos,
                    "while evaluating the `sha256` attribute passed to builtins.path"
                ),
                HashType::SHA256
            );
        } else {
            state.ctx.errors
                .make<EvalError>(
                    "unsupported argument '%1%' to 'addPath'", state.ctx.symbols[attr.name]
                )
                .atPos(attr.pos)
                .debugThrow();
        }
    }
    if (!path)
        state.ctx.errors.make<EvalError>(
            "missing required 'path' attribute in the first argument to builtins.path"
        ).debugThrow();
    if (name.empty())
        name = path->baseName();

    addPath(state, name, path->canonical().abs(), filterFun, method, expectedHash, v, context);
}


/*************************************************************
 * Sets
 *************************************************************/


/* Return the names of the attributes in a set as a sorted list of
   strings. */
static void prim_attrNames(EvalState & state, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], noPos, "while evaluating the argument passed to builtins.attrNames");

    auto result = state.ctx.mem.newList(args[0]->attrs()->size());
    v = {NewValueAs::list, result};

    size_t n = 0;
    for (auto & i : *args[0]->attrs())
        result->elems[n++] = state.ctx.symbols[i.name].toValue();

    std::sort(result->elems, result->elems + n, [](Value & v1, Value & v2) {
        return v1.str() < v2.str();
    });
}

/* Return the values of the attributes in a set as a list, in the same
   order as attrNames. */
static void prim_attrValues(EvalState & state, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], noPos, "while evaluating the argument passed to builtins.attrValues");

    auto result = state.ctx.mem.newList(args[0]->attrs()->size());
    v = {NewValueAs::list, result};

    boost::container::small_vector<const Attr *, 128> tmp;
    tmp.reserve(args[0]->attrs()->size());

    for (auto & i : *args[0]->attrs())
        tmp.push_back(&i);

    std::sort(tmp.begin(), tmp.end(), [&](const Attr * v1, const Attr * v2) {
        std::string_view s1 = state.ctx.symbols[v1->name], s2 = state.ctx.symbols[v2->name];
        return s1 < s2;
    });

    for (auto [i, attr] : enumerate(tmp)) {
        result->elems[i] = attr->value;
    }
}

/* Dynamic version of the `.' operator. */
void prim_getAttr(EvalState & state, Value * * args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.getAttr");
    state.forceAttrs(*args[1], noPos, "while evaluating the second argument passed to builtins.getAttr");
    auto i = getAttr(
        state,
        state.ctx.symbols.create(attr),
        args[1]->attrs(),
        "in the attribute set under consideration"
    );
    // !!! add to stack trace?
    if (state.ctx.stats.countCalls && i->pos) state.ctx.stats.attrSelects[i->pos]++;
    state.forceValue(i->value, noPos);
    v = i->value;
}

/* Return position information of the specified attribute. */
static void prim_unsafeGetAttrPos(EvalState & state, Value * * args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.unsafeGetAttrPos");
    state.forceAttrs(*args[1], noPos, "while evaluating the second argument passed to builtins.unsafeGetAttrPos");
    auto i = args[1]->attrs()->get(state.ctx.symbols.create(attr));
    if (!i) {
        v.mkNull();
    } else {
        state.mkPos(v, i->pos);
    }
}

// access to exact position information (ie, line and colum numbers) is deferred
// due to the cost associated with calculating that information and how rarely
// it is used in practice. this is achieved by creating thunks to otherwise
// inaccessible primops that are not exposed as __op or under builtins to turn
// the internal PosIdx back into a line and column number, respectively. exposing
// these primops in any way would at best be not useful and at worst create wildly
// indeterministic eval results depending on parse order of files.
//
// in a simpler world this would instead be implemented as another kind of thunk,
// but each type of thunk has an associated runtime cost in the current evaluator.
// as with black holes this cost is too high to justify another thunk type to check
// for in the very hot path that is forceValue.
static struct LazyPosAcessors {
    PrimOp primop_lineOfPos{{.arity = 1, .fun = [](EvalState & state, Value ** args, Value & v) {
                                 v.mkInt(state.ctx.positions[PosIdx(args[0]->integer().value)].line
                                 );
                             }}};
    PrimOp primop_columnOfPos{{.arity = 1, .fun = [](EvalState & state, Value ** args, Value & v) {
                                   v.mkInt(
                                       state.ctx.positions[PosIdx(args[0]->integer().value)].column
                                   );
                               }}};

    Value lineOfPos, columnOfPos;

    LazyPosAcessors()
    {
        lineOfPos.mkPrimOp(&primop_lineOfPos);
        columnOfPos.mkPrimOp(&primop_columnOfPos);
    }

    void operator()(EvalState & state, const PosIdx pos, Value & line, Value & column)
    {
        Value posV{NewValueAs::integer, NixInt{pos.id}};
        line = {NewValueAs::app, state.ctx.mem, lineOfPos, posV};
        column = {NewValueAs::app, state.ctx.mem, columnOfPos, posV};
    }
} makeLazyPosAccessors;

void makePositionThunks(EvalState & state, const PosIdx pos, Value & line, Value & column)
{
    makeLazyPosAccessors(state, pos, line, column);
}

/* Dynamic version of the `?' operator. */
static void prim_hasAttr(EvalState & state, Value * * args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.hasAttr");
    state.forceAttrs(*args[1], noPos, "while evaluating the second argument passed to builtins.hasAttr");
    v.mkBool(args[1]->attrs()->get(state.ctx.symbols.create(attr)));
}

/* Determine whether the argument is a set. */
static void prim_isAttrs(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    v.mkBool(args[0]->type() == nAttrs);
}

static void prim_removeAttrs(EvalState & state, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], noPos, "while evaluating the first argument passed to builtins.removeAttrs");
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.removeAttrs");

    /* Get the attribute names to be removed.
       We keep them as Attrs instead of Symbols so std::set_difference
       can be used to remove them from attrs[0]. */
    // 64: large enough to fit the attributes of a derivation
    boost::container::small_vector<Attr, 64> names;
    names.reserve(args[1]->listSize());
    for (auto & elem : args[1]->listItems()) {
        state.forceStringNoCtx(
            elem,
            noPos,
            "while evaluating the values of the second argument passed to builtins.removeAttrs"
        );
        names.emplace_back(state.ctx.symbols.create(elem.str()), Value());
    }
    std::sort(names.begin(), names.end());

    /* Copy all attributes not in that set.  Note that we don't need
       to sort v.attrs because it's a subset of an already sorted
       vector. */
    auto attrs = state.ctx.buildBindings(args[0]->attrs()->size());
    std::set_difference(
        args[0]->attrs()->begin(), args[0]->attrs()->end(),
        names.begin(), names.end(),
        std::back_inserter(attrs));
    v.mkAttrs(attrs.alreadySorted());
}

/* Builds a set from a list specifying (name, value) pairs.  To be
   precise, a list [{name = "name1"; value = value1;} ... {name =
   "nameN"; value = valueN;}] is transformed to {name1 = value1;
   ... nameN = valueN;}.  In case of duplicate occurrences of the same
   name, the first takes precedence. */
static void prim_listToAttrs(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[0], noPos, "while evaluating the argument passed to builtins.listToAttrs");

    auto attrs = state.ctx.buildBindings(args[0]->listSize());

    std::set<Symbol> seen;

    for (auto & v2 : args[0]->listItems()) {
        state.forceAttrs(
            v2, noPos, "while evaluating an element of the list passed to builtins.listToAttrs"
        );

        auto j = getAttr(state, state.ctx.s.name, v2.attrs(), "in a {name=...; value=...;} pair");

        auto name = state.forceStringNoCtx(
            j->value,
            j->pos,
            "while evaluating the `name` attribute of an element of the list passed to "
            "builtins.listToAttrs"
        );

        auto sym = state.ctx.symbols.create(name);
        if (seen.insert(sym).second) {
            auto j2 =
                getAttr(state, state.ctx.s.value, v2.attrs(), "in a {name=...; value=...;} pair");
            attrs.insert(sym, j2->value, j2->pos);
        }
    }

    v.mkAttrs(attrs);
}

static void prim_intersectAttrs(EvalState & state, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], noPos, "while evaluating the first argument passed to builtins.intersectAttrs");
    state.forceAttrs(*args[1], noPos, "while evaluating the second argument passed to builtins.intersectAttrs");

    Bindings &left = *args[0]->attrs();
    Bindings &right = *args[1]->attrs();

    auto attrs = state.ctx.buildBindings(std::min(left.size(), right.size()));

    // The current implementation has good asymptotic complexity and is reasonably
    // simple. Further optimization may be possible, but does not seem productive,
    // considering the state of eval performance in 2022.
    //
    // I have looked for reusable and/or standard solutions and these are my
    // findings:
    //
    // STL
    // ===
    // std::set_intersection is not suitable, as it only performs a simultaneous
    // linear scan; not taking advantage of random access. This is O(n + m), so
    // linear in the largest set, which is not acceptable for callPackage in Nixpkgs.
    //
    // Simultaneous scan, with alternating simple binary search
    // ===
    // One alternative algorithm scans the attrsets simultaneously, jumping
    // forward using `lower_bound` in case of inequality. This should perform
    // well on very similar sets, having a local and predictable access pattern.
    // On dissimilar sets, it seems to need more comparisons than the current
    // algorithm, as few consecutive attrs match. `lower_bound` could take
    // advantage of the decreasing remaining search space, but this causes
    // the medians to move, which can mean that they don't stay in the cache
    // like they would with the current naive `find`.
    //
    // Double binary search
    // ===
    // The optimal algorithm may be "Double binary search", which doesn't
    // scan at all, but rather divides both sets simultaneously.
    // See "Fast Intersection Algorithms for Sorted Sequences" by Baeza-Yates et al.
    // https://cs.uwaterloo.ca/~ajsaling/papers/intersection_alg_app10.pdf
    // The only downsides I can think of are not having a linear access pattern
    // for similar sets, and having to maintain a more intricate algorithm.
    //
    // Adaptive
    // ===
    // Finally one could run try a simultaneous scan, count misses and fall back
    // to double binary search when the counter hit some threshold and/or ratio.

    if (left.size() < right.size()) {
        for (auto & l : left) {
            auto r = right.get(l.name);
            if (r) {
                attrs.insert(*r);
            }
        }
    }
    else {
        for (auto & r : right) {
            auto l = left.get(r.name);
            if (l) {
                attrs.insert(r);
            }
        }
    }

    v.mkAttrs(attrs.alreadySorted());
}

static void prim_catAttrs(EvalState & state, Value * * args, Value & v)
{
    auto attrName = state.ctx.symbols.create(state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.catAttrs"));
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.catAttrs");

    SmallValueVector<nonRecursiveStackReservation> res(args[1]->listSize());
    size_t found = 0;

    for (auto & v2 : args[1]->listItems()) {
        state.forceAttrs(
            v2,
            noPos,
            "while evaluating an element in the list passed as second argument to builtins.catAttrs"
        );
        auto i = v2.attrs()->get(attrName);
        if (i) {
            res[found++] = i->value;
        }
    }

    auto result = state.ctx.mem.newList(found);
    v = {NewValueAs::list, result};
    for (size_t n = 0; n < found; ++n) {
        result->elems[n] = res[n];
    }
}

static void prim_functionArgs(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    if (args[0]->isPrimOpApp() || args[0]->isPrimOp()) {
        v.mkAttrs(&Bindings::EMPTY);
        return;
    }
    if (!args[0]->isLambda())
        state.ctx.errors.make<TypeError>("'functionArgs' requires a function").debugThrow();

    AttrsPattern * formals = dynamic_cast<AttrsPattern *>(args[0]->lambda().fun->pattern.get());
    if (!formals) {
        v.mkAttrs(&Bindings::EMPTY);
        return;
    }

    auto attrs = state.ctx.buildBindings(formals->formals.size());
    for (auto & i : formals->formals)
        // !!! should optimise booleans (allocate only once)
        attrs.alloc(i.name, i.pos).mkBool(i.def != nullptr);
    v.mkAttrs(attrs);
}

/*  */
static void prim_mapAttrs(EvalState & state, Value * * args, Value & v)
{
    state.forceAttrs(*args[1], noPos, "while evaluating the second argument passed to builtins.mapAttrs");

    auto attrs = state.ctx.buildBindings(args[1]->attrs()->size());

    for (auto & i : *args[1]->attrs()) {
        auto vName = state.ctx.symbols[i.name].toValue();
        Value appArgs[] = {vName, i.value};
        attrs.alloc(i.name) = {NewValueAs::app, state.ctx.mem, *args[0], appArgs};
    }

    v.mkAttrs(attrs.alreadySorted());
}

static void prim_zipAttrsWith(EvalState & state, Value * * args, Value & v)
{
    // we will first count how many values are present for each given key.
    // we then allocate a single attrset and pre-populate it with lists of
    // appropriate sizes, stash the pointers to the list elements of each,
    // and populate the lists. after that we replace the list in the every
    // attribute with the merge function application. this way we need not
    // use (slightly slower) temporary storage the GC does not know about.

    std::map<Symbol, std::pair<size_t, Value *>> attrsSeen;

    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.zipAttrsWith");
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.zipAttrsWith");
    const auto listSize = args[1]->listSize();
    const auto listElems = args[1]->listElems();

    for (unsigned int n = 0; n < listSize; ++n) {
        Value & vElem = listElems[n];
        state.forceAttrs(
            vElem,
            noPos,
            "while evaluating a value of the list passed as second argument to "
            "builtins.zipAttrsWith"
        );
        for (auto & attr : *vElem.attrs()) {
            attrsSeen[attr.name].first++;
        }
    }

    auto attrs = state.ctx.buildBindings(attrsSeen.size());
    for (auto & [sym, elem] : attrsSeen) {
        /* Take care of the returned lists. */
        auto content = state.ctx.mem.newList(elem.first);
        Value list{NewValueAs::list, content};
        elem.second = content->elems;

        /* Construct a `fn name list` function call value. */
        auto name = state.ctx.symbols[sym].toValue();
        Value callArgs[] = {name, list};
        Value call{NewValueAs::app, state.ctx.mem, *args[0], callArgs};

        /* Insert it inside the returned attribute set. */
        attrs.insert(sym, call);
    }

    /* Populate the lists inside the attribute set */
    for (unsigned int n = 0; n < listSize; ++n) {
        Value & vElem = listElems[n];
        for (auto & attr : *vElem.attrs()) {
            *attrsSeen[attr.name].second++ = attr.value;
        }
    }

    v.mkAttrs(attrs.alreadySorted());
}


/*************************************************************
 * Lists
 *************************************************************/


/* Determine whether the argument is a list. */
static void prim_isList(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    v.mkBool(args[0]->type() == nList);
}

static void elemAt(EvalState & state, Value & list, NixInt::Inner n, Value & v)
{
    state.forceList(list, noPos, "while evaluating the first argument passed to builtins.elemAt");
    if (n < 0 || std::make_unsigned_t<NixInt::Inner>(n) >= list.listSize()) {
        state.ctx.errors.make<EvalError>("list index %1% is out of bounds", n).debugThrow();
    }
    state.forceValue(list.listElems()[n], noPos);
    v = list.listElems()[n];
}

/* Return the n-1'th element of a list. */
static void prim_elemAt(EvalState & state, Value * * args, Value & v)
{
    NixInt::Inner elem = state.forceInt(*args[1], noPos, "while evaluating the second argument passed to builtins.elemAt").value;
    elemAt(state, *args[0], elem, v);
}

/* Return the first element of a list. */
static void prim_head(EvalState & state, Value * * args, Value & v)
{
    elemAt(state, *args[0], 0, v);
}

/* Return a list consisting of everything but the first element of
   a list.  Warning: this function takes O(n) time, so you probably
   don't want to use it!  */
static void prim_tail(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[0], noPos, "while evaluating the first argument passed to builtins.tail");
    if (args[0]->listSize() == 0)
        state.ctx.errors.make<EvalError>("'tail' called on an empty list").debugThrow();

    auto result = state.ctx.mem.newList(args[0]->listSize() - 1);
    v = {NewValueAs::list, result};
    for (unsigned int n = 0; n < v.listSize(); ++n)
        result->elems[n] = args[0]->listElems()[n + 1];
}

/* Apply a function to every element of a list. */
static void prim_map(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.map");

    if (args[1]->listSize() == 0) {
        v = *args[1];
        return;
    }

    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.map");

    auto result = state.ctx.mem.newList(args[1]->listSize());
    v = {NewValueAs::list, result};
    for (unsigned int n = 0; n < v.listSize(); ++n) {
        result->elems[n] = {NewValueAs::app, state.ctx.mem, *args[0], args[1]->listElems()[n]};
    }
}

/* Filter a list using a predicate; that is, return a list containing
   every element from the list for which the predicate function
   returns true. */
static void prim_filter(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.filter");

    if (args[1]->listSize() == 0) {
        v = *args[1];
        return;
    }

    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.filter");

    auto len = args[1]->listSize();
    SmallValueVector<nonRecursiveStackReservation> vs(len);
    size_t k = 0;

    bool same = true;
    for (size_t n = 0; n < len; ++n) {
        Value res;
        state.callFunction(*args[0], args[1]->listElems()[n], res, noPos);
        if (state.forceBool(res, noPos, "while evaluating the return value of the filtering function passed to builtins.filter"))
            vs[k++] = args[1]->listElems()[n];
        else
            same = false;
    }

    if (same)
        v = *args[1];
    else {
        auto result = state.ctx.mem.newList(k);
        v = {NewValueAs::list, result};
        for (unsigned int n = 0; n < k; ++n) {
            result->elems[n] = vs[n];
        }
    }
}

/* Return true if a list contains a given element. */
static void prim_elem(EvalState & state, Value * * args, Value & v)
{
    bool res = false;
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.elem");
    for (auto & elem : args[1]->listItems()) {
        if (state.eqValues(
                *args[0],
                elem,
                noPos,
                "while searching for the presence of the given element in the list"
            ))
        {
            res = true;
            break;
        }
    }
    v.mkBool(res);
}

/* Concatenate a list of lists. */
static void prim_concatLists(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[0], noPos, "while evaluating the first argument passed to builtins.concatLists");
    state.concatLists(
        v,
        std::span{args[0]->listElems(), args[0]->listSize()},
        noPos,
        "while evaluating a value of the list passed to builtins.concatLists"
    );
}

/* Return the length of a list.  This is an O(1) time operation. */
static void prim_length(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[0], noPos, "while evaluating the first argument passed to builtins.length");
    v.mkInt(args[0]->listSize());
}

/* Reduce a list by applying a binary operator, from left to
   right. The operator is applied strictly. */
static void prim_foldlStrict(EvalState & state, Value * * args, Value & v)
{
    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.foldlStrict");
    state.forceList(*args[2], noPos, "while evaluating the third argument passed to builtins.foldlStrict");

    if (args[2]->listSize()) {
        Value vCur = *args[1];

        for (auto && [n, elem] : enumerate(args[2]->listItems())) {
            Value vs[]{vCur, elem};
            state.callFunction(*args[0], vs, vCur, noPos);
        }
        v = vCur;
        state.forceValue(v, noPos);
    } else {
        state.forceValue(*args[1], noPos);
        v = *args[1];
    }
}

static void anyOrAll(bool any, EvalState & state, Value * * args, Value & v)
{
    state.forceFunction(*args[0], noPos, std::string("while evaluating the first argument passed to builtins.") + (any ? "any" : "all"));
    state.forceList(*args[1], noPos, std::string("while evaluating the second argument passed to builtins.") + (any ? "any" : "all"));

    std::string_view errorCtx = any
        ? "while evaluating the return value of the function passed to builtins.any"
        : "while evaluating the return value of the function passed to builtins.all";

    Value vTmp;
    for (auto & elem : args[1]->listItems()) {
        state.callFunction(*args[0], elem, vTmp, noPos);
        bool res = state.forceBool(vTmp, noPos, errorCtx);
        if (res == any) {
            v.mkBool(any);
            return;
        }
    }

    v.mkBool(!any);
}


static void prim_any(EvalState & state, Value * * args, Value & v)
{
    anyOrAll(true, state, args, v);
}

static void prim_all(EvalState & state, Value * * args, Value & v)
{
    anyOrAll(false, state, args, v);
}

static void prim_genList(EvalState & state, Value * * args, Value & v)
{
    auto len_ = state.forceInt(*args[1], noPos, "while evaluating the second argument passed to builtins.genList").value;

    if (len_ < 0 || std::make_unsigned_t<NixInt::Inner>(len_) > std::numeric_limits<size_t>::max())
    {
        state.ctx.errors.make<EvalError>("cannot create list of size %1%", len_).debugThrow();
    }

    size_t len = len_;

    // More strict than striclty (!) necessary, but acceptable
    // as evaluating map without accessing any values makes little sense.
    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.genList");

    auto result = state.ctx.mem.newList(len);
    v = {NewValueAs::list, result};
    for (size_t n = 0; n < len; ++n) {
        Value arg{NewValueAs::integer, NixInt{ssize_t(n)}};
        result->elems[n] = {NewValueAs::app, state.ctx.mem, *args[0], arg};
    }
}

static void prim_lessThan(EvalState & state, Value * * args, Value & v);


static void prim_sort(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.sort");

    auto len = args[1]->listSize();
    if (len == 0) {
        v = *args[1];
        return;
    }

    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.sort");

    auto list = state.ctx.mem.newList(len);
    v = {NewValueAs::list, list};
    for (unsigned int n = 0; n < len; ++n) {
        state.forceValue(args[1]->listElems()[n], noPos);
        list->elems[n] = args[1]->listElems()[n];
    }

    auto comparator = [&](Value a, Value b) {
        /* Optimization: if the comparator is lessThan, bypass
           callFunction. */
        /* TODO: (layus) this is absurd. An optimisation like this
           should be outside the lambda creation */
        if (args[0]->isPrimOp()) {
            auto ptr = args[0]->primOp()->fun.target<decltype(&prim_lessThan)>();
            if (ptr && *ptr == prim_lessThan)
                return CompareValues(state, "while evaluating the ordering function passed to builtins.sort")(a, b);
        }

        Value vs[] = {a, b};
        Value vBool;
        state.callFunction(*args[0], vs, vBool, noPos);
        return state.forceBool(vBool, noPos, "while evaluating the return value of the sorting function passed to builtins.sort");
    };

    /* FIXME: std::sort can segfault if the comparator is not a strict
       weak ordering. What to do? std::stable_sort() seems more
       resilient, but no guarantees... */
    std::stable_sort(list->elems, list->elems + len, comparator);
}

static void prim_partition(EvalState & state, Value * * args, Value & v)
{
    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.partition");
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.partition");

    auto len = args[1]->listSize();
    auto elems = args[1]->listElems();

    std::vector<size_t> right, wrong;

    for (size_t n = 0; n < len; ++n) {
        auto & vElem = args[1]->listElems()[n];
        state.forceValue(vElem, noPos);
        Value res;
        state.callFunction(*args[0], vElem, res, noPos);
        if (state.forceBool(res, noPos, "while evaluating the return value of the partition function passed to builtins.partition"))
            right.push_back(n);
        else
            wrong.push_back(n);
    }

    auto attrs = state.ctx.buildBindings(2);

    auto & vRight = attrs.alloc(state.ctx.s.right);
    auto rsize = right.size();
    auto rlist = state.ctx.mem.newList(rsize);
    vRight = {NewValueAs::list, rlist};
    for (auto [i, idx] : enumerate(right)) {
        rlist->elems[i] = elems[idx];
    }

    auto & vWrong = attrs.alloc(state.ctx.s.wrong);
    auto wsize = wrong.size();
    auto wlist = state.ctx.mem.newList(wsize);
    vWrong = {NewValueAs::list, wlist};
    for (auto [i, idx] : enumerate(wrong)) {
        wlist->elems[i] = elems[idx];
    }

    v.mkAttrs(attrs);
}

static void prim_groupBy(EvalState & state, Value * * args, Value & v)
{
    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.groupBy");
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.groupBy");

    std::map<Symbol, std::vector<size_t>> attrs;

    auto elems = args[1]->listElems();

    for (auto [i, vElem] : enumerate(args[1]->listItems())) {
        Value res;
        state.callFunction(*args[0], vElem, res, noPos);
        auto name = state.forceStringNoCtx(res, noPos, "while evaluating the return value of the grouping function passed to builtins.groupBy");
        auto sym = state.ctx.symbols.create(name);
        auto vector = attrs.try_emplace(sym, std::vector<size_t>()).first;
        vector->second.push_back(i);
    }

    auto attrs2 = state.ctx.buildBindings(attrs.size());

    for (auto & i : attrs) {
        auto & list = attrs2.alloc(i.first);
        auto size = i.second.size();
        auto content = state.ctx.mem.newList(size);
        list = {NewValueAs::list, content};
        for (auto [i, idx] : enumerate(i.second)) {
            content->elems[i] = elems[idx];
        }
    }

    v.mkAttrs(attrs2.alreadySorted());
}

static void prim_concatMap(EvalState & state, Value * * args, Value & v)
{
    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.concatMap");
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.concatMap");
    auto nrLists = args[1]->listSize();

    // List of returned lists before concatenation. References to these Values must NOT be persisted.
    SmallTemporaryValueVector<conservativeStackReservation> lists(nrLists);
    size_t len = 0;

    for (size_t n = 0; n < nrLists; ++n) {
        Value & vElem = args[1]->listElems()[n];
        state.callFunction(*args[0], vElem, lists[n], noPos);
        state.forceList(lists[n], noPos, "while evaluating the return value of the function passed to builtins.concatMap");
        len += lists[n].listSize();
    }

    auto result = state.ctx.mem.newList(len);
    v = {NewValueAs::list, result};
    auto out = result->elems;
    for (unsigned int n = 0, pos = 0; n < nrLists; ++n) {
        auto l = lists[n].listSize();
        if (l) {
            std::copy(lists[n].listItems().begin(), lists[n].listItems().end(), out + pos);
        }
        pos += l;
    }
}


/*************************************************************
 * Integer arithmetic
 *************************************************************/


static void prim_add(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    state.forceValue(*args[1], noPos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(state.forceFloat(*args[0], noPos, "while evaluating the first argument of the addition")
                + state.forceFloat(*args[1], noPos, "while evaluating the second argument of the addition"));
    else {
        auto i1 = state.forceInt(*args[0], noPos, "while evaluating the first argument of the addition");
        auto i2 = state.forceInt(*args[1], noPos, "while evaluating the second argument of the addition");

        auto result_ = i1 + i2;
        if (auto result = result_.valueChecked(); result.has_value()) {
            v.mkInt(*result);
        } else {
            state.ctx.errors.make<EvalError>("integer overflow in adding %1% + %2%", i1, i2).debugThrow();
        }
    }
}

static void prim_sub(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    state.forceValue(*args[1], noPos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(state.forceFloat(*args[0], noPos, "while evaluating the first argument of the subtraction")
                - state.forceFloat(*args[1], noPos, "while evaluating the second argument of the subtraction"));
    else {
        auto i1 = state.forceInt(*args[0], noPos, "while evaluating the first argument of the subtraction");
        auto i2 = state.forceInt(*args[1], noPos, "while evaluating the second argument of the subtraction");

        auto result_ = i1 - i2;

        if (auto result = result_.valueChecked(); result.has_value()) {
            v.mkInt(*result);
        } else {
            state.ctx.errors.make<EvalError>("integer overflow in subtracting %1% - %2%", i1, i2).debugThrow();
        }
    }
}

static void prim_mul(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    state.forceValue(*args[1], noPos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(state.forceFloat(*args[0], noPos, "while evaluating the first of the multiplication")
                * state.forceFloat(*args[1], noPos, "while evaluating the second argument of the multiplication"));
    else {
        auto i1 = state.forceInt(*args[0], noPos, "while evaluating the first argument of the multiplication");
        auto i2 = state.forceInt(*args[1], noPos, "while evaluating the second argument of the multiplication");

        auto result_ = i1 * i2;

        if (auto result = result_.valueChecked(); result.has_value()) {
            v.mkInt(*result);
        } else {
            state.ctx.errors.make<EvalError>("integer overflow in multiplying %1% * %2%", i1, i2).debugThrow();
        }
    }
}

static void prim_div(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    state.forceValue(*args[1], noPos);

    NixFloat f2 = state.forceFloat(*args[1], noPos, "while evaluating the second operand of the division");
    if (f2 == 0)
        state.ctx.errors.make<EvalError>("division by zero").debugThrow();

    if (args[0]->type() == nFloat || args[1]->type() == nFloat) {
        v.mkFloat(state.forceFloat(*args[0], noPos, "while evaluating the first operand of the division") / f2);
    } else {
        NixInt i1 = state.forceInt(*args[0], noPos, "while evaluating the first operand of the division");
        NixInt i2 = state.forceInt(*args[1], noPos, "while evaluating the second operand of the division");
        /* Avoid division overflow as it might raise SIGFPE. */
        auto result_ = i1 / i2;
        if (auto result = result_.valueChecked(); result.has_value()) {
            v.mkInt(*result);
        } else {
            state.ctx.errors.make<EvalError>("integer overflow in dividing %1% / %2%", i1, i2).debugThrow();
        }
    }
}

static void prim_bitAnd(EvalState & state, Value * * args, Value & v)
{
    auto i1 = state.forceInt(*args[0], noPos, "while evaluating the first argument passed to builtins.bitAnd");
    auto i2 = state.forceInt(*args[1], noPos, "while evaluating the second argument passed to builtins.bitAnd");
    v.mkInt(i1.value & i2.value);
}

static void prim_bitOr(EvalState & state, Value * * args, Value & v)
{
    auto i1 = state.forceInt(*args[0], noPos, "while evaluating the first argument passed to builtins.bitOr");
    auto i2 = state.forceInt(*args[1], noPos, "while evaluating the second argument passed to builtins.bitOr");

    v.mkInt(i1.value | i2.value);
}

static void prim_bitXor(EvalState & state, Value * * args, Value & v)
{
    auto i1 = state.forceInt(*args[0], noPos, "while evaluating the first argument passed to builtins.bitXor");
    auto i2 = state.forceInt(*args[1], noPos, "while evaluating the second argument passed to builtins.bitXor");

    v.mkInt(i1.value ^ i2.value);
}

static void prim_lessThan(EvalState & state, Value * * args, Value & v)
{
    state.forceValue(*args[0], noPos);
    state.forceValue(*args[1], noPos);
    CompareValues comp(state, "");
    v.mkBool(comp(*args[0], *args[1]));
}


/*************************************************************
 * String manipulation
 *************************************************************/


/* Convert the argument to a string.  Paths are *not* copied to the
   store, so `toString /foo/bar' yields `"/foo/bar"', not
   `"/nix/store/whatever..."'. */
static void prim_toString(EvalState & state, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(noPos, *args[0], context,
            "while evaluating the first argument passed to builtins.toString",
            StringCoercionMode::ToString, false);
    v.mkString(*s, context);
}

/* `substring start len str' returns the substring of `str' starting
   at character position `min(start, stringLength str)' inclusive and
   ending at `min(start + len, stringLength str)'.  `start' must be
   non-negative. */
static void prim_substring(EvalState & state, Value * * args, Value & v)
{
    using NixUInt = std::make_unsigned_t<NixInt::Inner>;
    NixInt::Inner start = state.forceInt(*args[0], noPos, "while evaluating the first argument (the start offset) passed to builtins.substring").value;

    if (start < 0)
        state.ctx.errors.make<EvalError>("negative start position in 'substring'").debugThrow();

    NixInt::Inner len_arg = state
                                .forceInt(
                                    *args[1],
                                    noPos,
                                    "while evaluating the second argument (the substring length) "
                                    "passed to builtins.substring"
                                )
                                .value;

    // Special-case on empty substring to avoid O(n) strlen
    // This allows for the use of empty substrings to efficiently capture string context
    if (len_arg == 0) {
        state.forceValue(*args[2], noPos);
        if (args[2]->type() == nString) {
            v.mkString("", args[2]->string().context);
            return;
        }
    }

    NixStringContext context;
    auto s = state.coerceToString(noPos, *args[2], context, "while evaluating the third argument (the string) passed to builtins.substring");

    // Negative length may be idiomatically passed to builtins.substring to get
    // the tail of the string.
    // Otherwise, clamp it to the size of the string or the length argument if it's smaller.
    // This is notably useful on 32 bits platforms where max(size_t) (32 bits) < max(NixUInt) (64
    // bits), because then the `len` argument fits a `size_t`.
    static_assert(
        sizeof(size_t) <= sizeof(NixUInt),
        "std::size_t's size must be smaller or equal to Nix's unsigned int type's size (NixUInt)"
    );
    auto len = len_arg >= 0 ? std::min(static_cast<NixUInt>(s->size()), NixUInt(len_arg))
                            : std::numeric_limits<std::string::size_type>::max();

    v.mkString(NixUInt(start) >= s->size() ? "" : s->substr(start, len), context);
}

static void prim_stringLength(EvalState & state, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(noPos, *args[0], context, "while evaluating the argument passed to builtins.stringLength");
    v.mkInt(NixInt::Inner(s->size()));
}

/* Return the cryptographic hash of a string in base-16. */
static void prim_hashString(EvalState & state, Value * * args, Value & v)
{
    auto type = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.hashString");
    std::optional<HashType> ht = parseHashType(type);
    if (!ht)
        state.ctx.errors.make<EvalError>("unknown hash algorithm '%1%'", type).debugThrow();

    NixStringContext context; // discarded
    auto s = state.forceString(*args[1], context, noPos, "while evaluating the second argument passed to builtins.hashString");

    v.mkString(hashString(*ht, s).to_string(Base::Base16, false));
}

struct RegexCache
{
    // TODO use C++20 transparent comparison when available
    std::unordered_map<std::string_view, std::regex> cache;
    std::list<std::string> keys;

    std::regex get(std::string_view re)
    {
        auto it = cache.find(re);
        if (it != cache.end())
            return it->second;
        keys.emplace_back(re);
        return cache.emplace(keys.back(), regex::parse(keys.back(), std::regex::extended)).first->second;
    }
};

static RegexCache & regexCacheOf(EvalState & state)
{
    if (!state.ctx.caches.regexes) {
        state.ctx.caches.regexes = std::make_shared<RegexCache>();
    }
    return *state.ctx.caches.regexes;
}

void prim_match(EvalState & state, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.match");

    try {

        auto regex = regexCacheOf(state).get(re);

        NixStringContext context;
        const auto str = state.forceString(*args[1], context, noPos, "while evaluating the second argument passed to builtins.match");

        std::cmatch match;
        if (!std::regex_match(str.begin(), str.end(), match, regex)) {
            v.mkNull();
            return;
        }

        // the first match is the whole string
        const size_t len = match.size() - 1;
        auto result = state.ctx.mem.newList(len);
        v = {NewValueAs::list, result};
        for (size_t i = 0; i < len; ++i) {
            if (!match[i+1].matched)
                result->elems[i].mkNull();
            else
                result->elems[i].mkString(match[i + 1].str());
        }

    } catch (regex::Error & e) {
        state.ctx.errors.make<EvalError>(e.info()).debugThrow();
    }
}

/* Split a string with a regular expression, and return a list of the
   non-matching parts interleaved by the lists of the matching groups. */
void prim_split(EvalState & state, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.split");

    try {

        auto regex = regexCacheOf(state).get(re);

        NixStringContext context;
        const auto str = state.forceString(*args[1], context, noPos, "while evaluating the second argument passed to builtins.split");

        auto begin = std::cregex_iterator(str.begin(), str.end(), regex);
        auto end = std::cregex_iterator();

        // Any matches results are surrounded by non-matching results.
        const size_t len = std::distance(begin, end);
        auto result = state.ctx.mem.newList(2 * len + 1);
        v = {NewValueAs::list, result};
        size_t idx = 0;

        if (len == 0) {
            result->elems[idx++] = *args[1];
            return;
        }

        for (auto i = begin; i != end; ++i) {
            assert(idx <= 2 * len + 1 - 3);
            auto match = *i;

            // Add a string for non-matched characters.
            result->elems[idx++].mkString(match.prefix().str());

            // Add a list for matched substrings.
            const size_t slen = match.size() - 1;
            auto & elem = result->elems[idx++];

            // Start at 1, beacause the first match is the whole string.
            auto content = state.ctx.mem.newList(slen);
            elem = {NewValueAs::list, content};
            for (size_t si = 0; si < slen; ++si) {
                if (!match[si + 1].matched)
                    content->elems[si].mkNull();
                else
                    content->elems[si].mkString(match[si + 1].str());
            }

            // Add a string for non-matched suffix characters.
            if (idx == 2 * len) {
                result->elems[idx++].mkString(match.suffix().str());
            }
        }

        assert(idx == 2 * len + 1);

    } catch (regex::Error & e) {
        state.ctx.errors.make<EvalError>(e.info()).debugThrow();
    }
}

static void prim_concatStringsSep(EvalState & state, Value * * args, Value & v)
{
    NixStringContext context;

    auto sep = state.forceString(*args[0], context, noPos, "while evaluating the first argument (the separator string) passed to builtins.concatStringsSep");
    state.forceList(*args[1], noPos, "while evaluating the second argument (the list of strings to concat) passed to builtins.concatStringsSep");

    std::string res;
    res.reserve((args[1]->listSize() + 32) * sep.size());
    bool first = true;

    for (auto & elem : args[1]->listItems()) {
        if (first) first = false; else res += sep;
        res += *state.coerceToString(
            noPos,
            elem,
            context,
            "while evaluating one element of the list of strings to concat passed to "
            "builtins.concatStringsSep"
        );
    }

    v.mkString(res, context);
}

static void prim_replaceStrings(EvalState & state, Value * * args, Value & v)
{
    state.forceList(*args[0], noPos, "while evaluating the first argument passed to builtins.replaceStrings");
    state.forceList(*args[1], noPos, "while evaluating the second argument passed to builtins.replaceStrings");
    if (args[0]->listSize() != args[1]->listSize())
        state.ctx.errors.make<EvalError>(
            "'from' and 'to' arguments passed to builtins.replaceStrings have different lengths"
        ).debugThrow();

    std::vector<std::string> from;
    from.reserve(args[0]->listSize());
    for (auto & elem : args[0]->listItems()) {
        from.emplace_back(state.forceString(
            elem,
            noPos,
            "while evaluating one of the strings to replace passed to builtins.replaceStrings"
        ));
    }

    std::unordered_map<size_t, std::string> cache;
    auto to = args[1]->listItems();

    NixStringContext context;
    auto s = state.forceString(*args[2], context, noPos, "while evaluating the third argument passed to builtins.replaceStrings");

    std::string res;
    // Loops one past last character to handle the case where 'from' contains an empty string.
    for (size_t p = 0; p <= s.size(); ) {
        bool found = false;
        auto i = from.begin();
        auto j = to.begin();
        size_t j_index = 0;
        for (; i != from.end(); ++i, ++j, ++j_index)
            if (s.compare(p, i->size(), *i) == 0) {
                found = true;
                auto v = cache.find(j_index);
                if (v == cache.end()) {
                    NixStringContext ctx;
                    auto ts = state.forceString(
                        *j,
                        ctx,
                        noPos,
                        "while evaluating one of the replacement strings passed to "
                        "builtins.replaceStrings"
                    );
                    v = (cache.emplace(j_index, ts)).first;
                    for (auto & path : ctx) {
                        context.insert(path);
                    }
                }
                res += v->second;
                if (i->empty()) {
                    if (p < s.size())
                        res += s[p];
                    p++;
                } else {
                    p += i->size();
                }
                break;
            }
        if (!found) {
            if (p < s.size())
                res += s[p];
            p++;
        }
    }

    v.mkString(res, context);
}


/*************************************************************
 * Versions
 *************************************************************/


static void prim_parseDrvName(EvalState & state, Value * * args, Value & v)
{
    auto name = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.parseDrvName");
    DrvName parsed(name);
    auto attrs = state.ctx.buildBindings(2);
    attrs.alloc(state.ctx.s.name).mkString(parsed.name);
    attrs.alloc("version").mkString(parsed.version);
    v.mkAttrs(attrs);
}

static void prim_compareVersions(EvalState & state, Value * * args, Value & v)
{
    auto version1 = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.compareVersions");
    auto version2 = state.forceStringNoCtx(*args[1], noPos, "while evaluating the second argument passed to builtins.compareVersions");
    auto result = compareVersions(version1, version2);
    v.mkInt(result < 0 ? -1 : result > 0 ? 1 : 0);
}

static void prim_splitVersion(EvalState & state, Value * * args, Value & v)
{
    auto version = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.splitVersion");
    auto iter = version.cbegin();
    Strings components;
    while (iter != version.cend()) {
        auto component = nextComponent(iter, version.cend());
        if (component.empty())
            break;
        components.emplace_back(component);
    }
    auto result = state.ctx.mem.newList(components.size());
    v = {NewValueAs::list, result};
    for (const auto & [n, component] : enumerate(components))
        result->elems[n].mkString(std::move(component));
}


/*************************************************************
 * Primop registration
 *************************************************************/


RegisterPrimOp::PrimOps * RegisterPrimOp::primOps;

RegisterPrimOp::RegisterPrimOp(PrimOpDetails && primOp)
{
    if (!primOps) primOps = new PrimOps;
    primOps->emplace_back(std::move(primOp));
}


Value EvalBuiltins::prepareNixPath(const SearchPath & searchPath)
{
    auto v = mem.newList(searchPath.elements.size());
    int n = 0;
    for (auto & i : searchPath.elements) {
        auto attrs = mem.buildBindings(symbols, 2);
        attrs.alloc("path").mkString(i.path.s);
        attrs.alloc("prefix").mkString(i.prefix.s);
        v->elems[n++].mkAttrs(attrs);
    }
    return {NewValueAs::list, v};
}

void EvalBuiltins::createBaseEnv(const SearchPath & searchPath, const Path & storeDir)
{
    env.up = 0;

    // constants include the magic `builtins` which must come first
    #include "register-builtin-constants.gen.inc"
    #include "register-builtins.gen.inc"

    // Miscellaneous
    if (evalSettings.enableNativeCode) {
        addPrimOp({
            .name = "__importNative",
            .arity = 2,
            .fun = prim_importNative,
        });
        addPrimOp({
            .name = "__exec",
            .arity = 1,
            .fun = prim_exec,
        });
    }

    if (RegisterPrimOp::primOps)
        for (auto & primOp : *RegisterPrimOp::primOps)
            if (experimentalFeatureSettings.isEnabled(primOp.experimentalFeature))
            {
                auto primOpAdjusted = primOp;
                primOpAdjusted.arity = std::max(primOp.args.size(), primOp.arity);
                addPrimOp(std::move(primOpAdjusted));
            }

    static PrimOp prim_initializeDerivation{{
        .arity = 1,
        .fun =
            [](EvalState & state, Value ** args, Value & v) {
                char code[] =
#include "primops/derivation.nix.gen.hh"
                    ;
                auto & expr = *state.ctx.parse(
                    code, sizeof(code), Pos::Hidden{}, {CanonPath::root}, state.ctx.builtins.staticEnv
                );
                state.eval(expr, v);
            },
    }};
    static Value initializeDerivation{NewValueAs::primop, prim_initializeDerivation};

    /* Add a wrapper around the derivation primop that computes the
       `drvPath' and `outPath' attributes lazily.

       Null docs because it is documented separately.
       App instead of PrimopApp to have eval immediately force it when accessed.
       */
    addConstant(
        "derivation",
        {NewValueAs::app, mem, initializeDerivation, initializeDerivation},
        {.type = nFunction}
    );

    /* Now that we've added all primops, sort the `builtins' set,
       because attribute lookups expect it to be sorted. */
    env.values[0].attrs()->sort();

    staticEnv->isRoot = true;
}


}
