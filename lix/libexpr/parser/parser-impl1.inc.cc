#include "eval-settings.hh"
#include "lix/libexpr/nixexpr.hh"
#include "lix/libexpr/parser/change_head.hh"
#include "lix/libexpr/parser/grammar.hh"
#include "lix/libexpr/parser/state.hh"
#include "lix/libexpr/pos-idx.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/users.hh"

#include <charconv>

// flip this define when doing parser development to enable some g checks.
#if 0
#include <tao/pegtl/contrib/analyze.hpp>
#define ANALYZE_GRAMMAR \
    ([] { \
        const std::size_t issues = tao::pegtl::analyze<grammar::v1::root>(); \
        assert(issues == 0); \
    })()
#else
#define ANALYZE_GRAMMAR ((void) 0)
#endif

namespace p = tao::pegtl;

namespace nix::parser::v1 {
namespace {

template<typename>
inline constexpr const char * error_message = nullptr;

#define error_message_for(...) \
    template<> inline constexpr auto error_message<__VA_ARGS__>

error_message_for(p::one<'{'>) = "expecting '{'";
error_message_for(p::one<'}'>) = "expecting '}'";
error_message_for(p::one<'"'>) = "expecting '\"'";
error_message_for(p::one<';'>) = "expecting ';'";
error_message_for(p::one<')'>) = "expecting ')'";
error_message_for(p::one<']'>) = "expecting ']'";
error_message_for(p::one<':'>) = "expecting ':'";
error_message_for(p::string<'\'', '\''>) = "expecting \"''\"";
error_message_for(p::any) = "expecting any character";
error_message_for(grammar::v1::eof) = "expecting end of file";
error_message_for(grammar::v1::seps) = "expecting separators";
error_message_for(grammar::v1::path::forbid_prefix_triple_slash) = "too many slashes in path";
error_message_for(grammar::v1::path::forbid_prefix_double_slash_no_interp) = "path has a trailing slash";
error_message_for(grammar::v1::expr) = "expecting expression";
error_message_for(grammar::v1::repl_root::expr_or_binding) = "expecting expression or a binding";
error_message_for(grammar::v1::expr::unary) = "expecting expression";
error_message_for(grammar::v1::binding::equal) = "expecting '='";
error_message_for(grammar::v1::expr::lambda::arg) = "expecting identifier";
error_message_for(grammar::v1::formals) = "expecting formals";
error_message_for(grammar::v1::attrpath) = "expecting attribute path";
error_message_for(grammar::v1::expr::select) = "expecting selection expression";
error_message_for(grammar::v1::t::kw_then) = "expecting 'then'";
error_message_for(grammar::v1::t::kw_else) = "expecting 'else'";
error_message_for(grammar::v1::t::kw_in) = "expecting 'in'";

struct SyntaxErrors
{
    template<typename Rule>
    static constexpr auto message = error_message<Rule>;

