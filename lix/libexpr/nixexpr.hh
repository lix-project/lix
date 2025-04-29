#pragma once
///@file

#include <map>
#include <vector>

#include "lix/libexpr/value.hh"
#include "lix/libexpr/symbol-table.hh"
#include "lix/libutil/json.hh"
#include "lix/libexpr/eval-error.hh"
#include "lix/libexpr/pos-idx.hh"
#include "lix/libutil/strings.hh"

namespace nix {

struct Env;
struct Value;
class Evaluator;
struct ExprWith;
struct StaticEnv;


/**
 * An attribute path is a sequence of attribute names.
 */
struct AttrName
{
    PosIdx pos;
    Symbol symbol;
    std::unique_ptr<Expr> expr;
    AttrName(PosIdx pos, Symbol s);
    AttrName(PosIdx pos, std::unique_ptr<Expr> e);
};

typedef std::vector<AttrName> AttrPath;

std::string showAttrPath(const SymbolTable & symbols, const AttrPath & attrPath);
JSON printAttrPathToJson(const SymbolTable & symbols, const AttrPath & attrPath);


/* Abstract syntax of Nix expressions. */

struct Expr
{
protected:
    Expr(Expr &&) = default;
    Expr & operator=(Expr &&) = default;
    Expr(const PosIdx pos) : pos(pos) {};

public:
    struct AstSymbols {
        Symbol sub, lessThan, mul, div, or_, findFile, nixPath, body, overrides;
    };

    PosIdx pos;

    Expr() = default;
    Expr(const Expr &) = delete;
    Expr & operator=(const Expr &) = delete;
    virtual ~Expr() { };

    virtual JSON toJSON(const SymbolTable & symbols) const;
    virtual void bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env);
    virtual void eval(EvalState & state, Env & env, Value & v);
    virtual Value * maybeThunk(EvalState & state, Env & env);
    virtual void setName(Symbol name);
    PosIdx getPos() const { return pos; }
};

#define COMMON_METHODS \
    JSON toJSON(const SymbolTable & symbols) const override; \
    void eval(EvalState & state, Env & env, Value & v) override; \
    void bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env) override;

struct ExprLiteral : Expr
{
protected:
    Value v;
    ExprLiteral(const PosIdx pos) : Expr(pos) {};
public:

    ExprLiteral(const PosIdx pos, NewValueAs::integer_t, NixInt n) : Expr(pos) { v.mkInt(n); };
    ExprLiteral(const PosIdx pos, NewValueAs::integer_t, NixInt::Inner n) : Expr(pos) { v.mkInt(n); };
    ExprLiteral(const PosIdx pos, NewValueAs::floating_t, NixFloat nf) : Expr(pos) { v.mkFloat(nf); };
    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprString : ExprLiteral
{
    std::string s;
    ExprString(const PosIdx pos, std::string &&s) : ExprLiteral(pos), s(std::move(s)) { v.mkString(this->s.data()); };
};

struct ExprPath : ExprLiteral
{
    std::string s;
    ExprPath(const PosIdx pos, std::string s) : ExprLiteral(pos), s(std::move(s)) { v.mkPath(this->s.c_str()); };
};

typedef uint32_t Level;
typedef uint32_t Displacement;

struct ExprVar : Expr
{
    Symbol name;

    /* Whether the variable comes from an environment (e.g. a rec, let
       or function argument) or from a "with".

       `nullptr`: Not from a `with`.
       Valid pointer: the nearest, innermost `with` expression to query first. */
    ExprWith * fromWith;

    /* In the former case, the value is obtained by going `level`
       levels up from the current environment and getting the
       `displ`th value in that environment.  In the latter case, the
       value is obtained by getting the attribute named `name` from
       the set stored in the environment that is `level` levels up
       from the current one.*/
    Level level;
    Displacement displ;

    /* Variables like `__sub` as generated from expressions like `5 - 3` shouldn't be overridden.
     * This is implemented by having a blessed "root" env that contains the definitions (usually `builtins`)
     * and checking that this var only binds against that env.
     */
    bool needsRoot;

    ExprVar(Symbol name) : name(name), needsRoot(false) { };
    ExprVar(const PosIdx & pos, Symbol name, bool needsRoot = false) : Expr(pos), name(name), needsRoot(needsRoot) { };
    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

/**
 * A pseudo-expression for the purpose of evaluating the `from` expression in `inherit (from)` syntax.
 * Unlike normal variable references, the displacement is set during parsing, and always refers to
 * `ExprAttrs::inheritFromExprs` (by itself or in `ExprLet`), whose values are put into their own `Env`.
 */
struct ExprInheritFrom : Expr
{
    ref<Expr> fromExpr;
    Displacement displ;

