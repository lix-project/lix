#include "lix/libexpr/nixexpr.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/symbol-table.hh"
#include "lix/libexpr/print.hh"
#include "lix/libutil/escape-string.hh"

#include <cstdlib>
#include <sstream>

using json = nlohmann::json;

namespace nix {

ExprBlackHole eBlackHole;
Expr *eBlackHoleAddr = &eBlackHole;

// FIXME: remove, because *symbols* are abstract and do not have a single
//        textual representation; see printIdentifier()
std::ostream & operator <<(std::ostream & str, const SymbolStr & symbol)
{
    std::string_view s = symbol;
    return printIdentifier(str, s);
}

AttrName::AttrName(PosIdx pos, Symbol s) : pos(pos), symbol(s)
{
}

AttrName::AttrName(PosIdx pos, std::unique_ptr<Expr> e) : pos(pos), expr(std::move(e))
{
}

json Expr::toJSON(const SymbolTable & symbols) const
{
    abort();
}

json ExprInt::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprInt"},
        {"value", n.value}
    };
}

json ExprFloat::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprFloat"},
        {"value", nf}
    };
}

json ExprString::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprString"},
        {"value", s}
    };
}

json ExprPath::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprPath"},
        {"value", s}
    };
}

json ExprVar::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprVar"},
        {"value", symbols[name]}
    };
}

json ExprInheritFrom::toJSON(SymbolTable const & symbols) const
{
    return {
        {"_type", "ExprInheritFrom"}
    };
}

json ExprSelect::toJSON(const SymbolTable & symbols) const
{
    json out = {
        {"_type", "ExprSelect"},
        {"e", e->toJSON(symbols)},
        {"attrs", printAttrPathToJson(symbols, attrPath)}
    };
    if (def)
        out["default"] = def->toJSON(symbols);
    return out;
}

json ExprOpHasAttr::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprOpHasAttr"},
        {"e", e->toJSON(symbols)},
        {"attrs", printAttrPathToJson(symbols, attrPath)}
    };
}

void ExprAttrs::addBindingsToJSON(json & out, const SymbolTable & symbols) const
{
    typedef const decltype(attrs)::value_type * Attr;
    std::vector<Attr> sorted;
    for (auto & i : attrs) sorted.push_back(&i);
    std::sort(sorted.begin(), sorted.end(), [&](Attr a, Attr b) {
        std::string_view sa = symbols[a->first], sb = symbols[b->first];
        return sa < sb;
    });
    std::map<Displacement, std::vector<Symbol>> inheritsFrom;
    for (auto & i : sorted) {
        switch (i->second.kind) {
        case AttrDef::Kind::Plain:
            out["attrs"][symbols[i->first]] = i->second.e->toJSON(symbols);
            break;
        case AttrDef::Kind::Inherited:
            out["inherit"][symbols[i->first]] = i->second.e->toJSON(symbols);
            break;
        case AttrDef::Kind::InheritedFrom: {
            auto & select = dynamic_cast<ExprSelect &>(*i->second.e);
            auto & from = dynamic_cast<ExprInheritFrom &>(*select.e);
            inheritsFrom[from.displ].push_back(i->first);
            break;
        }
        }
    }

    for (const auto & [from, syms] : inheritsFrom) {
        json attrs = json::array();
        for (auto sym : syms)
            attrs.push_back(symbols[sym]);
        out["inheritFrom"].push_back({
            {"from", (*inheritFromExprs)[from]->toJSON(symbols)},
            {"attrs", attrs}
        });
    }

    for (auto & i : dynamicAttrs) {
        out["dynamicAttrs"].push_back({
            {"name", i.nameExpr->toJSON(symbols) },
            {"value", i.valueExpr->toJSON(symbols)}
        });
    }
}

json ExprSet::toJSON(const SymbolTable & symbols) const
{
    json out = {
        {"_type", "ExprSet"},
        {"recursive", recursive},
    };
    addBindingsToJSON(out, symbols);
    return out;
}