    template<typename Rule>
    static constexpr bool raise_on_failure = false;
};

template<typename Rule>
struct Control : p::must_if<SyntaxErrors>::control<Rule>
{
    template<typename ParseInput, typename... States>
    [[noreturn]] static void raise(const ParseInput & in, States &&... st)
    {
        if (in.empty()) {
            std::string expected;
            if constexpr (constexpr auto msg = error_message<Rule>)
                expected = fmt(", %s", msg);
            throw p::parse_error("unexpected end of file" + expected, in);
        }
        p::must_if<SyntaxErrors>::control<Rule>::raise(in, st...);
    }
};

struct ExprState
    : grammar::v1::
          operator_semantics<ExprState, PosIdx, AttrPath, std::pair<PosIdx, std::unique_ptr<Expr>>>
{
    std::unique_ptr<Expr> popExprOnly() {
        return std::move(popExpr().second);
    }

    template<typename Op, typename... Args>
    std::unique_ptr<Expr> applyUnary(PosIdx pos, Args &&... args) {
        return std::make_unique<Op>(pos, popExprOnly(), std::forward<Args>(args)...);
    }

    template<typename Op>
    std::unique_ptr<Expr> applyBinary(PosIdx pos) {
        auto right = popExprOnly(), left = popExprOnly();
        return std::make_unique<Op>(pos, std::move(left), std::move(right));
    }

    std::unique_ptr<Expr> call(PosIdx pos, State & state, Symbol fn, bool flip = false)
    {
        std::vector<std::unique_ptr<Expr>> args(2);
        args[flip ? 0 : 1] = popExprOnly();
        args[flip ? 1 : 0] = popExprOnly();
        return std::make_unique<ExprCall>(pos, state.mkInternalVar(pos, fn), std::move(args));
    }

    std::unique_ptr<Expr> pipe(PosIdx pos, State & state, bool flip = false)
    {
        if (!state.featureSettings.isEnabled(Xp::PipeOperator))
            throw ParseError({
                .msg = HintFmt("Pipe operator is disabled"),
                .pos = state.positions[pos]
            });

        // Reverse the order compared to normal function application: arg |> fn
        std::unique_ptr<Expr> fn, arg;
        if (flip) {
            fn = popExprOnly();
            arg = popExprOnly();
        } else {
            arg = popExprOnly();
            fn = popExprOnly();
        }
        std::vector<std::unique_ptr<Expr>> args{1};
        args[0] = std::move(arg);

        return std::make_unique<ExprCall>(pos, std::move(fn), std::move(args));
    }

    std::unique_ptr<Expr> order(PosIdx pos, bool less, State & state)
    {
        return call(pos, state, state.s.lessThan, !less);
    }

    std::unique_ptr<Expr> concatStrings(PosIdx pos)
    {
        std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>> args(2);
        args[1] = popExpr();
        args[0] = popExpr();
        return std::make_unique<ExprConcatStrings>(pos, false, std::move(args));
    }

    std::unique_ptr<Expr> negate(PosIdx pos, State & state)
    {
        std::vector<std::unique_ptr<Expr>> args(2);
        args[0] = std::make_unique<ExprInt>(pos, 0);
        args[1] = popExprOnly();
        return std::make_unique<ExprCall>(pos, state.mkInternalVar(pos, state.s.sub), std::move(args));
    }

    void applyOp(PosIdx pos, auto & op, State & state) {
        using Op = grammar::v1::op;

        auto not_ = [&] (auto e) {
            return std::make_unique<ExprOpNot>(pos, std::move(e));
        };

        auto expr = (overloaded {
            [&] (Op::implies)     { return applyBinary<ExprOpImpl>(pos); },
            [&] (Op::or_)         { return applyBinary<ExprOpOr>(pos); },
            [&] (Op::and_)        { return applyBinary<ExprOpAnd>(pos); },
            [&] (Op::equals)      { return applyBinary<ExprOpEq>(pos); },
            [&] (Op::not_equals)  { return applyBinary<ExprOpNEq>(pos); },
            [&] (Op::less)        { return order(pos, true, state); },
            [&] (Op::greater_eq)  { return not_(order(pos, true, state)); },
            [&] (Op::greater)     { return order(pos, false, state); },
            [&] (Op::less_eq)     { return not_(order(pos, false, state)); },
            [&] (Op::update)      { return applyBinary<ExprOpUpdate>(pos); },
            [&] (Op::not_)        { return applyUnary<ExprOpNot>(pos); },
            [&] (Op::plus)        { return concatStrings(pos); },
            [&] (Op::minus)       { return call(pos, state, state.s.sub); },
            [&] (Op::mul)         { return call(pos, state, state.s.mul); },
            [&] (Op::div)         { return call(pos, state, state.s.div); },
            [&] (Op::concat)      { return applyBinary<ExprOpConcatLists>(pos); },
            [&] (has_attr & a)    { return applyUnary<ExprOpHasAttr>(pos, std::move(a.path)); },
            [&] (Op::unary_minus) { return negate(pos, state); },
            [&] (Op::pipe_right)  { return pipe(pos, state, true); },
            [&] (Op::pipe_left)   { return pipe(pos, state); },
        })(op);
        pushExpr(pos, std::move(expr));
    }

    // always_inline is needed, otherwise pushOp slows down considerably
    [[noreturn, gnu::always_inline]]
    static void badOperator(PosIdx pos, State & state)
    {
        throw ParseError({
            .msg = HintFmt("syntax error, unexpected operator"),
            .pos = state.positions[pos]
        });
    }

    template<typename ExprT, typename... Args>
    inline ExprT & emplaceExpr(PosIdx pos, Args && ... args)
    {
        auto p = std::make_unique<ExprT>(pos, std::forward<Args>(args)...);
        auto & result = *p;
        pushExpr(pos, std::move(p));
        return result;
    }

    inline Expr & pushExpr(PosIdx pos, std::unique_ptr<Expr> expr)
    {
        auto & result = *expr;
        exprs.emplace_back(pos, std::move(expr));
        return result;
    }
};

struct SubexprState {
private:
    ExprState * up;

public:
    explicit SubexprState(ExprState & up, auto &...) : up(&up) {}
    operator ExprState &() { return *up; }
    ExprState * operator->() { return up; }
};



template<typename Rule>
struct BuildAST : grammar::v1::nothing<Rule> {};

template<> struct BuildAST<grammar::v1::t::eol::deprecated_cr_crlf> {
    static void apply(const auto & in, auto &, State & ps) {
        if (!ps.featureSettings.isEnabled(Dep::CRLineEndings))
            ps.badLineEndingFound(ps.at(in), true);
    }
};

struct SimpleLambdaState : SubexprState {
    using SubexprState::SubexprState;

