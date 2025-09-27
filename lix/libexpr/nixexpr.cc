#include "lix/libexpr/nixexpr.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/symbol-table.hh"
#include "lix/libexpr/print.hh"

#include <cstdlib>
#include <sstream>

namespace nix {

ExprBlackHole eBlackHole;

static Env nullEnv;
Value::Thunk Value::blackHole{{&nullEnv}, &eBlackHole};

// FIXME: remove, because *symbols* are abstract and do not have a single
//        textual representation; see printIdentifier()
std::ostream & operator <<(std::ostream & str, const SymbolStr & symbol)
{
    std::string_view s = symbol;
    return printIdentifier(str, s);
}

std::ostream & operator<<(std::ostream & str, const InternedSymbol & symbol)
{
    str << SymbolStr(symbol);
    return str;
}

AttrName::AttrName(PosIdx pos, Symbol s) : pos(pos), symbol(s)
{
}

AttrName::AttrName(PosIdx pos, std::unique_ptr<Expr> e) : pos(pos), expr(std::move(e))
{
}

JSON Expr::toJSON(const SymbolTable & symbols) const
{
    abort();
}

JSON ExprLiteral::toJSON(const SymbolTable & symbols) const
{
    JSON valueType;
    JSON value;
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (v.type()) {
        case nInt:
            valueType = "Int";
            value = v.integer().value;
            break;
        case nFloat:
            valueType = "Float";
            value = v.fpoint();
            break;
        case nString:
            valueType = "String";
            value = v.str();
            break;
        case nPath:
            valueType = "Path";
            value = v.path().to_string();
            break;
        default:
            assert(false);
    };
    #pragma GCC diagnostic pop

    return {
        {"_type", "ExprLiteral"},
        {"valueType", valueType},
        {"value", value}
    };
}

JSON ExprVar::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprVar"},
        {"value", symbols[name]}
    };
}

JSON ExprInheritFrom::toJSON(SymbolTable const & symbols) const
{
    return {
        {"_type", "ExprInheritFrom"}
    };
}

JSON ExprSelect::toJSON(const SymbolTable & symbols) const
{
    JSON out = {
        {"_type", "ExprSelect"},
        {"e", e->toJSON(symbols)},
        {"attrs", printAttrPathToJson(symbols, attrPath)}
    };
    if (def)
        out["default"] = def->toJSON(symbols);
    return out;
}

JSON ExprOpHasAttr::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprOpHasAttr"},
        {"e", e->toJSON(symbols)},
        {"attrs", printAttrPathToJson(symbols, attrPath)}
    };
}