json ExprList::toJSON(const SymbolTable & symbols) const
{
    json list = json::array();
    for (auto & i : elems)
        list.push_back(i->toJSON(symbols));
    return {
        { "_type", "ExprList" },
        { "elems", list },
    };
}

json ExprLambda::toJSON(const SymbolTable & symbols) const
{
    json out = {
        { "_type", "ExprLambda" },
        { "body", body->toJSON(symbols) }
    };
    if (hasFormals()) {
        // the natural Symbol ordering is by creation time, which can lead to the
        // same expression being printed in two different ways depending on its
        // context. always use lexicographic ordering to avoid this.
        for (const Formal & i : formals->lexicographicOrder(symbols)) {
            if (i.def)
                out["formals"][symbols[i.name]] = i.def->toJSON(symbols);
            else
                out["formals"][symbols[i.name]] = nullptr;
        }
        out["formalsEllipsis"] = formals->ellipsis;
    }
    if (arg)
        out["arg"] = symbols[arg];
    return out;
}

json ExprCall::toJSON(const SymbolTable & symbols) const
{
    json outArgs = json::array();
    for (auto & e : args)
        outArgs.push_back(e->toJSON(symbols));
    return {
        {"_type", "ExprCall"},
        {"fun", fun->toJSON(symbols)},
        {"args", outArgs}
    };
}

json ExprLet::toJSON(const SymbolTable & symbols) const
{
    json out = {
        { "_type", "ExprLet" },
        { "body", body->toJSON(symbols) }
    };
    addBindingsToJSON(out, symbols);
    return out;
}

json ExprWith::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprWith"},
        {"attrs", attrs->toJSON(symbols)},
        {"body", body->toJSON(symbols)}
    };
}

json ExprIf::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprIf"},
        {"cond", cond->toJSON(symbols)},
        {"then", then->toJSON(symbols)},
        {"else", else_->toJSON(symbols)}
    };
}

json ExprAssert::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprAssert"},
        {"cond", cond->toJSON(symbols)},
        {"body", body->toJSON(symbols)}
    };
}

json ExprOpNot::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprOpNot"},
        {"e", e->toJSON(symbols)}
    };
}

json ExprConcatStrings::toJSON(const SymbolTable & symbols) const
{
    json parts = json::array();
    for (auto & [_pos, part] : es)
        parts.push_back(part->toJSON(symbols));
    return {
        {"_type", "ExprConcatStrings"},
        {"forceString", forceString},
        {"es", parts}
    };
}

json ExprPos::toJSON(const SymbolTable & symbols) const
{
    return {{ "_type", "ExprPos" }};
}


std::string showAttrPath(const SymbolTable & symbols, const AttrPath & attrPath)
{
    std::ostringstream out;
    bool first = true;
    for (auto & i : attrPath) {
        if (!first) out << '.'; else first = false;
        if (i.symbol)
            out << symbols[i.symbol];
        else
            out << "\"${...}\"";
    }
    return out.str();
}

json printAttrPathToJson(const SymbolTable & symbols, const AttrPath & attrPath)
{
    json out = json::array();
    for (auto & i : attrPath) {
        if (i.symbol)
            out.push_back(symbols[i.symbol]);
        else
            out.push_back(i.expr->toJSON(symbols));
    }
    return out;
}


/* Computing levels/displacements for variables. */

void Expr::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    abort();
}

void ExprInt::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));
}

void ExprFloat::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));
}

void ExprString::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));
}

void ExprPath::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));
}