    SimplePattern pattern;
};

struct AttrsLambdaState : SubexprState {
    using SubexprState::SubexprState;

    AttrsPattern pattern;
};

struct FormalsState : SubexprState {
    using SubexprState::SubexprState;

    std::vector<AttrsPattern::Formal> formals;
    bool ellipsis = false;

    AttrsPattern::Formal formal {};
};

template<> struct BuildAST<grammar::v1::formal::name> {
    static void apply(const auto & in, FormalsState & s, State & ps) {
        s.formal = {
            .pos = ps.at(in),
            .name = ps.symbols.create(in.string_view()),
        };
    }
};

template<> struct BuildAST<grammar::v1::formal> {
    static void apply0(FormalsState & s, State &) {
        s.formals.emplace_back(std::move(s.formal));
    }
};

template<> struct BuildAST<grammar::v1::formal::default_value> {
    static void apply0(FormalsState & s, State & ps) {
        s.formal.def = s->popExprOnly();
    }
};

template<> struct BuildAST<grammar::v1::formals::ellipsis> {
    static void apply0(FormalsState & s, State &) {
        s.ellipsis = true;
    }
};

template<> struct BuildAST<grammar::v1::formals> : change_head<FormalsState> {
    static void success0(FormalsState & f, AttrsLambdaState & s, State &) {
        s.pattern.formals = std::move(f.formals);
        s.pattern.ellipsis = f.ellipsis;
    }
};

template<> struct BuildAST<grammar::v1::expr::lambda::arg> {
    static void apply(const auto & in, auto & s, State & ps) {
        s.pattern.name = ps.symbols.create(in.string_view());
    }
};

template<> struct BuildAST<grammar::v1::expr::lambda::pattern_simple> : change_head<SimpleLambdaState> {
    static void success(const auto & in, SimpleLambdaState & l, ExprState & s, State & ps) {
        s.emplaceExpr<ExprLambda>(ps.at(in), std::make_unique<SimplePattern>(std::move(l.pattern)), l->popExprOnly());
    }
};

template<> struct BuildAST<grammar::v1::expr::lambda::pattern_attrs> : change_head<AttrsLambdaState> {
    static void success(const auto & in, AttrsLambdaState & l, ExprState & s, State & ps) {
        ps.validateLambdaAttrs(l.pattern, ps.at(in));
        s.emplaceExpr<ExprLambda>(ps.at(in), std::make_unique<AttrsPattern>(std::move(l.pattern)), l->popExprOnly());
    }
};

struct AttrState : SubexprState {
    using SubexprState::SubexprState;

    AttrPath attrs;

    template <typename T>
    void pushAttr(T && attr, PosIdx pos) { attrs.emplace_back(pos, std::forward<T>(attr)); }
};

template<> struct BuildAST<grammar::v1::attr::simple> {
    static void apply(const auto & in, auto & s, State & ps) {
        s.pushAttr(ps.symbols.create(in.string_view()), ps.at(in));
    }
};

template<> struct BuildAST<grammar::v1::attr::string> {
    static void apply(const auto & in, auto & s, State & ps) {
        auto e = s->popExprOnly();
        if (auto str = dynamic_cast<ExprString *>(e.get()))
            s.pushAttr(ps.symbols.create(str->s), ps.at(in));
        else
            s.pushAttr(std::move(e), ps.at(in));
    }
};

template<> struct BuildAST<grammar::v1::attr::expr> : BuildAST<grammar::v1::attr::string> {};

struct BindingState : AttrState {
    using AttrState::AttrState;

    std::unique_ptr<Expr> value;
};

struct BindingsState : SubexprState {
    explicit BindingsState(ExprState & up, ExprAttrs & attrs) : SubexprState(up), attrs(attrs) {}

    ExprAttrs & attrs;
};

struct BindingsStateSet : BindingsState {
    ExprSet set = {};
    BindingsStateSet(ExprState & up, State & ps, auto &...) : BindingsState(up, set) { }
};

struct BindingsStateRecSet : BindingsState {
    ExprSet set = { PosIdx{}, true };
    BindingsStateRecSet(ExprState & up, State & ps, auto &...) : BindingsState(up, set) { }
};

struct BindingsStateLet : BindingsState {
    ExprLet let = {};
    BindingsStateLet(ExprState & up, State & ps, auto &...) : BindingsState(up, let) { }
};

struct InheritState : SubexprState {
    using SubexprState::SubexprState;

    std::vector<AttrName> attrs;
    std::unique_ptr<Expr> from;
    PosIdx fromPos;

