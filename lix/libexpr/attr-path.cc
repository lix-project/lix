#include "lix/libexpr/attr-path.hh"
#include "lix/libutil/strings.hh"
#include "print-options.hh"
#include <algorithm>
#include <sstream>


namespace nix {


std::vector<std::string> parseAttrPath(std::string_view const s)
{
    std::vector<std::string> res;
    std::string cur;
    bool haveData = false;
    auto i = s.begin();
    while (i != s.end()) {
        if (*i == '.') {
            res.push_back(cur);
            haveData = false;
            cur.clear();
        } else if (*i == '"') {
            // If there is a quote there *will* be a named term even if it is empty.
            ++i;
            haveData = true;

            while (1) {
                if (i == s.end())
                    throw ParseError("missing closing quote in selection path '%1%'", s);
                if (*i == '"') break;
                cur.push_back(*i++);
            }
        } else {
            cur.push_back(*i);
            haveData = true;
        }
        ++i;
    }
    if (haveData) res.push_back(cur);
    return res;
}


std::string unparseAttrPath(std::vector<std::string> const & attrPath)
{
    // FIXME(jade): can probably be rewritten with ranges once libc++ has a
    // fully featured implementation
    // https://github.com/llvm/llvm-project/pull/65536
    auto ret = std::ostringstream{};
    bool first = true;

    for (auto const & part : attrPath) {
        if (!first) {
            ret << ".";
        }
        first = false;

        bool mustQuote = std::ranges::any_of(part, [](char c) -> bool {
            return c == '"' || c == '.' || c == ' ';
        });

        if (mustQuote || part.empty()) {
            ret << '"' << part << '"';
        } else {
            ret << part;
        }
    }

    return ret.str();
}

std::pair<Value, PosIdx>
findAlongAttrPath(EvalState & state, const std::string & attrPath, Bindings & autoArgs, Value & vIn)
{
    auto tokens = parseAttrPath(attrPath);

    Value v = vIn;
    PosIdx pos = noPos;

    for (auto [attrPathIdx, attr] : enumerate(tokens)) {

        /* Is i an index (integer) or a normal attribute name? */
        auto attrIndex = string2Int<unsigned int>(attr);

        /* Evaluate the expression. */
        Value vNew;
        state.autoCallFunction(autoArgs, v, vNew, pos);
        v = vNew;
        state.forceValue(v, noPos);

        /* It should evaluate to either a set or an expression,
           according to what is specified in the attrPath. */

        if (!attrIndex) {
            if (attr.empty())
                throw Error("empty attribute name in selection path '%1%'", attrPath);

            if (v.type() != nAttrs) {
                auto pathPart =
                    std::vector<std::string>(tokens.begin(), tokens.begin() + attrPathIdx);
                state.ctx.errors
                    .make<TypeError>(
                        "the value being indexed in the selection path '%1%' at '%2%' should be a "
                        "set but is %3%: %4%",
                        attrPath,
                        unparseAttrPath(pathPart),
                        showType(v),
                        ValuePrinter(state, v, errorPrintOptions)
                    )
                    .debugThrow();
            }

            auto a = v.attrs()->get(state.ctx.symbols.create(attr));
            if (!a) {
                std::set<std::string> attrNames;
                for (auto & attr : *v.attrs()) {
                    attrNames.emplace(state.ctx.symbols[attr.name]);
                }

                auto suggestions = Suggestions::bestMatches(attrNames, attr);
                auto pathPart =
                    std::vector<std::string>(tokens.begin(), tokens.begin() + attrPathIdx);
                throw AttrPathNotFound(
                    suggestions,
                    "attribute '%1%' in selection path '%2%' not found inside path '%3%', whose "
                    "contents are: %4%",
                    attr,
                    attrPath,
                    unparseAttrPath(pathPart),
                    ValuePrinter(state, v, errorPrintOptions)
                );
            }
            v = a->value;
            pos = a->pos;
        } else {
            if (!v.isList()) {
                state.ctx.errors
                    .make<TypeError>(
                        "the expression selected by the selection path '%1%' should be a list but "
                        "is %2%: %3%",
                        attrPath,
                        showType(v),
                        ValuePrinter(state, v, errorPrintOptions)
                    )
                    .debugThrow();
            }
            if (*attrIndex >= v.listSize()) {
                throw AttrPathNotFound(
                    "list index %1% in selection path '%2%' is out of range for list %3%",
                    *attrIndex,
                    attrPath,
                    ValuePrinter(state, v, errorPrintOptions)
                );
            }

            v = v.listElems()[*attrIndex];
            pos = noPos;
        }

    }

    return {v, pos};
}


std::pair<SourcePath, uint32_t> findPackageFilename(EvalState & state, Value & v, std::string what)
{
    Value v2;
    try {
        auto dummyArgs = state.ctx.mem.allocBindings(0);
        v2 = findAlongAttrPath(state, "meta.position", *dummyArgs, v).first;
    } catch (Error &) {
        throw NoPositionInfo("package '%s' has no source location information", what);
    }

    // FIXME: is it possible to extract the Pos object instead of doing this
    //        toString + parsing?
    NixStringContext context;
    auto path = state.coerceToPath(
        noPos, v2, context, "while evaluating the 'meta.position' attribute of a derivation"
    );

    auto fn = path.canonical().abs();

    auto fail = [fn]() {
        throw ParseError("cannot parse 'meta.position' attribute '%s'", fn);
    };

    auto colon = fn.rfind(':');
    if (colon == std::string::npos) fail();
    // parsing as int32 instead of the uint32 we return for historical reasons.
    // previously this was a stoi(), and we don't know what editors would do if
    // we gave them line numbers that wouldn't fit into the int32 number space.
    auto lineno = string2Int<int32_t>(std::string(fn, colon + 1, std::string::npos));
    if (!lineno) fail();
    return {CanonPath(fn.substr(0, colon)), *lineno};
}


}