    ExprInheritFrom(PosIdx pos, Displacement displ, ref<Expr> fromExpr)
          : Expr(pos), fromExpr(fromExpr), displ(displ)
    {
    }

    COMMON_METHODS
};

struct ExprSelect : Expr
{
    /** The expression attributes are being selected on. e.g. `foo` in `foo.bar.baz`. */
    std::unique_ptr<Expr> e;

    /** A default value specified with `or`, if the selected attr doesn't exist.
     * e.g. `bix` in `foo.bar.baz or bix`
     */
    std::unique_ptr<Expr> def;

    /** The path of attributes being selected. e.g. `bar.baz` in `foo.bar.baz.` */
    AttrPath attrPath;

    ExprSelect(const PosIdx & pos, std::unique_ptr<Expr> e, AttrPath attrPath, std::unique_ptr<Expr> def) : Expr(pos), e(std::move(e)), def(std::move(def)), attrPath(std::move(attrPath)) { };
    ExprSelect(const PosIdx & pos, std::unique_ptr<Expr> e, const PosIdx namePos, Symbol name) : Expr(pos), e(std::move(e)) { attrPath.push_back(AttrName(namePos, name)); };
    COMMON_METHODS
};

struct ExprOpHasAttr : Expr
{
    std::unique_ptr<Expr> e;
    AttrPath attrPath;
    ExprOpHasAttr(const PosIdx & pos, std::unique_ptr<Expr> e, AttrPath attrPath) : Expr(pos), e(std::move(e)), attrPath(std::move(attrPath)) { };
    COMMON_METHODS
};

/* Helper struct to contain the data shared across lets and sets */
struct ExprAttrs
{
    ExprAttrs() = default;
    ExprAttrs(const ExprAttrs &) = delete;
    ExprAttrs & operator=(const ExprAttrs &) = delete;
    ExprAttrs(ExprAttrs &&) = default;
    ExprAttrs & operator=(ExprAttrs &&) = default;
    virtual ~ExprAttrs() = default;

    struct AttrDef {
        enum class Kind {
            /** `attr = expr;` */
            Plain,
            /** `inherit attr1 attrn;` */
            Inherited,
            /** `inherit (expr) attr1 attrn;` */
            InheritedFrom,
        };

        Kind kind;
        std::unique_ptr<Expr> e;
        PosIdx pos;
        Displacement displ; // displacement
        AttrDef(std::unique_ptr<Expr> e, const PosIdx & pos, Kind kind = Kind::Plain)
            : kind(kind), e(std::move(e)), pos(pos) { };
        AttrDef() { };

        template<typename T>
        const T & chooseByKind(const T & plain, const T & inherited, const T & inheritedFrom) const
        {
            switch (kind) {
            case Kind::Plain:
                return plain;
            case Kind::Inherited:
                return inherited;
            default:
            case Kind::InheritedFrom:
                return inheritedFrom;
            }
        }
    };
    typedef std::map<Symbol, AttrDef> AttrDefs;
    AttrDefs attrs;
    std::unique_ptr<std::vector<ref<Expr>>> inheritFromExprs;
    struct DynamicAttrDef {
        std::unique_ptr<Expr> nameExpr, valueExpr;
        PosIdx pos;
        DynamicAttrDef(std::unique_ptr<Expr> nameExpr, std::unique_ptr<Expr> valueExpr, const PosIdx & pos)
            : nameExpr(std::move(nameExpr)), valueExpr(std::move(valueExpr)), pos(pos) { };
    };
    typedef std::vector<DynamicAttrDef> DynamicAttrDefs;
    DynamicAttrDefs dynamicAttrs;

    std::shared_ptr<const StaticEnv> bindInheritSources(
        Evaluator & es, const std::shared_ptr<const StaticEnv> & env);
    Env * buildInheritFromEnv(EvalState & state, Env & up);
    void addBindingsToJSON(JSON & out, const SymbolTable & symbols) const;
};

struct ExprSet : Expr, ExprAttrs {
    bool recursive = false;

    ExprSet(const PosIdx &pos, bool recursive = false) : Expr(pos), recursive(recursive) { };
    ExprSet() { };
    COMMON_METHODS
};

struct ExprReplBindings {
    std::map<Symbol, std::unique_ptr<Expr>> symbols;