    template <typename T>
    void pushAttr(T && attr, PosIdx pos) { attrs.emplace_back(pos, std::forward<T>(attr)); }
};

template<> struct BuildAST<grammar::v1::inherit::from> {
    static void apply(const auto & in, InheritState & s, State & ps) {
        s.from = s->popExprOnly();
        s.fromPos = ps.at(in);
    }
};

template<> struct BuildAST<grammar::v1::inherit> : change_head<InheritState> {
    static void success0(InheritState & s, BindingsState & b, State & ps) {
        auto & attrs = b.attrs.attrs;
        // TODO this should not reuse generic attrpath rules.
        for (auto & i : s.attrs) {
            if (i.symbol)
                continue;
            if (auto str = dynamic_cast<ExprString *>(i.expr.get()))
                i = AttrName(i.pos, ps.symbols.create(str->s));
            else {
                throw ParseError({
                    .msg = HintFmt("dynamic attributes not allowed in inherit"),
                    .pos = ps.positions[i.pos]
                });
            }
        }
        if (s.from != nullptr) {
            if (!b.attrs.inheritFromExprs)
                b.attrs.inheritFromExprs = std::make_unique<std::list<std::unique_ptr<Expr>>>();
            b.attrs.inheritFromExprs->push_back(std::move(s.from));
            auto & fromExpr = b.attrs.inheritFromExprs->back();
            for (auto & i : s.attrs) {
                if (attrs.find(i.symbol) != attrs.end())
                    ps.dupAttr(i.symbol, i.pos, attrs[i.symbol].pos);
                auto inheritFrom = std::make_unique<ExprInheritFrom>(
                    s.fromPos,
                    b.attrs.inheritFromExprs->size() - 1,
                    *fromExpr
                );
                attrs.emplace(
                    i.symbol,
                    ExprAttrs::AttrDef(
                        std::make_unique<ExprSelect>(i.pos, std::move(inheritFrom), i.pos, i.symbol),
                        i.pos,
                        ExprAttrs::AttrDef::Kind::InheritedFrom));
            }
        } else {
            for (auto & i : s.attrs) {
                if (attrs.find(i.symbol) != attrs.end())
                    ps.dupAttr(i.symbol, i.pos, attrs[i.symbol].pos);
                attrs.emplace(
                    i.symbol,
                    ExprAttrs::AttrDef(
                        std::make_unique<ExprVar>(i.pos, i.symbol),
                        i.pos,
                        ExprAttrs::AttrDef::Kind::Inherited));
            }
        }
    }
};

template<> struct BuildAST<grammar::v1::binding::value> {
    static void apply0(BindingState & s, State & ps) {
        s.value = s->popExprOnly();
    }
};

template<> struct BuildAST<grammar::v1::binding> : change_head<BindingState> {
    static void success(const auto & in, BindingState & b, BindingsState & s, State & ps) {
        ps.addAttr(&s.attrs, std::move(b.attrs), std::move(b.value), ps.at(in));
    }
};

struct BindingsStateRepl : ExprState {
    std::map<Symbol, std::unique_ptr<Expr>> symbols;
};

template<> struct BuildAST<grammar::v1::repl_binding> : change_head<BindingState> {
    static void success(const auto & in, BindingState & b, BindingsStateRepl & s, State & ps) {
        auto path = std::move(b.attrs);
        AttrName name = std::move(path.front());
        path.erase(path.begin());
        if (name.expr)
            throw ParseError({
                .msg = HintFmt("dynamic attributes not allowed in REPL"),
                .pos = ps.positions[ps.at(in)]
            });
        Symbol symbol = name.symbol;

        if (auto iter = s.symbols.find(symbol); iter != s.symbols.end())
            ps.dupAttr({symbol}, iter->second->getPos(), ps.at(in));

        if (path.empty()) {
            // key = value
            s.symbols.emplace(symbol, std::move(b.value));
        } else {
            // key.stuff = value
            auto attrs = std::make_unique<ExprSet>(b.value->getPos());
            ps.addAttr(&*attrs, std::move(path), std::move(b.value), ps.at(in));
            s.symbols.emplace(symbol, std::move(attrs));
        }
    }
};

using ReplRootState = std::variant<std::unique_ptr<Expr>, ExprReplBindings>;

template<> struct BuildAST<grammar::v1::repl_bindings> : change_head<BindingsStateRepl> {
    static void success0(BindingsStateRepl & b, ReplRootState & r, State &) {
        r = ExprReplBindings { std::move(b.symbols) };
    }
};

template<> struct BuildAST<grammar::v1::repl_root::expression> : change_head<ExprState> {
    static void success0(ExprState & inner, ReplRootState & outer, State & ps) {
        auto [_pos, expr] = inner.finish(ps);
        outer = std::move(expr);
    }
};

template<> struct BuildAST<grammar::v1::expr::id> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        if (in.string_view() == "__curPos")
            s.emplaceExpr<ExprPos>(ps.at(in));
        else
            s.emplaceExpr<ExprVar>(ps.at(in), ps.symbols.create(in.string_view()));
    }
};

