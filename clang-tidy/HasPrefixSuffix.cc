#include "HasPrefixSuffix.hh"
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
#include <iostream>

namespace nix::clang_tidy {
using namespace clang::ast_matchers;
using namespace clang;

void HasPrefixSuffixCheck::registerMatchers(ast_matchers::MatchFinder *Finder) {
  Finder->addMatcher(
      traverse(clang::TK_AsIs,
               callExpr(callee(functionDecl(anyOf(hasName("hasPrefix"),
                                                  hasName("hasSuffix")))
                                   .bind("callee-decl")),
                        optionally(hasArgument(
                            0, cxxConstructExpr(
                                   hasDeclaration(functionDecl(hasParameter(
                                       0, parmVarDecl(hasType(
                                              asString("const char *")))))))
                                   .bind("implicit-cast"))))
                   .bind("call")),
      this);
}

void HasPrefixSuffixCheck::check(
    const ast_matchers::MatchFinder::MatchResult &Result) {

  const auto *CalleeDecl = Result.Nodes.getNodeAs<FunctionDecl>("callee-decl");
  auto FuncName = std::string(CalleeDecl->getName());
  std::string NewName;
  if (FuncName == "hasPrefix") {
    NewName = "starts_with";
  } else if (FuncName == "hasSuffix") {
    NewName = "ends_with";
  } else {
    llvm_unreachable("nix-has-prefix: invalid callee");
  }

  const auto *MatchedDecl = Result.Nodes.getNodeAs<CallExpr>("call");
  const auto *ImplicitConvertArg =
      Result.Nodes.getNodeAs<CXXConstructExpr>("implicit-cast");

  const auto *Lhs = MatchedDecl->getArg(0);
  const auto *Rhs = MatchedDecl->getArg(1);
  auto Diag = diag(MatchedDecl->getExprLoc(), FuncName + " is deprecated");

  std::string Text = "";

  // Form possible cast to string_view, or nothing.
  if (ImplicitConvertArg) {
    Text = "std::string_view(";
    Text.append(tooling::getText(*Lhs, *Result.Context));
    Text.append(").");
  } else {
    Text.append(*tooling::buildAccess(*Lhs, *Result.Context));
  }

  // Call .starts_with.
  Text.append(NewName);
  Text.push_back('(');
  Text.append(tooling::getText(*Rhs, *Result.Context));
  Text.push_back(')');

  Diag << FixItHint::CreateReplacement(MatchedDecl->getSourceRange(), Text);

  // for (const auto *arg : MatchedDecl->arguments()) {
  //   arg->dumpColor();
  //   arg->getType().dump();
  // }
}
}; // namespace nix::clang_tidy
