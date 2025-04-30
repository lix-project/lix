#pragma once
///@file

#include "lix/libexpr/nixexpr.hh"
#include "lix/libexpr/pos-idx.hh"
#include "lix/libutil/logging.hh"

namespace nix::parser {

struct IndStringLine {
    // String containing only the leading whitespace of the line. May be empty.
    std::string_view indentation;
    // Position of the line start (before the indentation)
    PosIdx pos;

    // Whether the line contains anything besides indentation and line break
    bool hasContent = false;

    std::vector<
        std::pair<
            PosIdx,
            std::variant<std::unique_ptr<Expr>, std::string_view>
        >
    > parts = {};
};

struct State
{
    SymbolTable & symbols;
    PosTable & positions;
    SourcePath basePath;
    PosTable::Origin origin;
    const Expr::AstSymbols & s;
    const FeatureSettings & featureSettings;
    bool hasWarnedAboutBadLineEndings = false; // State to only warn on first occurrence

    void dupAttr(const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos);
    void dupAttr(Symbol attr, const PosIdx pos, const PosIdx prevPos);
    void overridesFound(const PosIdx pos);
    void badLineEndingFound(const PosIdx pos, bool warnOnly);
    void nulFound(const PosIdx pos);
    void addAttr(ExprAttrs * attrs, AttrPath && attrPath, std::unique_ptr<Expr> e, const PosIdx pos);
    void validateLambdaAttrs(AttrsPattern & pattern, PosIdx pos = noPos);
    std::unique_ptr<Expr> stripIndentation(const PosIdx pos, std::vector<IndStringLine> && line);

    /* Creates an ExprVar or an ExprVarRoot depending on the feature settings.
     * The symbol is synthetic, but for the purpose of error handling the pos is required
     * and should point to the expression where the var is used
     */
    inline std::unique_ptr<ExprVar> mkInternalVar(PosIdx pos, Symbol name);

    // lazy positioning means we don't get byte offsets directly, in.position() would work
    // but also requires line and column (which is expensive)
    PosIdx at(const auto & in)
    {
        return positions.add(origin, in.begin() - in.input().begin());
    }

