#include "lix/libexpr/value-to-json.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/store-api.hh"

#include <cstdlib>
#include <iomanip>
#include <nlohmann/json.hpp>


namespace nix {
using json = nlohmann::json;
json printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore)
{
    checkInterrupt();

    if (strict) state.forceValue(v, pos);

    json out;

    switch (v.type()) {

        case nInt:
            out = v.integer.value;
            break;

        case nBool:
            out = v.boolean;
            break;

        case nString:
            copyContext(v, context);
            out = v.string.s;
            break;

        case nPath:
            if (copyToStore)
                out = state.store->printStorePath(
                    state.paths.copyPathToStore(context, v.path(), state.repair));
            else
                out = v.path().path.abs();
            break;

        case nNull:
            // already initialized as null
            break;

        case nAttrs: {
            auto maybeString = state.tryAttrsToString(pos, v, context, false, false);
            if (maybeString) {
                out = *maybeString;
                break;
            }
            auto i = v.attrs->find(state.ctx.s.outPath);
            if (i == v.attrs->end()) {
                out = json::object();
                StringSet names;
                for (auto & j : *v.attrs)
                    names.emplace(state.ctx.symbols[j.name]);
                for (auto & j : names) {
                    Attr & a(*v.attrs->find(state.ctx.symbols.create(j)));
                    try {
                        out[j] = printValueAsJSON(state, strict, *a.value, a.pos, context, copyToStore);
                    } catch (Error & e) {
                        e.addTrace(state.ctx.positions[a.pos],
                            HintFmt("while evaluating attribute '%1%'", j));
                        throw;
                    }
                }
            } else
                return printValueAsJSON(state, strict, *i->value, i->pos, context, copyToStore);
            break;
        }

        case nList: {
            out = json::array();
            int i = 0;
            for (auto elem : v.listItems()) {
                try {
                    out.push_back(printValueAsJSON(state, strict, *elem, pos, context, copyToStore));
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
            return v.external->printValueAsJSON(state, strict, context, copyToStore);
            break;

        case nFloat:
            out = v.fpoint;
            break;

        case nThunk:
        case nFunction:
            state.ctx.errors.make<TypeError>(
                "cannot convert %1% to JSON",
                showType(v)
            )
            .atPos(v.determinePos(pos))
            .debugThrow();
    }
    return out;
}

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, std::ostream & str, NixStringContext & context, bool copyToStore)
{
    str << printValueAsJSON(state, strict, v, pos, context, copyToStore);
}

json ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
    NixStringContext & context, bool copyToStore) const
{
    state.ctx.errors.make<TypeError>("cannot convert %1% to JSON", showType())
    .debugThrow();
}


}
