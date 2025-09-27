#pragma once
///@file

#include <map>
#include <memory>
#include <vector>

#include "lix/libexpr/value.hh"
#include "lix/libexpr/symbol-table.hh"
#include "lix/libutil/json.hh"
#include "lix/libexpr/eval-error.hh"
#include "lix/libexpr/pos-idx.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/linear-map.hh"

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

struct ExprDebugFrame;
struct ExprLiteral;
struct ExprString;
struct ExprPath;
struct ExprVar;
struct ExprInheritFrom;
struct ExprSelect;
struct ExprOpHasAttr;
struct ExprSet;
struct ExprList;
struct ExprLambda;
struct ExprCall;
struct ExprLet;
struct ExprWith;
struct ExprIf;
struct ExprAssert;
struct ExprOpNot;
struct ExprOpEq;
struct ExprOpNEq;
struct ExprOpAnd;
struct ExprOpOr;
struct ExprOpImpl;
struct ExprOpUpdate;
struct ExprOpConcatLists;
struct ExprConcatStrings;
struct ExprPos;
struct ExprBlackHole;

struct ExprVisitor
{
    virtual void visit(ExprDebugFrame & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprLiteral & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprVar & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprInheritFrom & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprSelect & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprOpHasAttr & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprSet & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprList & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprLambda & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprCall & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprLet & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprWith & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprIf & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprAssert & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprOpNot & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprOpEq & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprOpNEq & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprOpAnd & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprOpOr & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprOpImpl & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprOpUpdate & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprOpConcatLists & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprConcatStrings & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprPos & e, std::unique_ptr<Expr> & ptr) = 0;
    virtual void visit(ExprBlackHole & e, std::unique_ptr<Expr> & ptr) = 0;

    void visit(std::unique_ptr<Expr> & ptr);
};

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

    static std::unique_ptr<Expr> finalize(
        std::unique_ptr<Expr> parsed, Evaluator & es, const std::shared_ptr<const StaticEnv> & env
    );

    virtual JSON toJSON(const SymbolTable & symbols) const;
    virtual void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) = 0;
    virtual void eval(EvalState & state, Env & env, Value & v);
    virtual Value maybeThunk(EvalState & state, Env & env);
    virtual void setName(Symbol name);
    PosIdx getPos() const { return pos; }

    template<typename E>
    E & cast()
    {
        return dynamic_cast<E &>(*this);
    }

    template<typename E>
    E * try_cast()
    {
        return dynamic_cast<E *>(this);
    }
};

inline void ExprVisitor::visit(std::unique_ptr<Expr> & ptr)
{
    ptr->accept(*this, ptr);
}

struct ExprDebugFrame : Expr
{
    std::unique_ptr<Expr> inner;
    std::string message;

    ExprDebugFrame(PosIdx pos, std::unique_ptr<Expr> inner, std::string message)
        : Expr(pos)
        , inner(std::move(inner))
        , message(std::move(message))
    {
    }