void ExprVar::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    fromWith = nullptr;

    /* Check whether the variable appears in the environment.  If so,
       set its level and displacement. */
    const StaticEnv * curEnv;
    Level level;
    int withLevel = -1;
    for (curEnv = env.get(), level = 0; curEnv; curEnv = curEnv->up, level++) {
        if (curEnv->isWith) {
            if (withLevel == -1) withLevel = level;
        } else {
            auto i = curEnv->find(name);
            if (i != curEnv->vars.end()) {
                if (this->needsRoot && !curEnv->isRoot) {
                    throw ParseError({
                        .msg = HintFmt(
                            "Shadowing symbol '%s' used in internal expressions is not allowed. Use %s to disable this error.",
                            es.symbols[name],
                            "--extra-deprecated-features shadow-internal-symbols"
                        ),
                        .pos = es.positions[pos]
                    });
                }

                this->level = level;
                displ = i->second;
                return;
            }
        }
    }

    /* Otherwise, the variable must be obtained from the nearest
       enclosing `with'.  If there is no `with', then we can issue an
       "undefined variable" error now. */
    if (withLevel == -1)
        es.errors.make<UndefinedVarError>(
            "undefined variable '%1%'",
            es.symbols[name]
        ).atPos(pos).debugThrow();
    for (auto * e = env.get(); e && !fromWith; e = e->up)
        fromWith = e->isWith;
    this->level = withLevel;
}

void ExprInheritFrom::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));
}

void ExprSelect::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    e->bindVars(es, env);
    if (def) def->bindVars(es, env);
    for (auto & i : attrPath)
        if (!i.symbol)
            i.expr->bindVars(es, env);
}

void ExprOpHasAttr::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    e->bindVars(es, env);
    for (auto & i : attrPath)
        if (!i.symbol)
            i.expr->bindVars(es, env);
}

std::shared_ptr<const StaticEnv> ExprAttrs::bindInheritSources(
    Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (!inheritFromExprs)
        return nullptr;

    // the inherit (from) source values are inserted into an env of its own, which
    // does not introduce any variable names.
    // analysis must see an empty env, or an env that contains only entries with
    // otherwise unused names to not interfere with regular names. the parser
    // has already filled all exprs that access this env with appropriate level
    // and displacement, and nothing else is allowed to access it. ideally we'd
    // not even *have* an expr that grabs anything from this env since it's fully
    // invisible, but the evaluator does not allow for this yet.
    auto inner = std::make_shared<StaticEnv>(nullptr, env.get(), 0);
    for (auto & from : *inheritFromExprs)
        from->bindVars(es, env);

    return inner;
}

void ExprSet::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    if (recursive) {
        auto newEnv = [&] () -> std::shared_ptr<const StaticEnv> {
            auto newEnv = std::make_shared<StaticEnv>(nullptr, env.get(), attrs.size());

            Displacement displ = 0;
            for (auto & i : attrs)
                newEnv->vars.emplace_back(i.first, i.second.displ = displ++);
            return newEnv;
        }();

        // No need to sort newEnv since attrs is in sorted order.

        auto inheritFromEnv = bindInheritSources(es, newEnv);
        for (auto & i : attrs)
            i.second.e->bindVars(es, i.second.chooseByKind(newEnv, env, inheritFromEnv));

        for (auto & i : dynamicAttrs) {
            i.nameExpr->bindVars(es, newEnv);
            i.valueExpr->bindVars(es, newEnv);
        }
    }
    else {
        auto inheritFromEnv = bindInheritSources(es, env);

        for (auto & i : attrs)
            i.second.e->bindVars(es, i.second.chooseByKind(env, env, inheritFromEnv));

        for (auto & i : dynamicAttrs) {
            i.nameExpr->bindVars(es, env);
            i.valueExpr->bindVars(es, env);
        }
    }
}

void ExprList::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    for (auto & i : elems)
        i->bindVars(es, env);
}

void ExprLambda::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    auto newEnv = std::make_shared<StaticEnv>(
        nullptr, env.get(),
        (hasFormals() ? formals->formals.size() : 0) +
        (!arg ? 0 : 1));

    Displacement displ = 0;

    if (arg) newEnv->vars.emplace_back(arg, displ++);

    if (hasFormals()) {
        for (auto & i : formals->formals)
            newEnv->vars.emplace_back(i.name, displ++);

        newEnv->sort();

        for (auto & i : formals->formals)
            if (i.def) i.def->bindVars(es, newEnv);
    }

    body->bindVars(es, newEnv);
}

