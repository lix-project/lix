#include "lix/libexpr/primops.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libexpr/extra-primops.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/store-api.hh"

namespace nix {

static void prim_unsafeDiscardStringContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.unsafeDiscardStringContext");
    v.mkString(*s);
}

static RegisterPrimOp primop_unsafeDiscardStringContext({
    .name = "__unsafeDiscardStringContext",
    .arity = 1,
    .fun = prim_unsafeDiscardStringContext
});


void prim_hasContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    state.forceString(*args[0], context, pos, "while evaluating the argument passed to builtins.hasContext");
    v.mkBool(!context.empty());
}


void prim_unsafeDiscardOutputDependency(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.unsafeDiscardOutputDependency");

    NixStringContext context2;
    for (auto && c : context) {
        if (auto * ptr = std::get_if<NixStringContextElem::DrvDeep>(&c.raw)) {
            context2.emplace(NixStringContextElem::Opaque {
                .path = ptr->drvPath
            });
        } else {
            /* Can reuse original item */
            context2.emplace(std::move(c).raw);
        }
    }

    v.mkString(*s, context2);
}


void prim_addDrvOutputDependencies(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.addDrvOutputDependencies");

	auto contextSize = context.size();
    if (contextSize != 1) {
        state.errors.make<EvalError>(
            "context of string '%s' must have exactly one element, but has %d",
            *s,
            contextSize
        ).atPos(pos).debugThrow();
    }
    NixStringContext context2 {
        (NixStringContextElem { std::visit(overloaded {
            [&](const NixStringContextElem::Opaque & c) -> NixStringContextElem::DrvDeep {
                if (!c.path.isDerivation()) {
                    state.errors.make<EvalError>(
                        "path '%s' is not a derivation",
                        state.store->printStorePath(c.path)
                    ).atPos(pos).debugThrow();
                }
                return NixStringContextElem::DrvDeep {
                    .drvPath = c.path,
                };
            },
            [&](const NixStringContextElem::Built & c) -> NixStringContextElem::DrvDeep {
                state.errors.make<EvalError>(
                    "`addDrvOutputDependencies` can only act on derivations, not on a derivation output such as '%1%'",
                    c.output
                ).atPos(pos).debugThrow();
            },
            [&](const NixStringContextElem::DrvDeep & c) -> NixStringContextElem::DrvDeep {
                /* Reuse original item because we want this to be idempotent. */
                return std::move(c);
            },
        }, context.begin()->raw) }),
    };

    v.mkString(*s, context2);
}


/* Extract the context of a string as a structured Nix value.

   The context is represented as an attribute set whose keys are the
   paths in the context set and whose values are attribute sets with
   the following keys:
     path: True if the relevant path is in the context as a plain store
           path (i.e. the kind of context you get when interpolating
           a Nix path (e.g. ./.) into a string). False if missing.
     allOutputs: True if the relevant path is a derivation and it is
                  in the context as a drv file with all of its outputs
                  (i.e. the kind of context you get when referencing
                  .drvPath of some derivation). False if missing.
     outputs: If a non-empty list, the relevant path is a derivation
              and the provided outputs are referenced in the context
              (i.e. the kind of context you get when referencing
              .outPath of some derivation). Empty list if missing.
   Note that for a given path any combination of the above attributes
   may be present.
*/
void prim_getContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    struct ContextInfo {
        bool path = false;
        bool allOutputs = false;
        Strings outputs;
    };
    NixStringContext context;
    state.forceString(*args[0], context, pos, "while evaluating the argument passed to builtins.getContext");
    auto contextInfos = std::map<StorePath, ContextInfo>();
    for (auto && i : context) {
        std::visit(overloaded {
            [&](NixStringContextElem::DrvDeep && d) {
                contextInfos[std::move(d.drvPath)].allOutputs = true;
            },
            [&](NixStringContextElem::Built && b) {
                // FIXME should eventually show string context as is, no
                // resolving here.
                auto drvPath = resolveDerivedPath(*state.store, *b.drvPath);
                contextInfos[std::move(drvPath)].outputs.emplace_back(std::move(b.output));
            },
            [&](NixStringContextElem::Opaque && o) {
                contextInfos[std::move(o.path)].path = true;
            },
        }, ((NixStringContextElem &&) i).raw);
    }

    auto attrs = state.buildBindings(contextInfos.size());

    auto sAllOutputs = state.ctx.symbols.create("allOutputs");
    for (const auto & info : contextInfos) {
        auto infoAttrs = state.buildBindings(3);
        if (info.second.path)
            infoAttrs.alloc(state.ctx.s.path).mkBool(true);
        if (info.second.allOutputs)
            infoAttrs.alloc(sAllOutputs).mkBool(true);
        if (!info.second.outputs.empty()) {
            auto & outputsVal = infoAttrs.alloc(state.ctx.s.outputs);
            outputsVal = state.mem.newList(info.second.outputs.size());
            for (const auto & [i, output] : enumerate(info.second.outputs))
                (outputsVal.listElems()[i] = state.mem.allocValue())->mkString(output);
        }
        attrs.alloc(state.store->printStorePath(info.first)).mkAttrs(infoAttrs);
    }

    v.mkAttrs(attrs);
}


