#pragma once
///@file

#include "eval.hh"
#include "logging.hh"

namespace nix::parser {

struct StringToken
{
    std::string_view s;
    // canMerge is only used to faithfully reproduce the quirks from the old code base.
    bool canMerge = false;
    operator std::string_view() const { return s; }
};

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
            std::variant<std::unique_ptr<Expr>, StringToken>
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

    void dupAttr(const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos);
    void dupAttr(Symbol attr, const PosIdx pos, const PosIdx prevPos);
    void overridesFound(const PosIdx pos);
    void addAttr(ExprAttrs * attrs, AttrPath && attrPath, std::unique_ptr<Expr> e, const PosIdx pos);
    std::unique_ptr<Formals> validateFormals(std::unique_ptr<Formals> formals, PosIdx pos = noPos, Symbol arg = {});
    std::unique_ptr<Expr> stripIndentation(const PosIdx pos, std::vector<IndStringLine> && line);

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
    warn(
        "%s found at %s. This feature is deprecated and will be removed in the future. Use %s to silence this warning.",
        "__overrides",
        positions[pos],
        "--extra-deprecated-features rec-set-overrides"
    );
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
                    ExprAttrs * attrs2 = dynamic_cast<ExprAttrs *>(j->second.e.get());
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
                    std::tuple(std::make_unique<ExprAttrs>(), pos));
                attrs = static_cast<ExprAttrs *>(next.first->second.e.get());
            }
        } else {
            auto & next = attrs->dynamicAttrs.emplace_back(std::move(i->expr), std::make_unique<ExprAttrs>(), pos);
            attrs = static_cast<ExprAttrs *>(next.valueExpr.get());
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
            auto * ae = dynamic_cast<ExprAttrs *>(e.get());
            auto * jAttrs = dynamic_cast<ExprAttrs *>(j->second.e.get());
            if (jAttrs && ae) {
                if (ae->inheritFromExprs && !jAttrs->inheritFromExprs)
                    jAttrs->inheritFromExprs = std::make_unique<std::vector<ref<Expr>>>();
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
            if (attrs->recursive && !featureSettings.isEnabled(Dep::RecSetOverrides) && i->symbol == s.overrides) {
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

inline std::unique_ptr<Formals> State::validateFormals(std::unique_ptr<Formals> formals, PosIdx pos, Symbol arg)
{
    std::sort(formals->formals.begin(), formals->formals.end(),
        [] (const auto & a, const auto & b) {
            return std::tie(a.name, a.pos) < std::tie(b.name, b.pos);
        });

    std::optional<std::pair<Symbol, PosIdx>> duplicate;
    for (size_t i = 0; i + 1 < formals->formals.size(); i++) {
        if (formals->formals[i].name != formals->formals[i + 1].name)
            continue;
        std::pair thisDup{formals->formals[i].name, formals->formals[i + 1].pos};
        duplicate = std::min(thisDup, duplicate.value_or(thisDup));
    }
    if (duplicate)
        throw ParseError({
            .msg = HintFmt("duplicate formal function argument '%1%'", symbols[duplicate->first]),
            .pos = positions[duplicate->second]
        });

    if (arg && formals->has(arg))
        throw ParseError({
            .msg = HintFmt("duplicate formal function argument '%1%'", symbols[arg]),
            .pos = positions[pos]
        });

    return formals;
}

inline std::unique_ptr<Expr> State::stripIndentation(
    const PosIdx pos,
    std::vector<IndStringLine> && lines)
{
    /* If the only line is whitespace-only, directly return empty string.
     * NOTE: This is not merely an optimization, but `compatStripLeadingEmptyString`
     * later on relies on the string not being empty for working.
     */
    if (lines.size() == 1 && lines.front().parts.empty()) {
        return std::make_unique<ExprString>("");
    }

    /* If the last line only contains whitespace, trim it to not cause excessive whitespace.
     * (Other whitespace-only lines get stripped only of the common indentation, and excess
     * whitespace becomes part of the string.)
     */
    if (lines.back().parts.empty()) {
        lines.back().indentation = {};
    }

    /*
     * Quirk compatibility:
     *
     * » nix-instantiate --parse -E $'\'\'${"foo"}\'\''
     * "foo"
     * » nix-instantiate --parse -E $'\'\'    ${"foo"}\'\''
     * ("" + "foo")
     *
     * Our code always produces the form with the additional "" +, so we'll manually
     * strip it at the end if necessary.
     */
    const bool compatStripLeadingEmptyString = !lines.empty() && lines[0].indentation.empty();

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

    /* Note that we don't concat all adjacent string parts to fully reproduce the original code.
     * This means that any escapes will result in string concatenation even if this is unnecessary.
     */
    std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>> parts;
    /* Accumulator for merging intermediates */
    PosIdx merged_pos;
    std::string merged = "";
    bool has_merged = false;

    auto push_merged = [&] (PosIdx i_pos, std::string_view str) {
        merged += str;
        if (!has_merged) {
            has_merged = true;
            merged_pos = i_pos;
        }
    };

    auto flush_merged = [&] () {
        if (has_merged) {
            parts.emplace_back(merged_pos, std::make_unique<ExprString>(std::string(merged)));
            merged.clear();
            has_merged = false;
        }
    };

    for (auto && [li, line] : enumerate(lines)) {
        /* Always merge indentation, except for the first line when compatStripLeadingEmptyString is set (see above) */
        if (!compatStripLeadingEmptyString || li != 0) {
            push_merged(line.pos, line.indentation);
        }

        for (auto & val : line.parts) {
            auto &[i_pos, item] = val;

            std::visit(overloaded{
                [&](StringToken str) {
                    if (str.canMerge) {
                        push_merged(i_pos, str.s);
                    } else {
                        flush_merged();
                        parts.emplace_back(i_pos, std::make_unique<ExprString>(std::string(str.s)));
                    }
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