    JSON toJSON(const SymbolTable & symbols) const override
    {
        return inner->toJSON(symbols);
    }
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprLiteral : Expr
{
protected:
    Value v;
    ExprLiteral(const PosIdx pos) : Expr(pos) {};
public:
    Value maybeThunk(EvalState & state, Env & env) override;
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprInt : ExprLiteral
{
    Value::Int i;
    ExprInt(const PosIdx pos, NixInt n) : ExprLiteral(pos), i{{Value::Acb::tInt}, n}
    {
        v = Value::isTaggableInteger(n) ? Value{NewValueAs::integer, n} : Value(i);
    }
    ExprInt(const PosIdx pos, NixInt::Inner n) : ExprInt(pos, NixInt(n)) {}
};

struct ExprFloat : ExprLiteral
{
    Value::Float f;
    ExprFloat(const PosIdx pos, NewValueAs::floating_t, double f)
        : ExprLiteral(pos)
        , f{{Value::Acb::tFloat}, f}
    {
        v = Value(this->f);
    }
};

struct ExprString : ExprLiteral
{
    std::string s;
    Value::String strcb{.content = s.c_str(), .context = nullptr};
    ExprString(const PosIdx pos, std::string && s) : ExprLiteral(pos), s(std::move(s))
    {
        v = {NewValueAs::string, &strcb};
    }
};

struct ExprPath : ExprLiteral
{
    std::string s;
    Value::String strcb{.content = s.c_str(), .context = Value::String::path};
    ExprPath(const PosIdx pos, std::string s) : ExprLiteral(pos), s(std::move(s))
    {
        v = {NewValueAs::path, &strcb};
    }
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
    Value maybeThunk(EvalState & state, Env & env) override;
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

/**
 * A pseudo-expression for the purpose of evaluating the `from` expression in `inherit (from)` syntax.
 * Unlike normal variable references, the displacement is set during parsing, and always refers to
 * `ExprAttrs::inheritFromExprs` (by itself or in `ExprLet`), whose values are put into their own `Env`.
 */
struct ExprInheritFrom : Expr
{
    Expr & fromExpr;
    Displacement displ;

    ExprInheritFrom(PosIdx pos, Displacement displ, Expr & fromExpr)
          : Expr(pos), fromExpr(fromExpr), displ(displ)
    {
    }

    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
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
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprOpHasAttr : Expr
{
    std::unique_ptr<Expr> e;
    AttrPath attrPath;
    ExprOpHasAttr(const PosIdx & pos, std::unique_ptr<Expr> e, AttrPath attrPath) : Expr(pos), e(std::move(e)), attrPath(std::move(attrPath)) { };
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
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
        T chooseByKind(const T & plain, const T & inherited, const T & inheritedFrom) const
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
    std::unique_ptr<std::list<std::unique_ptr<Expr>>> inheritFromExprs;
    struct DynamicAttrDef {
        std::unique_ptr<Expr> nameExpr, valueExpr;
        PosIdx pos;
        DynamicAttrDef(std::unique_ptr<Expr> nameExpr, std::unique_ptr<Expr> valueExpr, const PosIdx & pos)
            : nameExpr(std::move(nameExpr)), valueExpr(std::move(valueExpr)), pos(pos) { };
    };
    typedef std::vector<DynamicAttrDef> DynamicAttrDefs;
    DynamicAttrDefs dynamicAttrs;

    std::shared_ptr<const StaticEnv> buildRecursiveEnv(const std::shared_ptr<const StaticEnv> & env);
    std::shared_ptr<const StaticEnv> bindInheritSources(ExprVisitor & e, const StaticEnv & env);
    Env * buildInheritFromEnv(EvalState & state, Env & up);
    void addBindingsToJSON(JSON & out, const SymbolTable & symbols) const;
};

struct ExprSet : Expr, ExprAttrs {
    bool recursive = false;

    ExprSet(const PosIdx &pos, bool recursive = false) : Expr(pos), recursive(recursive) { };
    ExprSet() { };
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprReplBindings {
    std::map<Symbol, std::unique_ptr<Expr>> symbols;

    void finalize(Evaluator & es, const std::shared_ptr<const StaticEnv> & env) {
        for (auto & [_, e] : symbols)
            e = Expr::finalize(std::move(e), es, env);
    }
};

struct ExprList : Expr
{
    std::vector<std::unique_ptr<Expr>> elems;
    ExprList(PosIdx pos) : Expr(pos) { };
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
    Value maybeThunk(EvalState & state, Env & env) override;
};

struct Pattern {
    /** The argument name of this particular lambda. Is a falsey symbol if there
     * is no such argument. */
    Symbol name;

    Pattern() = default;
    explicit Pattern(Symbol name) : name(name) { }
    virtual ~Pattern() = default;

    virtual std::shared_ptr<const StaticEnv> buildEnv(const StaticEnv * up) = 0;
    virtual void accept(ExprVisitor & ev) = 0;
    virtual Env &
    match(ExprLambda & lambda, EvalState & state, Env & up, Value & arg, const PosIdx pos) = 0;

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
    virtual void accept(ExprVisitor & ev) override;
    virtual Env &
    match(ExprLambda & lambda, EvalState & state, Env & up, Value & arg, const PosIdx pos) override;

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
    virtual void accept(ExprVisitor & ev) override;
    virtual Env &
    match(ExprLambda & lambda, EvalState & state, Env & up, Value & arg, const PosIdx pos) override;

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
    inline std::string_view getName(SymbolTable const & symbols) const
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

    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprCall : Expr
{
    std::unique_ptr<Expr> fun;
    std::vector<std::unique_ptr<Expr>> args;
    ExprCall(const PosIdx & pos, std::unique_ptr<Expr> fun, std::vector<std::unique_ptr<Expr>> && args)
        : Expr(pos), fun(std::move(fun)), args(std::move(args))
    { }
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprLet : Expr, ExprAttrs
{
    std::unique_ptr<Expr> body;
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprWith : Expr
{
    std::unique_ptr<Expr> attrs, body;
    size_t prevWith;
    ExprWith * parentWith;
    ExprWith(const PosIdx & pos, std::unique_ptr<Expr> attrs, std::unique_ptr<Expr> body) : Expr(pos), attrs(std::move(attrs)), body(std::move(body)) { };
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprIf : Expr
{
    std::unique_ptr<Expr> cond, then, else_;
    ExprIf(const PosIdx & pos, std::unique_ptr<Expr> cond, std::unique_ptr<Expr> then, std::unique_ptr<Expr> else_) : Expr(pos), cond(std::move(cond)), then(std::move(then)), else_(std::move(else_)) { };
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprAssert : Expr
{
    std::unique_ptr<Expr> cond, body;
    ExprAssert(const PosIdx & pos, std::unique_ptr<Expr> cond, std::unique_ptr<Expr> body) : Expr(pos), cond(std::move(cond)), body(std::move(body)) { };
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprOpNot : Expr
{
    std::unique_ptr<Expr> e;
    ExprOpNot(const PosIdx & pos, std::unique_ptr<Expr> e) : Expr(pos), e(std::move(e)) { };
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
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
        void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); } \
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
    bool isInterpolation;
    std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>> es;
    ExprConcatStrings(const PosIdx & pos, bool isInterpolation, std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>> es)
        : Expr(pos), isInterpolation(isInterpolation), es(std::move(es)) { };
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

struct ExprPos : Expr
{
    ExprPos(const PosIdx & pos) : Expr(pos) { };
    JSON toJSON(const SymbolTable & symbols) const override;
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

/* only used to mark thunks as black holes. */
struct ExprBlackHole : Expr
{
    void eval(EvalState & state, Env & env, Value & v) override;
    void accept(ExprVisitor & ev, std::unique_ptr<Expr> & ptr) override { ev.visit(*this, ptr); }
};

extern ExprBlackHole eBlackHole;


/* Static environments are used to map variable names onto (level,
   displacement) pairs used to obtain the value of the variable at
   runtime. */
struct StaticEnv
{
    ExprWith * isWith;
    const StaticEnv * up;

    LinearMap<Symbol, Displacement> vars;

    /* See ExprVar::needsRoot */
    bool isRoot = false;

    StaticEnv(ExprWith * isWith, const StaticEnv * up, size_t expectedSize = 0) : isWith(isWith), up(up) {
        vars.reserve(expectedSize);
    };
};


}
