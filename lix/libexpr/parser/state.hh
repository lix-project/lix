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
    NixSymbolTable & symbols;
    PosTable & positions;
    SourcePath basePath;
    PosTable::Origin origin;
    const FeatureSettings & featureSettings;
    bool hasWarnedAboutBadLineEndings = false; // State to only warn on first occurrence

    void dupAttr(const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos);
    void dupAttr(Symbol attr, const PosIdx pos, const PosIdx prevPos);
    void overridesFound(const PosIdx pos);
    void badLineEndingFound(const PosIdx pos, bool warnOnly);
    void badFirstLineIndStringFound(const PosIdx pos);
    void badSingleLineIndStringFound(const PosIdx pos);
    void badEscapeFound(const PosIdx pos, char found, std::string escape);
    void nulFound(const PosIdx pos);
    void recSetMergeFound(const AttrPath & attrPath, const PosIdx pos);
    void recSetDynamicAttrFound(const PosIdx pos);
    void orIdentifierFound(const PosIdx pos);
    void orArgumentFound(const PosIdx pos);
    void whitespaceBetweenTokensRequired(const PosIdx pos);
    void addAttr(ExprAttrs * attrs, AttrPath && attrPath, std::unique_ptr<Expr> e, const PosIdx pos);
    void mergeAttrs(AttrPath & attrPath, ExprSet * source, ExprSet * target);
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
    // Added 2024-09-18 as a warning, updated and made hard error 2025-11-27
    // See the documentation on deprecated features for more details.
    throw ParseError({
        .msg = HintFmt(
            "%s attributes are deprecated and will be removed in the future. Use %s to silence this warning.",
            "__overrides",
            "--extra-deprecated-features rec-set-overrides"
        ),
        .pos = positions[pos],
    });
}

// Both added 2026-01-30. Probably won't turn this one into an error for a while,

