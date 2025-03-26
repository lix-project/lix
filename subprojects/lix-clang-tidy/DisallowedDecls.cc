#include "DisallowedDecls.hh"
#include <clang/AST/ASTTypeTraits.h>
#include <clang/AST/Expr.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Tooling/Transformer/SourceCode.h>
#include <clang/Tooling/Transformer/SourceCodeBuilders.h>

namespace nix::clang_tidy {
using namespace clang::ast_matchers;
using namespace clang;

void DisallowedDeclsCheck::registerMatchers(ast_matchers::MatchFinder *Finder) {
  Finder->addMatcher(
      traverse(clang::TK_AsIs,
               callExpr(callee(cxxMethodDecl(hasName("parse"),
                                             ofClass(isSameOrDerivedFrom(
                                                 "nlohmann::basic_json")))))
                   .bind("json-parse")),
      this);
}

void DisallowedDeclsCheck::check(
    const ast_matchers::MatchFinder::MatchResult &Result) {

  if (auto MatchedParse = Result.Nodes.getNodeAs<CallExpr>("json-parse")) {
    auto Diag = diag(MatchedParse->getExprLoc(),
                     "using nlohmann::basic_json::parse is disallowed, use the lix wrapper instead");
    Diag << FixItHint::CreateReplacement(MatchedParse->getCallee()->getSourceRange(),
                                       "json::parse");
  }
}
}; // namespace nix::clang_tidy