void ExprAttrs::addBindingsToJSON(JSON & out, const SymbolTable & symbols) const
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
            out["attrs"][std::string(symbols[i->first])] = i->second.e->toJSON(symbols);
            break;
        case AttrDef::Kind::Inherited:
            out["inherit"][std::string(symbols[i->first])] = i->second.e->toJSON(symbols);
            break;
        case AttrDef::Kind::InheritedFrom: {
            auto & select = i->second.e->cast<ExprSelect>();
            auto & from = select.e->cast<ExprInheritFrom>();
            inheritsFrom[from.displ].push_back(i->first);
            break;
        }
        }
    }

    std::vector<Expr *> inheritFromExprs;
    if (this->inheritFromExprs) {
        for (auto & e : *this->inheritFromExprs) {
            inheritFromExprs.push_back(e.get());
        }
    }

    for (const auto & [from, syms] : inheritsFrom) {
        JSON attrs = JSON::array();
        for (auto sym : syms)
            attrs.push_back(symbols[sym]);
        out["inheritFrom"].push_back({
            {"from", inheritFromExprs[from]->toJSON(symbols)},
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

JSON ExprSet::toJSON(const SymbolTable & symbols) const
{
    JSON out = {
        {"_type", "ExprSet"},
        {"recursive", recursive},
    };
    addBindingsToJSON(out, symbols);
    return out;
}

JSON ExprList::toJSON(const SymbolTable & symbols) const
{
    JSON list = JSON::array();
    for (auto & i : elems)
        list.push_back(i->toJSON(symbols));
    return {
        { "_type", "ExprList" },
        { "elems", list },
    };
}

void SimplePattern::addBindingsToJSON(JSON & out, const SymbolTable & symbols) const
{
    out["arg"] = symbols[name];
}

void AttrsPattern::addBindingsToJSON(JSON & out, const SymbolTable & symbols) const
{
    if (name)
        out["arg"] = symbols[name];

    // the natural Symbol ordering is by creation time, which can lead to the
    // same expression being printed in two different ways depending on its
    // context. always use lexicographic ordering to avoid this.
    for (const Formal & i : lexicographicOrder(symbols)) {
        if (i.def)
            out["formals"][std::string(symbols[i.name])] = i.def->toJSON(symbols);
        else
            out["formals"][std::string(symbols[i.name])] = nullptr;
    }
    out["formalsEllipsis"] = ellipsis;
}

JSON ExprLambda::toJSON(const SymbolTable & symbols) const
{
    JSON out = {
        { "_type", "ExprLambda" },
        { "body", body->toJSON(symbols) }
    };
    pattern->addBindingsToJSON(out, symbols);
    return out;
}

JSON ExprCall::toJSON(const SymbolTable & symbols) const
{
    JSON outArgs = JSON::array();
    for (auto & e : args)
        outArgs.push_back(e->toJSON(symbols));
    return {
        {"_type", "ExprCall"},
        {"fun", fun->toJSON(symbols)},
        {"args", outArgs}
    };
}

JSON ExprLet::toJSON(const SymbolTable & symbols) const
{
    JSON out = {
        { "_type", "ExprLet" },
        { "body", body->toJSON(symbols) }
    };
    addBindingsToJSON(out, symbols);
    return out;
}

JSON ExprWith::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprWith"},
        {"attrs", attrs->toJSON(symbols)},
        {"body", body->toJSON(symbols)}
    };
}

JSON ExprIf::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprIf"},
        {"cond", cond->toJSON(symbols)},
        {"then", then->toJSON(symbols)},
        {"else", else_->toJSON(symbols)}
    };
}

JSON ExprAssert::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprAssert"},
        {"cond", cond->toJSON(symbols)},
        {"body", body->toJSON(symbols)}
    };
}

JSON ExprOpNot::toJSON(const SymbolTable & symbols) const
{
    return {
        {"_type", "ExprOpNot"},
        {"e", e->toJSON(symbols)}
    };
}

JSON ExprConcatStrings::toJSON(const SymbolTable & symbols) const
{
    JSON parts = JSON::array();
    for (auto & [_pos, part] : es)
        parts.push_back(part->toJSON(symbols));
    return {
        {"_type", "ExprConcatStrings"},
        {"isInterpolation", isInterpolation},
        {"es", parts}
    };
}

JSON ExprPos::toJSON(const SymbolTable & symbols) const
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

JSON printAttrPathToJson(const SymbolTable & symbols, const AttrPath & attrPath)
{
    JSON out = JSON::array();
    for (auto & i : attrPath) {
        if (i.symbol)
            out.push_back(symbols[i.symbol]);
        else
            out.push_back(i.expr->toJSON(symbols));
    }
    return out;
}


/* Computing levels/displacements for variables. */

namespace {
struct VarBinder : ExprVisitor
{
    Evaluator & es;
    std::shared_ptr<const StaticEnv> env;

    VarBinder(Evaluator & eval, std::shared_ptr<const StaticEnv> env) : es(eval), env(env) {}

    auto withEnv(std::shared_ptr<const StaticEnv> env, auto fn)
    {
        std::swap(env, this->env);
        KJ_DEFER(std::swap(env, this->env););
        return fn();
    }

    using ExprVisitor::visit;

