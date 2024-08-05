#include "CharPtrCast.hh"
#include <clang/AST/ExprCXX.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Tooling/Transformer/SourceCode.h>

namespace nix::clang_tidy {
using namespace clang::ast_matchers;
using namespace clang;

void CharPtrCastCheck::registerMatchers(ast_matchers::MatchFinder *Finder) {
  Finder->addMatcher(
      traverse(clang::TK_IgnoreUnlessSpelledInSource,
               cxxReinterpretCastExpr(allOf(
                   hasDestinationType(qualType(pointsTo(isAnyCharacter()))),
                   has(expr(hasType(qualType(pointsTo(isAnyCharacter()))))))))
          .bind("reinterpret-cast-expr"),
      this);
}

void CharPtrCastCheck::check(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  const auto ReinterpretCastExpr =
      Result.Nodes.getNodeAs<CXXReinterpretCastExpr>("reinterpret-cast-expr");
  const auto ToTypeSpan = ReinterpretCastExpr->getAngleBrackets();
  const auto & SM = Result.Context->getSourceManager();

  auto Diag =
      diag(ReinterpretCastExpr->getExprLoc(),
           "reinterpret_cast used for trivially safe character pointer cast");
  Diag << ReinterpretCastExpr->getSourceRange();

  auto Inside = tooling::getText(*ReinterpretCastExpr->getSubExprAsWritten(),
                                 *Result.Context);

  Diag << Inserter.createIncludeInsertion(SM.getFileID(ReinterpretCastExpr->getExprLoc()), "charptr-cast.hh");

  llvm::Twine Replacement =
      "charptr_cast" +
      tooling::getText(CharSourceRange(ToTypeSpan, true), *Result.Context) +
      "(" + Inside + ")";
  Diag << FixItHint::CreateReplacement(ReinterpretCastExpr->getSourceRange(),
                                       Replacement.str());
}

} // namespace nix::clang_tidy
