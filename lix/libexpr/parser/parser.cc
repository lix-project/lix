#include "lix/libutil/error.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/nixexpr.hh"

#include "lix/libexpr/parser/grammar.hh"
#include "lix/libexpr/parser/state.hh"

#include <memory>

// Linter complains that this is a "suspicious include of file with '.cc' extension".
// While that is correct and generally not great, it is one of the less bad options to pick
// in terms of diff noise.
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "lix/libexpr/parser/parser-impl1.inc.cc"

namespace nix {

Expr * Evaluator::parse(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    std::shared_ptr<StaticEnv> & staticEnv,
    const FeatureSettings & featureSettings)
{
    parser::State s = {
        symbols,
        positions,
        basePath,
        positions.addOrigin(origin, length),
        this->s.exprSymbols,
        featureSettings,
    };

    p::string_input<p::tracking_mode::lazy> inp{std::string_view{text, length}, "input"};
    try {
        parser::v1::ExprState x;
        p::parse<parser::grammar::v1::root, parser::v1::BuildAST, parser::v1::Control>(inp, x, s);

        auto [_pos, result] = x.finish(s);
        result = Expr::finalize(std::move(result), *this, staticEnv);
        return result.release();
    } catch (p::parse_error & e) { // NOLINT(lix-foreign-exceptions)
        auto pos = e.positions().back();
        throw ParseError({
            .msg = HintFmt("syntax error, %s", e.message()),
            .pos = positions[s.positions.add(s.origin, pos.byte)]
        });
    }
}

std::variant<std::unique_ptr<Expr>, ExprReplBindings>
Evaluator::parse_repl(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    std::shared_ptr<StaticEnv> & staticEnv,
    const FeatureSettings & featureSettings)
{
    parser::State s = {
        symbols,
        positions,
        basePath,
        positions.addOrigin(origin, length),
        this->s.exprSymbols,
        featureSettings,
    };

    p::string_input<p::tracking_mode::lazy> inp{std::string_view{text, length}, "input"};
    try {
        parser::v1::ReplRootState x;
        p::parse<parser::grammar::v1::repl_root, parser::v1::BuildAST, parser::v1::Control>(inp, x, s);

        std::visit(overloaded {
            [&] (ExprReplBindings & result) { result.finalize(*this, staticEnv); },
            [&] (auto & result) { result = Expr::finalize(std::move(result), *this, staticEnv); }
        }, x);
        return x;
    } catch (p::parse_error & e) { // NOLINT(lix-foreign-exceptions)
        auto pos = e.positions().back();
        throw ParseError({
            .msg = HintFmt("syntax error, %s", e.message()),
            .pos = positions[s.positions.add(s.origin, pos.byte)]
        });
    }
}

}