// as this has quite a lot of use in the wild. But it's clearly wrong code,
// so we should warn users about it.
// See the documentation on deprecated features for more details.
inline void State::badSingleLineIndStringFound(const PosIdx pos)
{
    logWarning({
        .msg = HintFmt(
            "Whitespace in a ''-string will be stripped even if the string only has a single line, which is most likely not the intent of the code. To fix this, remove the whitespace or replace the string with \" instead. Use %s to silence this warning.",
            "--extra-deprecated-features broken-string-indentation"
        ),
        .pos = positions[pos],
    });
}
inline void State::badFirstLineIndStringFound(const PosIdx pos)
{
    logWarning({
        .msg = HintFmt(
            "Whitespace calculations for indentation stripping in a multiline ''-string include the first line, so putting text on it will effectively disable all indentation stripping. To fix this, simply break the line right after the string starts. Use %s to silence this warning.",
            "--extra-deprecated-features broken-string-indentation"
        ),
        .pos = positions[pos],
    });
}
// Added 2024-12-12, equally used in the wild.
inline void State::badEscapeFound(const PosIdx pos, char found, std::string escape)
{
    logWarning({
        .msg = HintFmt(
            "%s is an ill-defined escape. You can drop the %s and simply write %s instead. Use %s "
            "to silence this warning.",
            escape + found,
            escape,
            found,
            "--extra-deprecated-features broken-string-escape"
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
// Added 2025-11-23
inline void State::recSetMergeFound(const AttrPath & attrPath, const PosIdx pos)
{
    throw ParseError({
        .msg = HintFmt(
            "attribute '%s' cannot be merged, because one set is marked as recursive and the other "
            "isn't. Use %s to disable this error and make the expression parse as-is with "
            "implementation-defined semantics.",
            showAttrPath(symbols, attrPath),
            "--extra-deprecated-features rec-set-merges"
        ),
        .pos = positions[pos],
    });
}
// Added 2025-11-24
inline void State::recSetDynamicAttrFound(const PosIdx pos)
{
    throw ParseError({
        .msg = HintFmt(
            "dynamic attributes are not allowed within recursive attrsets, because they would be "
            "evaluated separately from the other recursive attributes. Use %s to disable this "
            "error.",
            "--extra-deprecated-features rec-set-dynamic-attrs"
        ),
        .pos = positions[pos],
    });
}

// Added 2026-01-30
inline void State::orIdentifierFound(const PosIdx pos)
{
    logWarning({
        .msg = HintFmt(
            "using %s as an identifier is deprecated because it cannot be used in most places (try "
            "%s). Use %s to disable this warning.",
            "or",
            "let or = 1; in or",
            "--extra-deprecated-features or-as-identifier"
        ),
        .pos = positions[pos],
    });
}
// Added 2026-01-30
inline void State::orArgumentFound(const PosIdx pos)
{
    logWarning({
        .msg = HintFmt(
            "using %s as an argument is deprecated because it is parsed with the wrong precedence "
            "which may cause unexpected behavior. Use %s to disable this warning.",
            "or",
            "--extra-deprecated-features or-as-identifier"
        ),
        .pos = positions[pos],
    });
}

// Added 2026-01-30
inline void State::whitespaceBetweenTokensRequired(const PosIdx pos)
{
    throw ParseError(
        {.msg = HintFmt(
             "whitespace between function arguments or list elements is required here. Use %s to "
             "disable this error",
             "--extra-deprecated-features tokens-no-whitespace"
         ),
         .pos = positions[pos]}
    );
}

inline void State::addAttr(ExprAttrs * attrs, AttrPath && attrPath, std::unique_ptr<Expr> e, const PosIdx pos)
{
    AttrPath::iterator i;
    // All attrpaths have at least one attr
    assert(!attrPath.empty());

    // Walk the attrpath up to the parent of the attribute we want to insert, moving `attrs` along
    // and creating new empty intermediate attrsets as necessary.
    for (i = attrPath.begin(); i + 1 < attrPath.end(); i++) {
        AttrName & attr = *i;

        if (attr.isDynamic()) {
            /* We don't want to insert dynamic attributes into recursive sets, because that has
             * fucky semantics */
            if (ExprSet * set = dynamic_cast<ExprSet *>(attrs);
                !featureSettings.isEnabled(Dep::RecSetDynamicAttrs) && set && set->recursive)
            {
                recSetDynamicAttrFound(pos);
            }

            // Simply insert an empty attrset (but dynamic)
            auto & next = attrs->dynamicAttrs.emplace_back(std::move(i->expr), std::make_unique<ExprSet>(), pos);
            attrs = static_cast<ExprSet *>(next.valueExpr.get());
        } else if (ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
                   j != attrs->attrs.end())
        {
            // Try to walk down the next attribute, throw duplicate error if not possible
            auto & [foundName, foundDef] = *j;
            if (foundDef.kind == ExprAttrs::AttrDef::Kind::Inherited) {
                attrPath.erase(i + 1, attrPath.end());
                return dupAttr(attrPath, pos, foundDef.pos);
            }
            ExprSet * foundAttrs = dynamic_cast<ExprSet *>(foundDef.e.get());
            if (!foundAttrs) {
                attrPath.erase(i + 1, attrPath.end());
                return dupAttr(attrPath, pos, foundDef.pos);
            }
            attrs = foundAttrs;
        } else {
            // Simply insert an empty attrset
            auto next = attrs->attrs.emplace(
                std::piecewise_construct,
                std::tuple(attr.symbol),
                std::tuple(std::make_unique<ExprSet>(), pos)
            );
            // Before inserting new attrs, check for __override and throw an error
            // (the error will initially be a warning to ease migration)
            if (!featureSettings.isEnabled(Dep::RecSetOverrides) && attr.symbol == symbols.sym___overrides) {
                if (auto set = dynamic_cast<ExprSet *>(attrs); set && set->recursive) {
                    overridesFound(pos);
                }
            }
            attrs = static_cast<ExprSet *>(next.first->second.e.get());
        }
    }

    // Expr insertion.
    // ==========================
    AttrName & attr = *i;

    if (attr.isDynamic()) {
        /* We don't want to insert dynamic attributes into recursive sets, because that has
         * fucky semantics */
        if (ExprSet * set = dynamic_cast<ExprSet *>(attrs);
            !featureSettings.isEnabled(Dep::RecSetDynamicAttrs) && set && set->recursive)
        {
            recSetDynamicAttrFound(pos);
        }

        attrs->dynamicAttrs.emplace_back(std::move(attr.expr), std::move(e), pos);
    } else if (ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(attr.symbol);
               j != attrs->attrs.end())
    {
        // This attr path is already defined. However, if both
        // e and the expr pointed by the attr path are two attribute sets,
        // we want to merge them.
        // Otherwise, throw an error.
        auto & [foundName, foundDef] = *j;

        auto * insertAttrs = dynamic_cast<ExprSet *>(e.get());
        auto * foundAttrs = dynamic_cast<ExprSet *>(foundDef.e.get());
        if (!foundAttrs || !insertAttrs) {
            return dupAttr(attrPath, pos, foundDef.pos);
        }
        mergeAttrs(attrPath, insertAttrs, foundAttrs);
    } else {
        // This attr path is not defined. Let's create it.

        // Before inserting new attrs, check for __override and throw an error
        // (the error will initially be a warning to ease migration)
        if (!featureSettings.isEnabled(Dep::RecSetOverrides) && attr.symbol == symbols.sym___overrides) {
            if (auto set = dynamic_cast<ExprSet *>(attrs); set && set->recursive) {
                overridesFound(pos);
            }
        }
        // Also check for recursive sets to insert into and throw an error
        // We check on the attrpath length because `x = null;` in `rec { x = null; }` is allowed but
        // not `{ x = rec {}; x.y = null; }`.
        // (Note that `rec { x.y = null; }` is not affected by this condition because `attrs`
        // currently points to `x` (non-rec`) and not to the outer set.)
        if (!featureSettings.isEnabled(Dep::RecSetMerges) && attrPath.size() > 1) {
            if (auto set = dynamic_cast<ExprSet *>(attrs); set && set->recursive) {
                recSetMergeFound(attrPath, e->pos);
            }
        }

        e->setName(attr.symbol);
        attrs->attrs.emplace(
            std::piecewise_construct, std::tuple(attr.symbol), std::tuple(std::move(e), pos)
        );
    }
}

/* mutably merge source into target. attrPath is only for error messages */
inline void State::mergeAttrs(AttrPath & attrPath, ExprSet * source, ExprSet * target)
{
    // Before merging, we check that either both or neither are marked as `rec` and throw an error
    // otherwise
    if (!featureSettings.isEnabled(Dep::RecSetMerges) && source->recursive != target->recursive) {
        recSetMergeFound(attrPath, source->pos);
    }

    if (source->inheritFromExprs && !target->inheritFromExprs) {
        target->inheritFromExprs = std::make_unique<std::list<std::unique_ptr<Expr>>>();
    }
    for (auto & [insertKey, insertDef] : source->attrs) {
        if (auto collision = target->attrs.find(insertKey); collision != target->attrs.end()) {
            // Attr already defined in target, recurse merge if possible otherwise error.
            auto * collisionInsert = dynamic_cast<ExprSet *>(insertDef.e.get());
            auto * collisionTarget = dynamic_cast<ExprSet *>(collision->second.e.get());
            if (!collisionInsert || !collisionTarget) {
                attrPath.push_back(AttrName(insertDef.pos, insertKey));
                return dupAttr(attrPath, insertDef.pos, collision->second.pos);
            }

            // Push insertKey to the attrPath for error propagation (pop afterwards), then recurse
            // merge
            attrPath.push_back(AttrName(insertDef.pos, insertKey));
            mergeAttrs(attrPath, collisionInsert, collisionTarget);
            attrPath.pop_back();
        }
        if (insertDef.kind == ExprAttrs::AttrDef::Kind::InheritedFrom) {
            auto & sel = dynamic_cast<ExprSelect &>(*insertDef.e);
            auto & from = dynamic_cast<ExprInheritFrom &>(*sel.e);
            from.displ += target->inheritFromExprs->size();
        }
        target->attrs.emplace(insertKey, std::move(insertDef));
    }
    std::ranges::move(source->dynamicAttrs, std::back_inserter(target->dynamicAttrs));
    if (source->inheritFromExprs) {
        std::ranges::move(*source->inheritFromExprs, std::back_inserter(*target->inheritFromExprs));
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
