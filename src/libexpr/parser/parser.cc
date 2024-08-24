#include "attr-set.hh"
#include "error.hh"
#include "eval-settings.hh"
#include "eval.hh"
#include "finally.hh"
#include "nixexpr.hh"
#include "symbol-table.hh"
#include "users.hh"

#include "change_head.hh"
#include "grammar.hh"
#include "state.hh"

#include <charconv>
#include <memory>

// Linter complains that this is a "suspicious include of file with '.cc' extension".
// While that is correct and generally not great, it is one of the less bad options to pick
// in terms of diff noise.
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "parser-impl1.inc.cc"

namespace nix {

Expr * EvalState::parse(
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
        exprSymbols,
        featureSettings,
    };

    assert(length >= 2);
    assert(text[length - 1] == 0);
    assert(text[length - 2] == 0);
    length -= 2;

    p::string_input<p::tracking_mode::lazy> inp{std::string_view{text, length}, "input"};
    try {
        parser::v1::ExprState x;
        p::parse<parser::grammar::v1::root, parser::v1::BuildAST, parser::v1::Control>(inp, x, s);

        auto [_pos, result] = x.finish(s);
        result->bindVars(*this, staticEnv);
        return result.release();
    } catch (p::parse_error & e) {
        auto pos = e.positions().back();
        throw ParseError({
            .msg = HintFmt("syntax error, %s", e.message()),
            .pos = positions[s.positions.add(s.origin, pos.byte)]
        });
    }
}

}