template<> struct BuildAST<grammar::v1::expr::int_> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        int64_t v;
        if (std::from_chars(in.begin(), in.end(), v).ec != std::errc{}) {
            throw ParseError({
                .msg = HintFmt("invalid integer '%1%'", in.string_view()),
                .pos = ps.positions[ps.at(in)],
            });
        }
        s.emplaceExpr<ExprInt>(ps.at(in), v);
    }
};

template<> struct BuildAST<grammar::v1::expr::float_> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        // copy the input into a temporary string so we can call stod.
        // can't use from_chars because libc++ (thus darwin) does not have it,
        // and floats are not performance-sensitive anyway. if they were you'd
        // be in much bigger trouble than this.
        //
        // we also get to do a locale-save dance because stod is locale-aware and
        // something (a plugin?) may have called setlocale or uselocale.
        static struct locale_hack {
            locale_t posix;
            locale_hack(): posix(newlocale(LC_ALL_MASK, "POSIX", 0))
            {
                if (posix == 0)
                    throw SysError("could not get POSIX locale");
            }
        } locale;

        auto tmp = in.string();
        double v = [&] {
            auto oldLocale = uselocale(locale.posix);
            Finally resetLocale([=] { uselocale(oldLocale); });
            try {
                return std::stod(tmp);
            } catch (...) {
                throw ParseError({
                    .msg = HintFmt("invalid float '%1%'", in.string_view()),
                    .pos = ps.positions[ps.at(in)],
                });
            }
        }();
        s.emplaceExpr<ExprFloat>(ps.at(in), NewValueAs::floating, v);
    }
};

struct StringState : SubexprState {
    using SubexprState::SubexprState;

    std::string currentLiteral;
    PosIdx currentPos;
    std::vector<std::pair<nix::PosIdx, std::unique_ptr<Expr>>> parts;

    void append(PosIdx pos, std::string_view s)
    {
        if (currentLiteral.empty())
            currentPos = pos;
        currentLiteral += s;
    }

    // FIXME this truncates strings on NUL for compat with the old parser. ideally
    // we should use the decomposition the g gives us instead of iterating over
    // the entire string again.
    void unescapeStr(std::string & str, State & ps)
    {
        char * s = str.data();
        char * t = s;
        char c;
        while ((c = *s++)) {
            if (c == '\\') {
                c = *s++;
                if (c == 'n') *t = '\n';
                else if (c == 'r') *t = '\r';
                else if (c == 't') *t = '\t';
                else *t = c;
            }
            else if (c == '\r') {
                /* Normalise CR and CR/LF into LF. */
                *t = '\n';
                if (*s == '\n') s++; /* cr/lf */
            }
            else *t = c;
            t++;
        }
        if (!ps.featureSettings.isEnabled(Dep::NulBytes) && size_t(s - str.data() - 1) != str.size())
            ps.nulFound(currentPos);
        str.resize(t - str.data());
    }

    void endLiteral(State & ps)
    {
        if (!currentLiteral.empty()) {
            unescapeStr(currentLiteral, ps);
            parts.emplace_back(currentPos, std::make_unique<ExprString>(currentPos, std::move(currentLiteral)));
        }
    }

    std::unique_ptr<Expr> finish(State & ps)
    {
        if (parts.empty()) {
            unescapeStr(currentLiteral, ps);
            return std::make_unique<ExprString>(currentPos, std::move(currentLiteral));
        } else {
            endLiteral(ps);
            auto pos = parts[0].first;
            return std::make_unique<ExprConcatStrings>(pos, true, std::move(parts));
        }
    }
};

template<typename... Content> struct BuildAST<grammar::v1::string::literal<Content...>> {
    static void apply(const auto & in, StringState & s, State & ps) {
        s.append(ps.at(in), in.string_view());
    }
};

template<> struct BuildAST<grammar::v1::string::cr_crlf> {
    static void apply(const auto & in, StringState & s, State & ps) {
        if (!ps.featureSettings.isEnabled(Dep::CRLineEndings))
            ps.badLineEndingFound(ps.at(in), false);
        else
            s.append(ps.at(in), in.string_view()); // FIXME compat with old parser
    }
};

template<> struct BuildAST<grammar::v1::string::interpolation> {
    static void apply(const auto & in, StringState & s, State & ps) {
        s.endLiteral(ps);
        s.parts.emplace_back(ps.at(in), s->popExprOnly());
    }
};