    void bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env) {
        for (auto & [_, e] : symbols)
            e->bindVars(es, env);
    }
};

struct ExprList : Expr
{
    std::vector<std::unique_ptr<Expr>> elems;
    ExprList(PosIdx pos) : Expr(pos) { };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env) override;
};

struct Pattern {
    /** The argument name of this particular lambda. Is a falsey symbol if there
     * is no such argument. */
    Symbol name;

    Pattern() = default;
    explicit Pattern(Symbol name) : name(name) { }
    virtual ~Pattern() = default;

    virtual std::shared_ptr<const StaticEnv> buildEnv(const StaticEnv * up) = 0;
    virtual void bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env) = 0;
    virtual Env & match(ExprLambda & lambda, EvalState & state, Env & up, Value * arg, const PosIdx pos) = 0;

    virtual void addBindingsToJSON(JSON & out, const SymbolTable & symbols) const = 0;
};

/** A plain old lambda */
struct SimplePattern : Pattern
{
    SimplePattern() = default;
    SimplePattern(Symbol name) : Pattern(name) {
        assert(name);
    }

    virtual std::shared_ptr<const StaticEnv> buildEnv(const StaticEnv * up) override;
    virtual void bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env) override;
    virtual Env & match(ExprLambda & lambda, EvalState & state, Env & up, Value * arg, const PosIdx pos) override;

    virtual void addBindingsToJSON(JSON & out, const SymbolTable & symbols) const override;
};

/** Attribute set destructuring in arguments of a lambda, if present */
struct AttrsPattern : Pattern
{
    struct Formal
    {
        PosIdx pos;
        Symbol name;
        std::unique_ptr<Expr> def;
    };

    typedef std::vector<Formal> Formals_;
    Formals_ formals;
    bool ellipsis;

    virtual std::shared_ptr<const StaticEnv> buildEnv(const StaticEnv * up) override;
    virtual void bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env) override;
    virtual Env & match(ExprLambda & lambda, EvalState & state, Env & up, Value * arg, const PosIdx pos) override;

    virtual void addBindingsToJSON(JSON & out, const SymbolTable & symbols) const override;

    bool has(Symbol arg) const
    {
        auto it = std::lower_bound(formals.begin(), formals.end(), arg,
            [] (const Formal & f, const Symbol & sym) { return f.name < sym; });
        return it != formals.end() && it->name == arg;
    }

    std::vector<std::reference_wrapper<const Formal>> lexicographicOrder(const SymbolTable & symbols) const
    {
        std::vector<std::reference_wrapper<const Formal>> result(formals.begin(), formals.end());
        std::sort(result.begin(), result.end(),
            [&] (const Formal & a, const Formal & b) {
                std::string_view sa = symbols[a.name], sb = symbols[b.name];
                return sa < sb;
            });
        return result;
    }
};

struct ExprLambda : Expr
{
    /** Name of the lambda. This is set if the lambda is defined in a
     * let-expression or an attribute set, such that there is a name.
     * Lambdas may have a falsey symbol as the name if they are anonymous */
    Symbol name;
    std::unique_ptr<Pattern> pattern;
    std::unique_ptr<Expr> body;
    ExprLambda(PosIdx pos, std::unique_ptr<Pattern> pattern, std::unique_ptr<Expr> body)
        : Expr(pos), pattern(std::move(pattern)), body(std::move(body))
    {
    }
    void setName(Symbol name) override;
    std::string showNamePos(const EvalState & state) const;

    /** Returns the name of the lambda,
     * or "anonymous lambda" if it doesn't have one.
     */
    inline std::string getName(SymbolTable const & symbols) const
    {
        if (this->name) {
            return symbols[this->name];
        }

        return "anonymous lambda";
    }

    /** Returns the name of the lambda in single quotes,
     * or "anonymous lambda" if it doesn't have one.
     */
    inline std::string getQuotedName(SymbolTable const & symbols) const
    {
        if (this->name) {
            return concatStrings("'", symbols[this->name], "'");
        }

        return "anonymous lambda";
    }

    COMMON_METHODS
};

struct ExprCall : Expr
{
    std::unique_ptr<Expr> fun;
    std::vector<std::unique_ptr<Expr>> args;
    ExprCall(const PosIdx & pos, std::unique_ptr<Expr> fun, std::vector<std::unique_ptr<Expr>> && args)
        : Expr(pos), fun(std::move(fun)), args(std::move(args))
    { }
    COMMON_METHODS
};

struct ExprLet : Expr, ExprAttrs
{
    std::unique_ptr<Expr> body;
    COMMON_METHODS
};