/* Append the given context to a given string.

   See the commentary above unsafeGetContext for details of the
   context representation.
*/
static void prim_appendContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto orig = state.forceString(*args[0], context, noPos, "while evaluating the first argument passed to builtins.appendContext");

    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.appendContext");

    auto sAllOutputs = state.ctx.symbols.create("allOutputs");
    for (auto & i : *args[1]->attrs) {
        const auto & name = state.ctx.symbols[i.name];
        if (!state.store->isStorePath(name))
            state.errors.make<EvalError>(
                "context key '%s' is not a store path",
                name
            ).atPos(i.pos).debugThrow();
        auto namePath = state.store->parseStorePath(name);
        if (!settings.readOnlyMode)
            state.store->ensurePath(namePath);
        state.forceAttrs(*i.value, i.pos, "while evaluating the value of a string context");
        auto iter = i.value->attrs->find(state.ctx.s.path);
        if (iter != i.value->attrs->end()) {
            if (state.forceBool(*iter->value, iter->pos, "while evaluating the `path` attribute of a string context"))
                context.emplace(NixStringContextElem::Opaque {
                    .path = namePath,
                });
        }

        iter = i.value->attrs->find(sAllOutputs);
        if (iter != i.value->attrs->end()) {
            if (state.forceBool(*iter->value, iter->pos, "while evaluating the `allOutputs` attribute of a string context")) {
                if (!isDerivation(name)) {
                    state.errors.make<EvalError>(
                        "tried to add all-outputs context of %s, which is not a derivation, to a string",
                        name
                    ).atPos(i.pos).debugThrow();
                }
                context.emplace(NixStringContextElem::DrvDeep {
                    .drvPath = namePath,
                });
            }
        }

        iter = i.value->attrs->find(state.ctx.s.outputs);
        if (iter != i.value->attrs->end()) {
            state.forceList(*iter->value, iter->pos, "while evaluating the `outputs` attribute of a string context");
            if (iter->value->listSize() && !isDerivation(name)) {
                state.errors.make<EvalError>(
                    "tried to add derivation output context of %s, which is not a derivation, to a string",
                    name
                ).atPos(i.pos).debugThrow();
            }
            for (auto elem : iter->value->listItems()) {
                auto outputName = state.forceStringNoCtx(*elem, iter->pos, "while evaluating an output name within a string context");
                context.emplace(NixStringContextElem::Built {
                    .drvPath = makeConstantStorePathRef(namePath),
                    .output = std::string { outputName },
                });
            }
        }
    }

    v.mkString(orig, context);
}

static RegisterPrimOp primop_appendContext({
    .name = "__appendContext",
    .arity = 2,
    .fun = prim_appendContext
});

}