template<> struct BuildAST<grammar::v1::string::escape> {
    static void apply(const auto & in, StringState & s, State & ps) {
        if (!ps.featureSettings.isEnabled(Dep::NulBytes) && *in.begin() == '\0')
            ps.nulFound(ps.at(in));
        s.append(ps.at(in), "\\"); // FIXME compat with old parser
        s.append(ps.at(in), in.string_view());
    }
};

template<> struct BuildAST<grammar::v1::string> : change_head<StringState> {
    static void success0(StringState & s, ExprState & e, State & ps) {
        e.pushExpr(noPos, s.finish(ps));
    }
};

struct IndStringState : SubexprState {
    using SubexprState::SubexprState;

    std::vector<IndStringLine> lines;
};

template<> struct BuildAST<grammar::v1::ind_string::line_start> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        s.lines.push_back(IndStringLine { in.string_view(), ps.at(in) });
    }
};

template<typename... Content>
struct BuildAST<grammar::v1::ind_string::literal<Content...>> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        s.lines.back().parts.emplace_back(ps.at(in), in.string_view());
    }
};

template<> struct BuildAST<grammar::v1::ind_string::interpolation> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        s.lines.back().parts.emplace_back(ps.at(in), s->popExprOnly());
    }
};

template<> struct BuildAST<grammar::v1::ind_string::escape> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        switch (*in.begin()) {
        case 'n': s.lines.back().parts.emplace_back(ps.at(in), "\n"); break;
        case 'r': s.lines.back().parts.emplace_back(ps.at(in), "\r"); break;
        case 't': s.lines.back().parts.emplace_back(ps.at(in), "\t"); break;
        case 0:
            if (!ps.featureSettings.isEnabled(Dep::NulBytes)) {
                ps.nulFound(ps.at(in));
                break;
            }
            KJ_FALLTHROUGH;
        default:  s.lines.back().parts.emplace_back(ps.at(in), in.string_view()); break;
        }
    }
};

template<> struct BuildAST<grammar::v1::ind_string::has_content> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        s.lines.back().hasContent = true;
    }
};

template<> struct BuildAST<grammar::v1::ind_string::cr> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        if (!ps.featureSettings.isEnabled(Dep::CRLineEndings))
            ps.badLineEndingFound(ps.at(in), false);
    }
};

template<> struct BuildAST<grammar::v1::ind_string::nul> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        if (!ps.featureSettings.isEnabled(Dep::NulBytes))
            ps.nulFound(ps.at(in));
    }
};

template<> struct BuildAST<grammar::v1::ind_string> : change_head<IndStringState> {
    static void success(const auto & in, IndStringState & s, ExprState & e, State & ps) {
        e.pushExpr(noPos, ps.stripIndentation(ps.at(in), std::move(s.lines)));
    }
};

template<typename... Content> struct BuildAST<grammar::v1::path::literal<Content...>> {
    static void apply(const auto & in, StringState & s, State & ps) {
        s.append(ps.at(in), in.string_view());
        s.endLiteral(ps);
    }
};

template<> struct BuildAST<grammar::v1::path::interpolation> : BuildAST<grammar::v1::string::interpolation> {};

template<> struct BuildAST<grammar::v1::path::anchor> {
    static void apply(const auto & in, StringState & s, State & ps) {
        Path path(absPath(in.string(), ps.basePath.canonical().abs()));
        /* add back in the trailing '/' to the first segment */
        if (in.string_view().ends_with('/') && in.size() > 1)
            path += "/";
        s.parts.emplace_back(ps.at(in), new ExprPath(ps.at(in), std::move(path)));
    }
};

template<> struct BuildAST<grammar::v1::path::home_anchor> {
    static void apply(const auto & in, StringState & s, State & ps) {
        if (evalSettings.pureEval)
            throw Error("the path '%s' can not be resolved in pure mode", in.string_view());
        Path path(getHome() + in.string_view().substr(1));
        s.parts.emplace_back(ps.at(in), new ExprPath(ps.at(in), std::move(path)));
    }
};

template<> struct BuildAST<grammar::v1::path::searched_path> {
    static void apply(const auto & in, StringState & s, State & ps) {
        auto pos = ps.at(in);
        std::vector<std::unique_ptr<Expr>> args{2};
        /* Overriding __nixPath, while being barely documented, is intended and supported:
         * https://github.com/NixOS/nix/commit/62a6eeb1f3da0a5954ad2da54c454eb7fc1c6e5d
         * (TODO: Provide a better and officially supported and documented mechanism for doing this)
         */
        args[0] = std::make_unique<ExprVar>(pos, ps.s.nixPath);
        args[1] = std::make_unique<ExprString>(pos, in.string());
        s.parts.emplace_back(
            pos,
            std::make_unique<ExprCall>(
                pos,
                /* The option for overriding this should be deprecated eventually, but for now it has to stay
                 * until we can figure out how to design a better replacement.
                 * https://git.lix.systems/lix-project/lix/issues/599
                 */
                std::make_unique<ExprVar>(pos, ps.s.findFile),
                std::move(args)));
    }
};