struct ExprWith : Expr
{
    std::unique_ptr<Expr> attrs, body;
    size_t prevWith;
    ExprWith * parentWith;
    ExprWith(const PosIdx & pos, std::unique_ptr<Expr> attrs, std::unique_ptr<Expr> body) : Expr(pos), attrs(std::move(attrs)), body(std::move(body)) { };
    COMMON_METHODS
};

struct ExprIf : Expr
{
    std::unique_ptr<Expr> cond, then, else_;
    ExprIf(const PosIdx & pos, std::unique_ptr<Expr> cond, std::unique_ptr<Expr> then, std::unique_ptr<Expr> else_) : Expr(pos), cond(std::move(cond)), then(std::move(then)), else_(std::move(else_)) { };
    COMMON_METHODS
};

struct ExprAssert : Expr
{
    std::unique_ptr<Expr> cond, body;
    ExprAssert(const PosIdx & pos, std::unique_ptr<Expr> cond, std::unique_ptr<Expr> body) : Expr(pos), cond(std::move(cond)), body(std::move(body)) { };
    COMMON_METHODS
};

struct ExprOpNot : Expr
{
    std::unique_ptr<Expr> e;
    ExprOpNot(const PosIdx & pos, std::unique_ptr<Expr> e) : Expr(pos), e(std::move(e)) { };
    COMMON_METHODS
};

#define MakeBinOp(name, s) \
    struct name : Expr \
    { \
        std::unique_ptr<Expr> e1, e2; \
        name(std::unique_ptr<Expr> e1, std::unique_ptr<Expr> e2) : e1(std::move(e1)), e2(std::move(e2)) { }; \
        name(const PosIdx & pos, std::unique_ptr<Expr> e1, std::unique_ptr<Expr> e2) : Expr(pos), e1(std::move(e1)), e2(std::move(e2)) { }; \
        JSON toJSON(const SymbolTable & symbols) const override \
        { \
            return { \
                {"_type", #name}, \
                {"e1", e1->toJSON(symbols)}, \
                {"e2", e2->toJSON(symbols)} \
            };\
        } \
        void bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env) override \
        { \
            e1->bindVars(es, env); e2->bindVars(es, env);    \
        } \
        void eval(EvalState & state, Env & env, Value & v) override; \
    };

MakeBinOp(ExprOpEq, "==")
MakeBinOp(ExprOpNEq, "!=")
MakeBinOp(ExprOpAnd, "&&")
MakeBinOp(ExprOpOr, "||")
MakeBinOp(ExprOpImpl, "->")
MakeBinOp(ExprOpUpdate, "//")
MakeBinOp(ExprOpConcatLists, "++")

struct ExprConcatStrings : Expr
{
    bool forceString;
    std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>> es;
    ExprConcatStrings(const PosIdx & pos, bool forceString, std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>> es)
        : Expr(pos), forceString(forceString), es(std::move(es)) { };
    COMMON_METHODS
};

struct ExprPos : Expr
{
    ExprPos(const PosIdx & pos) : Expr(pos) { };
    COMMON_METHODS
};

/* only used to mark thunks as black holes. */
struct ExprBlackHole : Expr
{
    void eval(EvalState & state, Env & env, Value & v) override;
    void bindVars(Evaluator & es, const std::shared_ptr<const StaticEnv> & env) override {}
};

extern ExprBlackHole eBlackHole;


/* Static environments are used to map variable names onto (level,
   displacement) pairs used to obtain the value of the variable at
   runtime. */
struct StaticEnv
{
    ExprWith * isWith;
    const StaticEnv * up;

    // Note: these must be in sorted order.
    typedef std::vector<std::pair<Symbol, Displacement>> Vars;
    Vars vars;

    /* See ExprVar::needsRoot */
    bool isRoot = false;

    StaticEnv(ExprWith * isWith, const StaticEnv * up, size_t expectedSize = 0) : isWith(isWith), up(up) {
        vars.reserve(expectedSize);
    };

    void sort()
    {
        std::stable_sort(vars.begin(), vars.end(),
            [](const Vars::value_type & a, const Vars::value_type & b) { return a.first < b.first; });
    }

    void deduplicate()
    {
        auto it = vars.begin(), jt = it, end = vars.end();
        while (jt != end) {
            *it = *jt++;
            while (jt != end && it->first == jt->first) *it = *jt++;
            it++;
        }
        vars.erase(it, end);
    }

    Vars::const_iterator find(Symbol name) const
    {
        Vars::value_type key(name, 0);
        auto i = std::lower_bound(vars.begin(), vars.end(), key);
        if (i != vars.end() && i->first == name) return i;
        return vars.end();
    }
};


}