void ExprCall::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    fun->bindVars(es, env);
    for (auto & e : args)
        e->bindVars(es, env);
}

void ExprLet::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    auto newEnv = [&] () -> std::shared_ptr<const StaticEnv> {
        auto newEnv = std::make_shared<StaticEnv>(nullptr, env.get(), attrs.size());

        Displacement displ = 0;
        for (auto & i : attrs)
            newEnv->vars.emplace_back(i.first, i.second.displ = displ++);
        return newEnv;
    }();

    // No need to sort newEnv since attrs is in sorted order.

    auto inheritFromEnv = bindInheritSources(es, newEnv);
    for (auto & i : attrs)
        i.second.e->bindVars(es, i.second.chooseByKind(newEnv, env, inheritFromEnv));

    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    body->bindVars(es, newEnv);
}

void ExprWith::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    parentWith = nullptr;
    for (auto * e = env.get(); e && !parentWith; e = e->up)
        parentWith = e->isWith;

    /* Does this `with' have an enclosing `with'?  If so, record its
       level so that `lookupVar' can look up variables in the previous
       `with' if this one doesn't contain the desired attribute. */
    const StaticEnv * curEnv;
    Level level;
    prevWith = 0;
    for (curEnv = env.get(), level = 1; curEnv; curEnv = curEnv->up, level++)
        if (curEnv->isWith) {
            prevWith = level;
            break;
        }

    attrs->bindVars(es, env);
    auto newEnv = std::make_shared<StaticEnv>(this, env.get());
    body->bindVars(es, newEnv);
}

void ExprIf::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    cond->bindVars(es, env);
    then->bindVars(es, env);
    else_->bindVars(es, env);
}

void ExprAssert::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    cond->bindVars(es, env);
    body->bindVars(es, env);
}

void ExprOpNot::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    e->bindVars(es, env);
}

void ExprConcatStrings::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));

    for (auto & i : this->es)
        i.second->bindVars(es, env);
}

void ExprPos::bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debug)
        es.debug->exprEnvs.insert(std::make_pair(this, env));
}


/* Storing function names. */

void Expr::setName(Symbol name)
{
}


void ExprLambda::setName(Symbol name)
{
    this->name = name;
    body->setName(name);
}


std::string ExprLambda::showNamePos(const EvalState & state) const
{
    std::string id(name
        ? concatStrings("'", state.ctx.symbols[name], "'")
        : "anonymous function");
    return fmt("%1% at %2%", id, state.ctx.positions[pos]);
}



/* Position table. */

Pos PosTable::operator[](PosIdx p) const
{
    auto origin = resolve(p);
    if (!origin)
        return {};

    const auto offset = origin->offsetOf(p);

    Pos result{0, 0, origin->origin};
    auto lines = this->lines.lock();
    auto & linesForInput = (*lines)[origin->offset];

    if (linesForInput.empty()) {
        auto source = result.getSource().value_or("");
        const char * begin = source.data();
        for (Pos::LinesIterator it(source), end; it != end; it++)
            linesForInput.push_back(it->data() - begin);
        if (linesForInput.empty())
            linesForInput.push_back(0);
    }
    // as above: the first line starts at byte 0 and is always present
    auto lineStartOffset = std::prev(
        std::upper_bound(linesForInput.begin(), linesForInput.end(), offset));

    result.line = 1 + (lineStartOffset - linesForInput.begin());
    result.column = 1 + (offset - *lineStartOffset);
    return result;
}



/* Symbol table. */

size_t SymbolTable::totalSize() const
{
    size_t n = 0;
    dump([&] (const std::string & s) { n += s.size(); });
    return n;
}

}
