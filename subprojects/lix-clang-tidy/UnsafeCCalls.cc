#include "UnsafeCCalls.hh"
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Tooling/Transformer/SourceCode.h>

namespace nix::clang_tidy {
using namespace clang::ast_matchers;
using namespace clang;

void UnsafeCCalls::registerMatchers(ast_matchers::MatchFinder *Finder) {
  auto cStringType = pointerType(pointee(isAnyCharacter(), isConstQualified()));

  Finder->addMatcher(
      traverse(
          clang::TK_IgnoreUnlessSpelledInSource,
          callExpr(callee(functionDecl(
                       hasAnyParameter(hasType(cStringType)),
                       unless(anyOf(hasName("strlen"), hasName("strdup"),
                                    hasName("strcpy"))),
                       unless(hasAncestor(namespaceDecl())))),
                   hasAnyArgument(allOf(
                       hasType(asString("const char *")),
                       unless(callExpr(callee(cxxMethodDecl(
                           hasName("asCStr"), hasParent(cxxRecordDecl(hasName(
                                                  "nix::CString")))))))))))
          .bind("call"),
      this);
}

void UnsafeCCalls::check(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  const auto ReinterpretCastExpr = Result.Nodes.getNodeAs<CallExpr>("call");
  auto Diag =
      diag(ReinterpretCastExpr->getExprLoc(),
           "potentially unsafe call to C function (maybe use a sys::* wrapper instead)");
  Diag << ReinterpretCastExpr->getSourceRange();
}

} // namespace nix::clang_tidy