    void visit(ExprDebugFrame & e, std::unique_ptr<Expr> & ptr) override
    {
        visit(e.inner);
    }
    void visit(ExprLiteral & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprVar & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprInheritFrom & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprSelect & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprOpHasAttr & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprSet & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprList & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprLambda & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprCall & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprLet & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprWith & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprIf & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprAssert & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprOpNot & e, std::unique_ptr<Expr> & ptr) override;
#define BINOP(type)                                            \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */           \
    void visit(type & e, std::unique_ptr<Expr> & ptr) override \
    {                                                          \
        visit(e.e1);                                           \
        visit(e.e2);                                           \
    }
    BINOP(ExprOpEq)
    BINOP(ExprOpNEq)
    BINOP(ExprOpAnd)
    BINOP(ExprOpOr)
    BINOP(ExprOpImpl)
    BINOP(ExprOpUpdate)
    BINOP(ExprOpConcatLists)
#undef BINOP
    void visit(ExprConcatStrings & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprPos & e, std::unique_ptr<Expr> & ptr) override;
    void visit(ExprBlackHole & e, std::unique_ptr<Expr> & ptr) override {}
};
}

struct DebugVarBinder : VarBinder
{
    using VarBinder::VarBinder, VarBinder::visit;

#define OVERRIDE(type)                                         \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */           \
    void visit(type & e, std::unique_ptr<Expr> & ptr) override \
    {                                                          \
        es.debug->exprEnvs.insert(std::make_pair(&e, env));    \
        VarBinder::visit(e, ptr);                              \
    }

    OVERRIDE(ExprLiteral)
    OVERRIDE(ExprVar)
    OVERRIDE(ExprInheritFrom)
    void visit(ExprSelect & e, std::unique_ptr<Expr> & ptr) override
    {
        es.debug->exprEnvs.insert(std::make_pair(&e, env));
        VarBinder::visit(e, ptr);

        ptr = std::make_unique<ExprDebugFrame>(e.pos, std::move(ptr), "while evaluating an attribute");
    }
    OVERRIDE(ExprOpHasAttr)
    OVERRIDE(ExprSet)
    OVERRIDE(ExprList)
    void visit(ExprLambda & e, std::unique_ptr<Expr> & ptr) override
    {
        es.debug->exprEnvs.insert(std::make_pair(&e, env));
        VarBinder::visit(e, ptr);

        e.body = std::make_unique<ExprDebugFrame>(
            e.pos, std::move(e.body), HintFmt("while calling %s", e.getQuotedName(es.symbols)).str()
        );
    }
    void visit(ExprCall & e, std::unique_ptr<Expr> & ptr) override
    {
        es.debug->exprEnvs.insert(std::make_pair(&e, env));
        VarBinder::visit(e, ptr);

        ptr = std::make_unique<ExprDebugFrame>(e.pos, std::move(ptr), "while calling a function");
    }
    void visit(ExprLet & e, std::unique_ptr<Expr> & ptr) override
    {
        es.debug->exprEnvs.insert(std::make_pair(&e, env));
        VarBinder::visit(e, ptr);

        e.body = std::make_unique<ExprDebugFrame>(
            e.pos, std::move(e.body), HintFmt("while evaluating a '%1%' expression", "let").str()
        );
    }
    OVERRIDE(ExprWith)
    OVERRIDE(ExprIf)
    OVERRIDE(ExprAssert)
    OVERRIDE(ExprOpNot)
    OVERRIDE(ExprOpEq)
    OVERRIDE(ExprOpNEq)
    OVERRIDE(ExprOpAnd)
    OVERRIDE(ExprOpOr)
    OVERRIDE(ExprOpImpl)
    OVERRIDE(ExprOpUpdate)
    OVERRIDE(ExprOpConcatLists)
    OVERRIDE(ExprConcatStrings)
    OVERRIDE(ExprPos)
    OVERRIDE(ExprBlackHole)
#undef OVERRIDE
};