template<> struct BuildAST<grammar::v1::path> : change_head<StringState> {
    template<typename E>
    static void check_slash(PosIdx end, StringState & s, State & ps) {
        auto e = dynamic_cast<E *>(s.parts.back().second.get());
        if (!e || !e->s.ends_with('/'))
            return;
        if (s.parts.size() > 1 || e->s != "/")
            throw ParseError({
                .msg = HintFmt("path has a trailing slash"),
                .pos = ps.positions[end],
            });
    }

    static void success(const auto & in, StringState & s, ExprState & e, State & ps) {
        s.endLiteral(ps);
        check_slash<ExprPath>(ps.atEnd(in), s, ps);
        check_slash<ExprString>(ps.atEnd(in), s, ps);
        if (s.parts.size() == 1) {
            e.pushExpr(noPos, std::move(s.parts.back().second));
        } else {
            e.emplaceExpr<ExprConcatStrings>(ps.at(in), false, std::move(s.parts));
        }
    }
};

// strings and paths sare handled fully by the grammar-level rule for now
template<> struct BuildAST<grammar::v1::expr::string> : p::maybe_nothing {};
template<> struct BuildAST<grammar::v1::expr::ind_string> : p::maybe_nothing {};
template<> struct BuildAST<grammar::v1::expr::path> : p::maybe_nothing {};

template<> struct BuildAST<grammar::v1::expr::uri> {
    static void apply(const auto & in, ExprState & s, State & ps) {
       bool URLLiterals = ps.featureSettings.isEnabled(Dep::UrlLiterals);
       if (!URLLiterals)
           throw ParseError({
               .msg = HintFmt("URL literals are deprecated, allow using them with %s", "--extra-deprecated-features url-literals"),
               .pos = ps.positions[ps.at(in)]
           });
       s.emplaceExpr<ExprString>(ps.at(in), in.string());
    }
};

template<> struct BuildAST<grammar::v1::expr::ancient_let> : change_head<BindingsStateRecSet> {
    static void success(const auto & in, BindingsStateRecSet & b, ExprState & s, State & ps) {
        // Added 2024-09-18. Turn into an error at some point in the future.
        // See the documentation on deprecated features for more details.
        if (!ps.featureSettings.isEnabled(Dep::AncientLet))
            //FIXME: why aren't there any tests for this?
            logWarning({
                .msg = HintFmt(
                    "%s is deprecated and will be removed in the future. Use %s to silence this warning.",
                    "let {",
                    "--extra-deprecated-features ancient-let"
                    ),
                .pos = ps.positions[ps.at(in)]
            });

        auto pos = ps.at(in);
        b.set.pos = pos;
        s.emplaceExpr<ExprSelect>(pos, std::make_unique<ExprSet>(std::move(b.set)), pos, ps.s.body);
    }
};

template<> struct BuildAST<grammar::v1::expr::rec_set> : change_head<BindingsStateRecSet> {
    static void success(const auto & in, BindingsStateRecSet & b, ExprState & s, State & ps) {
        b.set.pos = ps.at(in);
        s.pushExpr(ps.at(in), std::make_unique<ExprSet>(std::move(b.set)));
    }
};

template<> struct BuildAST<grammar::v1::expr::set> : change_head<BindingsStateSet> {
    static void success(const auto & in, BindingsStateSet & b, ExprState & s, State & ps) {
        b.set.pos = ps.at(in);
        s.pushExpr(ps.at(in), std::make_unique<ExprSet>(std::move(b.set)));
    }
};

using ListState = std::vector<std::unique_ptr<Expr>>;

template<> struct BuildAST<grammar::v1::expr::list> : change_head<ListState> {
    static void success(const auto & in, ListState & ls, ExprState & s, State & ps) {
        auto e = std::make_unique<ExprList>(ps.at(in));
        e->elems = std::move(ls);
        s.pushExpr(ps.at(in), std::move(e));
    }
};

template<> struct BuildAST<grammar::v1::expr::list::entry> : change_head<ExprState> {
    static void success0(ExprState & e, ListState & s, State & ps) {
        s.emplace_back(e.finish(ps).second);
    }
};

struct SelectState : SubexprState {
    using SubexprState::SubexprState;

    PosIdx pos;
    ExprSelect * e = nullptr;
};

template<> struct BuildAST<grammar::v1::expr::select::head> {
    static void apply(const auto & in, SelectState & s, State & ps) {
        s.pos = ps.at(in);
    }
};

