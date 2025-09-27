#include "lix/libexpr/value-to-json.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/store-api.hh"

#include <cstdlib>


namespace nix {
JSON printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore)
{
    checkInterrupt();

    if (strict) state.forceValue(v, pos);

    JSON out;

    switch (v.type()) {

        case nInt:
            out = v.integer().value;
            break;

        case nBool:
            out = v.boolean();
            break;

        case nString:
            copyContext(v, context);
            out = v.str();
            break;

        case nPath:
            if (copyToStore)
                out = state.ctx.store->printStorePath(state.aio.blockOn(
                    state.ctx.paths.copyPathToStore(context, v.path(), state.ctx.repair)
                ).unwrap());
            else {
                out = v.path().to_string();
            }
            break;

        case nNull:
            // already initialized as null
            break;

        case nAttrs: {
            auto maybeString = state.tryAttrsToString(pos, v, context, StringCoercionMode::Strict, false);
            if (maybeString) {
                out = *maybeString;
                break;
            }
            auto i = v.attrs()->get(state.ctx.s.outPath);
            if (!i) {
                out = JSON::object();
                StringSet names;
                for (auto & j : *v.attrs())
                    names.emplace(state.ctx.symbols[j.name]);
                for (auto & j : names) {
                    const Attr & a(*v.attrs()->get(state.ctx.symbols.create(j)));
                    try {
                        out[j] =
                            printValueAsJSON(state, strict, a.value, a.pos, context, copyToStore);
                    } catch (Error & e) {
                        e.addTrace(
                            state.ctx.positions[a.pos],
                            HintFmt("while evaluating attribute '%1%'", j)
                        );
                        throw;
                    }
                }
            } else {
                return printValueAsJSON(state, strict, i->value, i->pos, context, copyToStore);
            }
            break;
        }

        case nList: {
            out = JSON::array();
            int i = 0;
            for (auto elem : v.listItems()) {
                try {
                    out.push_back(printValueAsJSON(state, strict, elem, pos, context, copyToStore));
                } catch (Error & e) {
                    e.addTrace(state.ctx.positions[pos],
                        HintFmt("while evaluating list element at index %1%", i));
                    throw;
                }
                i++;
            }
            break;
        }

        case nExternal:
            return v.external()->printValueAsJSON(state, strict, context, copyToStore);
            break;

        case nFloat:
            out = v.fpoint();
            break;

        case nThunk:
        case nFunction:
            state.ctx.errors.make<TypeError>(
                "cannot convert %1% to JSON",
                showType(v)
            )
            .atPos(pos)
            .debugThrow();
    }
    return out;
}

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, std::ostream & str, NixStringContext & context, bool copyToStore)
{
    str << printValueAsJSON(state, strict, v, pos, context, copyToStore);
}

JSON ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
    NixStringContext & context, bool copyToStore) const
{
    state.ctx.errors.make<TypeError>("cannot convert %1% to JSON", showType())
    .debugThrow();
}


}