std::unique_ptr<Expr> Expr::finalize(
    std::unique_ptr<Expr> parsed, Evaluator & es, const std::shared_ptr<const StaticEnv> & env
)
{
    if (es.debug) {
        DebugVarBinder{es, env}.visit(parsed);
    } else {
        VarBinder{es, env}.visit(parsed);
    }
    return parsed;
}

void VarBinder::visit(ExprLiteral & e, std::unique_ptr<Expr> & ptr)
{
}

void VarBinder::visit(ExprVar & e, std::unique_ptr<Expr> & ptr)
{
    e.fromWith = nullptr;

    /* Check whether the variable appears in the environment.  If so,
       set its level and displacement. */
    const StaticEnv * curEnv;
    Level level;
    int withLevel = -1;
    for (curEnv = env.get(), level = 0; curEnv; curEnv = curEnv->up, level++) {
        if (curEnv->isWith) {
            if (withLevel == -1) withLevel = level;
        } else {
            auto i = curEnv->vars.find(e.name);
            if (i != curEnv->vars.end()) {
                if (e.needsRoot && !curEnv->isRoot) {
                    throw ParseError({
                        .msg = HintFmt(
                            "Shadowing symbol '%s' used in internal expressions is not allowed. Use %s to disable this error.",
                            es.symbols[e.name],
                            "--extra-deprecated-features shadow-internal-symbols"
                        ),
                        .pos = es.positions[e.pos]
                    });
                }

                e.level = level;
                e.displ = i->second;
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
            es.symbols[e.name]
        ).atPos(e.pos).throw_();
    for (auto * se = env.get(); se && !e.fromWith; se = se->up)
        e.fromWith = se->isWith;
    e.level = withLevel;
}

void VarBinder::visit(ExprInheritFrom & e, std::unique_ptr<Expr> & ptr)
{
}

void VarBinder::visit(ExprSelect & e, std::unique_ptr<Expr> & ptr)
{
    visit(e.e);
    if (e.def) visit(e.def);
    for (auto & i : e.attrPath)
        if (!i.symbol)
            visit(i.expr);
}

void VarBinder::visit(ExprOpHasAttr & e, std::unique_ptr<Expr> & ptr)
{
    visit(e.e);
    for (auto & i : e.attrPath)
        if (!i.symbol)
            visit(i.expr);
}

std::shared_ptr<const StaticEnv> ExprAttrs::buildRecursiveEnv(const std::shared_ptr<const StaticEnv> & env)
{
    auto newEnv = std::make_shared<StaticEnv>(nullptr, env.get(), attrs.size());

    // safety: the attrs is already sorted
    newEnv->vars.unsafe_insert_bulk([&] (auto & map) {
        Displacement displ = 0;
        for (auto & i : attrs)
            map.emplace_back(i.first, i.second.displ = displ++);
    });
    return newEnv;
}

std::shared_ptr<const StaticEnv> ExprAttrs::bindInheritSources(ExprVisitor & e, const StaticEnv & env)
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
    auto inner = std::make_shared<StaticEnv>(nullptr, &env, 0);
    for (auto & from : *inheritFromExprs)
        e.visit(from);

    return inner;
}

void VarBinder::visit(ExprSet & e, std::unique_ptr<Expr> & ptr)
{
    auto innerEnv = e.recursive ? e.buildRecursiveEnv(env) : env;
    auto inheritFromEnv = withEnv(innerEnv, [&] { return e.bindInheritSources(*this, *innerEnv); });

    // No need to sort newEnv since attrs is in sorted order.

    for (auto & i : e.attrs) {
        withEnv(i.second.chooseByKind(innerEnv, env, inheritFromEnv), [&] {
            visit(i.second.e);
        });
    }

    withEnv(innerEnv, [&] {
        for (auto & i : e.dynamicAttrs) {
            visit(i.nameExpr);
            visit(i.valueExpr);
        }
    });
}

void VarBinder::visit(ExprList & e, std::unique_ptr<Expr> & ptr)
{
    for (auto & i : e.elems)
        visit(i);
}

void VarBinder::visit(ExprLambda & e, std::unique_ptr<Expr> & ptr)
{
    withEnv(e.pattern->buildEnv(env.get()), [&] {
        e.pattern->accept(*this);
        visit(e.body);
    });
}

void VarBinder::visit(ExprCall & e, std::unique_ptr<Expr> & ptr)
{
    visit(e.fun);
    for (auto & se : e.args)
        visit(se);
}

void VarBinder::visit(ExprLet & e, std::unique_ptr<Expr> & ptr)
{
    auto newEnv = e.buildRecursiveEnv(env);

    // No need to sort newEnv since attrs is in sorted order.

    auto inheritFromEnv = withEnv(newEnv, [&] { return e.bindInheritSources(*this, *newEnv); });
    for (auto & i : e.attrs) {
        withEnv(i.second.chooseByKind(newEnv, env, inheritFromEnv), [&] {
            visit(i.second.e);
        });
    }

    withEnv(std::move(newEnv), [&] { visit(e.body); });
}

void VarBinder::visit(ExprWith & e, std::unique_ptr<Expr> & ptr)
{
    e.parentWith = nullptr;
    for (auto * se = env.get(); se && !e.parentWith; se = se->up)
        e.parentWith = se->isWith;

    /* Does this `with' have an enclosing `with'?  If so, record its
       level so that `lookupVar' can look up variables in the previous
       `with' if this one doesn't contain the desired attribute. */
    const StaticEnv * curEnv;
    Level level;
    e.prevWith = 0;
    for (curEnv = env.get(), level = 1; curEnv; curEnv = curEnv->up, level++)
        if (curEnv->isWith) {
            e.prevWith = level;
            break;
        }

    visit(e.attrs);
    withEnv(std::make_shared<StaticEnv>(&e, env.get()), [&] { visit(e.body); });
}

void VarBinder::visit(ExprIf & e, std::unique_ptr<Expr> & ptr)
{
    visit(e.cond);
    visit(e.then);
    visit(e.else_);
}

void VarBinder::visit(ExprAssert & e, std::unique_ptr<Expr> & ptr)
{
    visit(e.cond);
    visit(e.body);
}

void VarBinder::visit(ExprOpNot & e, std::unique_ptr<Expr> & ptr)
{
    visit(e.e);
}

void VarBinder::visit(ExprConcatStrings & e, std::unique_ptr<Expr> & ptr)
{
    for (auto & i : e.es)
        visit(i.second);
}

void VarBinder::visit(ExprPos & e, std::unique_ptr<Expr> & ptr)
{
}

/* Function argument destructuring */

std::shared_ptr<const StaticEnv> SimplePattern::buildEnv(const StaticEnv * up)
{
    auto newEnv = std::make_shared<StaticEnv>(nullptr, up, 1);
    newEnv->vars.insert_or_assign(name, 0);
    return newEnv;
}

void SimplePattern::accept(ExprVisitor & ev) { }

std::shared_ptr<const StaticEnv> AttrsPattern::buildEnv(const StaticEnv * up)
{
    auto newEnv = std::make_shared<StaticEnv>(
        nullptr, up,
        formals.size() + (name ? 1 : 0)
    );

    Displacement displ = 0;

    if (name) newEnv->vars.insert_or_assign(name, displ++);

    // safety: The formals are already sorted
    newEnv->vars.unsafe_insert_bulk([&] (auto & map) {
        for (auto & i : formals)
            map.emplace_back(i.name, displ++);
    });

    return newEnv;
}

void AttrsPattern::accept(ExprVisitor & ev)
{
    for (auto & i : formals)
        if (i.def) ev.visit(i.def);
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
    dump([&](const std::string_view s) { n += s.size(); });
    return n;
}

}
