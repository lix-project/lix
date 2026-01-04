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
  // we have none right now
  (void)Finder;
}

void DisallowedDeclsCheck::check(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  // we have none right now
  (void)Result;
}
}; // namespace nix::clang_tidy