template<> struct BuildAST<grammar::v1::expr::select::attr> : change_head<AttrState> {
    static void success0(AttrState & a, SelectState & s, State &) {
        s.e = &s->emplaceExpr<ExprSelect>(s.pos, s->popExprOnly(), std::move(a.attrs), nullptr);
    }
};

template<> struct BuildAST<grammar::v1::expr::select::attr_or> {
    static void apply0(SelectState & s, State &) {
        s.e->def = s->popExprOnly();
    }
};

template<> struct BuildAST<grammar::v1::expr::select::as_app_or> {
    static void apply(const auto & in, SelectState & s, State & ps) {
        std::vector<std::unique_ptr<Expr>> args(1);
        args[0] = std::make_unique<ExprVar>(ps.at(in), ps.s.or_);
        s->emplaceExpr<ExprCall>(s.pos, s->popExprOnly(), std::move(args));
    }
};

template<> struct BuildAST<grammar::v1::expr::select> : change_head<SelectState> {
    static void success0(const auto &...) {}
};

struct AppState : SubexprState {
    using SubexprState::SubexprState;

    PosIdx pos;
    ExprCall * e = nullptr;
};

template<> struct BuildAST<grammar::v1::expr::app::select_or_fn> {
    static void apply(const auto & in, AppState & s, State & ps) {
        s.pos = ps.at(in);
    }
};

template<> struct BuildAST<grammar::v1::expr::app::first_arg> {
    static void apply(auto & in, AppState & s, State & ps) {
        auto arg = s->popExprOnly(), fn = s->popExprOnly();
        if ((s.e = dynamic_cast<ExprCall *>(fn.get()))) {
            // TODO remove.
            // AST compat with old parser, semantics are the same.
            // this can happen on occasions such as `<p> <p>` or `a or b or`,
            // neither of which are super worth optimizing.
            s.e->args.push_back(std::move(arg));
            s->pushExpr(noPos, std::move(fn));
        } else {
            std::vector<std::unique_ptr<Expr>> args{1};
            args[0] = std::move(arg);
            s.e = &s->emplaceExpr<ExprCall>(s.pos, std::move(fn), std::move(args));
        }
    }
};

template<> struct BuildAST<grammar::v1::expr::app::another_arg> {
    static void apply0(AppState & s, State & ps) {
        s.e->args.push_back(s->popExprOnly());
    }
};

template<> struct BuildAST<grammar::v1::expr::app> : change_head<AppState> {
    static void success0(const auto &...) {}
};

template<typename Op> struct BuildAST<grammar::v1::expr::operator_<Op>> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        s.pushOp(ps.at(in), Op{}, ps);
    }
};
template<> struct BuildAST<grammar::v1::expr::operator_<grammar::v1::op::has_attr>> : change_head<AttrState> {
    static void success(const auto & in, AttrState & a, ExprState & s, State & ps) {
        s.pushOp(ps.at(in), ExprState::has_attr{{}, std::move(a.attrs)}, ps);
    }
};

template<> struct BuildAST<grammar::v1::expr::assert_> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        auto body = s.popExprOnly(), cond = s.popExprOnly();
        s.emplaceExpr<ExprAssert>(ps.at(in), std::move(cond), std::move(body));
    }
};

template<> struct BuildAST<grammar::v1::expr::with> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        auto body = s.popExprOnly(), scope = s.popExprOnly();
        s.emplaceExpr<ExprWith>(ps.at(in), std::move(scope), std::move(body));
    }
};

template<> struct BuildAST<grammar::v1::expr::let> : change_head<BindingsStateLet> {
    static void success(const auto & in, BindingsStateLet & b, ExprState & s, State & ps) {
        if (!b.let.dynamicAttrs.empty())
            throw ParseError({
                .msg = HintFmt("dynamic attributes not allowed in let"),
                .pos = ps.positions[ps.at(in)]
            });
        b.let.body = b->popExprOnly();
        b.let.pos = ps.at(in);
        s.pushExpr(ps.at(in), std::make_unique<ExprLet>(std::move(b.let)));
    }
};

template<> struct BuildAST<grammar::v1::expr::if_> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        auto else_ = s.popExprOnly(), then = s.popExprOnly(), cond = s.popExprOnly();
        s.emplaceExpr<ExprIf>(ps.at(in), std::move(cond), std::move(then), std::move(else_));
    }
};

template<> struct BuildAST<grammar::v1::expr> : change_head<ExprState> {
    static void success0(ExprState & inner, ExprState & outer, State & ps) {
        auto [pos, expr] = inner.finish(ps);
        outer.pushExpr(pos, std::move(expr));
    }
};

}
}