    PosIdx atEnd(const auto & in)
    {
        return positions.add(origin, in.end() - in.input().begin());
    }
};

std::unique_ptr<ExprVar> State::mkInternalVar(PosIdx pos, Symbol name) {
    return std::make_unique<ExprVar>(pos, name, !featureSettings.isEnabled(Dep::ShadowInternalSymbols));
}

inline void State::dupAttr(const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError({
         .msg = HintFmt("attribute '%1%' already defined at %2%",
             showAttrPath(symbols, attrPath), positions[prevPos]),
         .pos = positions[pos]
    });
}

inline void State::dupAttr(Symbol attr, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError({
        .msg = HintFmt("attribute '%1%' already defined at %2%", symbols[attr], positions[prevPos]),
        .pos = positions[pos]
    });
}

inline void State::overridesFound(const PosIdx pos) {
    // Added 2024-09-18. Turn into an error at some point in the future.
    // See the documentation on deprecated features for more details.
    logWarning({
        .msg = HintFmt(
            "%s attributes are deprecated and will be removed in the future. Use %s to silence this warning.",
            "__overrides",
             "--extra-deprecated-features rec-set-overrides"
            ),
        .pos = positions[pos],
    });
}

// Added 2025-02-05. This is unlikely to ever occur in the wild, given how broken it is
inline void State::badLineEndingFound(const PosIdx pos, bool warnOnly)
{
    ErrorInfo ei = {
        .msg = HintFmt(
            "CR (`\\r`) and CRLF (`\\r\\n`) line endings are not supported. Please inspect the file and normalize it to use LF (`\\n`) line endings instead. Use %s to silence this warning.",
            "--extra-deprecated-features cr-line-endings"
        ),
        .pos = positions[pos],
    };
    // Within strings we should throw because it is a correctness issue, outside of
    // strings it only harmlessly fucks up line numbers in error messages so warning is sufficient.
    if (warnOnly) {
        if (!hasWarnedAboutBadLineEndings)
            logWarning(ei);
        hasWarnedAboutBadLineEndings = true;
    } else
        throw ParseError(ei);
}
// Added 2025-02-05.
inline void State::nulFound(const PosIdx pos)
{
    throw ParseError({
        .msg = HintFmt(
            "NUL bytes (`\\0`) are currently not well supported, because internally strings are NUL-terminated, which may lead to unexpected truncation. Use %s to disable this error.",
            "--extra-deprecated-features nul-bytes"
        ),
        .pos = positions[pos],
    });
}

inline void State::addAttr(ExprAttrs * attrs, AttrPath && attrPath, std::unique_ptr<Expr> e, const PosIdx pos)
{
    AttrPath::iterator i;
    // All attrpaths have at least one attr
    assert(!attrPath.empty());
    // Checking attrPath validity.
    // ===========================
    for (i = attrPath.begin(); i + 1 < attrPath.end(); i++) {
        if (i->symbol) {
            ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
            if (j != attrs->attrs.end()) {
                if (j->second.kind != ExprAttrs::AttrDef::Kind::Inherited) {
                    ExprSet * attrs2 = dynamic_cast<ExprSet *>(j->second.e.get());
                    if (!attrs2) {
                        attrPath.erase(i + 1, attrPath.end());
                        dupAttr(attrPath, pos, j->second.pos);
                    }
                    attrs = attrs2;
                } else {
                    attrPath.erase(i + 1, attrPath.end());
                    dupAttr(attrPath, pos, j->second.pos);
                }
            } else {
                auto next = attrs->attrs.emplace(std::piecewise_construct,
                    std::tuple(i->symbol),
                    std::tuple(std::make_unique<ExprSet>(), pos));
                attrs = static_cast<ExprSet *>(next.first->second.e.get());
            }
        } else {
            auto & next = attrs->dynamicAttrs.emplace_back(std::move(i->expr), std::make_unique<ExprSet>(), pos);
            attrs = static_cast<ExprSet *>(next.valueExpr.get());
        }
    }
    // Expr insertion.
    // ==========================
    if (i->symbol) {
        ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
        if (j != attrs->attrs.end()) {
            // This attr path is already defined. However, if both
            // e and the expr pointed by the attr path are two attribute sets,
            // we want to merge them.
            // Otherwise, throw an error.
            auto * ae = dynamic_cast<ExprSet *>(e.get());
            auto * jAttrs = dynamic_cast<ExprSet *>(j->second.e.get());
            if (jAttrs && ae) {
                if (ae->inheritFromExprs && !jAttrs->inheritFromExprs)
                    jAttrs->inheritFromExprs = std::make_unique<std::list<std::unique_ptr<Expr>>>();
                for (auto & ad : ae->attrs) {
                    auto j2 = jAttrs->attrs.find(ad.first);
                    if (j2 != jAttrs->attrs.end()) // Attr already defined in iAttrs, error.
                        return dupAttr(ad.first, j2->second.pos, ad.second.pos);
                    if (ad.second.kind == ExprAttrs::AttrDef::Kind::InheritedFrom) {
                        auto & sel = dynamic_cast<ExprSelect &>(*ad.second.e);
                        auto & from = dynamic_cast<ExprInheritFrom &>(*sel.e);
                        from.displ += jAttrs->inheritFromExprs->size();
                    }
                    jAttrs->attrs.emplace(ad.first, std::move(ad.second));
                }
                std::ranges::move(ae->dynamicAttrs, std::back_inserter(jAttrs->dynamicAttrs));
                if (ae->inheritFromExprs)
                    std::ranges::move(*ae->inheritFromExprs, std::back_inserter(*jAttrs->inheritFromExprs));
            } else {
                dupAttr(attrPath, pos, j->second.pos);
            }
        } else {
            // Before inserting new attrs, check for __override and throw an error
            // (the error will initially be a warning to ease migration)
            if (!featureSettings.isEnabled(Dep::RecSetOverrides) && i->symbol == s.overrides) {
                if (auto set = dynamic_cast<ExprSet *>(attrs); set && set->recursive)
                    overridesFound(pos);
            }

            // This attr path is not defined. Let's create it.
            e->setName(i->symbol);
            attrs->attrs.emplace(std::piecewise_construct,
                std::tuple(i->symbol),
                std::tuple(std::move(e), pos));
        }
    } else {
        attrs->dynamicAttrs.emplace_back(std::move(i->expr), std::move(e), pos);
    }
}

inline void State::validateLambdaAttrs(AttrsPattern & formals, PosIdx pos)
{
    std::sort(formals.formals.begin(), formals.formals.end(),
        [] (const auto & a, const auto & b) {
            return std::tie(a.name, a.pos) < std::tie(b.name, b.pos);
        });

    std::optional<std::pair<Symbol, PosIdx>> duplicate;
    for (size_t i = 0; i + 1 < formals.formals.size(); i++) {
        if (formals.formals[i].name != formals.formals[i + 1].name)
            continue;
        std::pair thisDup{formals.formals[i].name, formals.formals[i + 1].pos};
        duplicate = std::min(thisDup, duplicate.value_or(thisDup));
    }
    if (duplicate)
        throw ParseError({
            .msg = HintFmt("duplicate formal function argument '%1%'", symbols[duplicate->first]),
            .pos = positions[duplicate->second]
        });

    if (formals.name && formals.has(formals.name))
        throw ParseError({
            .msg = HintFmt("duplicate formal function argument '%1%'", symbols[formals.name]),
            .pos = positions[pos]
        });
}

inline std::unique_ptr<Expr> State::stripIndentation(
    const PosIdx pos,
    std::vector<IndStringLine> && lines)
{
    /* If the only line is whitespace-only, directly return empty string.
     * The rest of the code relies on the final string not being empty.
     */
    if (lines.size() == 1 && lines.front().parts.empty()) {
        return std::make_unique<ExprString>(pos, "");
    }

    /* If the last line only contains whitespace, trim it to not cause excessive whitespace.
     * (Other whitespace-only lines get stripped only of the common indentation, and excess
     * whitespace becomes part of the string.)
     */
    if (lines.back().parts.empty()) {
        lines.back().indentation = {};
    }

    /* Figure out the minimum indentation. Note that by design
       whitespace-only lines are not taken into account. */
    size_t minIndent = 1000000;
    for (auto & line : lines) {
        if (line.hasContent) {
            minIndent = std::min(minIndent, line.indentation.size());
        }
    }

    /* Strip spaces from each line. */
    for (auto & line : lines) {
        line.indentation.remove_prefix(std::min(minIndent, line.indentation.size()));
    }

    /* Concat the parts together again */

    std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>> parts;
    /* Accumulator for merging intermediates */
    PosIdx merged_pos;
    std::string merged = "";

    auto push_merged = [&] (PosIdx i_pos, std::string_view str) {
        if (merged.empty()) {
            merged_pos = i_pos;
        }
        merged += str;
    };

    auto flush_merged = [&] () {
        if (!merged.empty()) {
            parts.emplace_back(merged_pos, std::make_unique<ExprString>(pos, std::string(merged)));
            merged.clear();
        }
    };

    for (auto && [li, line] : enumerate(lines)) {
        push_merged(line.pos, line.indentation);

        for (auto & val : line.parts) {
            auto &[i_pos, item] = val;

            std::visit(overloaded{
                [&](std::string_view str) {
                    push_merged(i_pos, str);
                },
                [&](std::unique_ptr<Expr> expr) {
                    flush_merged();
                    parts.emplace_back(i_pos, std::move(expr));
                },
            }, std::move(item));
        }
    }

    flush_merged();

    /* If this is a single string, then don't do a concatenation.
     * (If it's a single expression, still do the ConcatStrings to properly force it being a string.)
     */
    if (parts.size() == 1 && dynamic_cast<ExprString *>(parts[0].second.get())) {
        return std::move(parts[0].second);
    }
    return std::make_unique<ExprConcatStrings>(pos, true, std::move(parts));
}

}
