#include "ForeignExceptions.hh"
#include <clang/AST/ASTTypeTraits.h>
#include <clang/AST/Decl.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/StmtCXX.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ASTMatchers/ASTMatchersMacros.h>
#include <llvm/Support/ErrorHandling.h>

namespace nix::clang_tidy {
using namespace clang::ast_matchers;
using namespace clang;
using namespace std::literals;

void ForeignExceptions::registerMatchers(ast_matchers::MatchFinder *Finder) {
  // bad_alloc is explicitly allowed because wrapping it would require *more*
  // allocations, and if we're already bad_alloc'ing that will probably fail.
  auto allowedExceptions =
      cxxRecordDecl(anyOf(isSameOrDerivedFrom(hasName("nix::BaseException")),
                          hasName("std::bad_alloc")));
  auto isAllowedCatch =
      anyOf(isCatchAll(), has(varDecl(hasType(references(allowedExceptions)))));

  Finder->addMatcher(
      traverse(clang::TK_AsIs,
               cxxCatchStmt(unless(isAllowedCatch)).bind("catch")),
      this);

  // bare `throw;` is allowed if it's known to rethrow a BaseException-ish type.
  auto rethrowsAllowed = allOf(
      unless(has(expr())), hasAncestor(stmt(forCallable(equalsBoundNode("fn")),
                                            cxxCatchStmt(isAllowedCatch))));
  // `throw e` is allowed if `e` is BaseException-ish including refs, moves, etc
  auto throwsAllowed = anyOf(
      has(expr(hasType(allowedExceptions))),
      has(cxxConstructExpr(hasDeclaration(hasParent(allowedExceptions)))));

  Finder->addMatcher(
      traverse(clang::TK_AsIs,
               stmt(forCallable(decl().bind("fn")),
                    cxxThrowExpr(unless(anyOf(rethrowsAllowed, throwsAllowed)))
                        .bind("throw"))),
      this);

  // flag STL constructors/functions that have caused exception problems before.
  Finder->addMatcher(
      traverse(
          clang::TK_AsIs,
          cxxConstructExpr(
              hasDeclaration(cxxConstructorDecl(
                  hasAncestor(cxxRecordDecl(hasName("std::basic_regex"))),
                  unless(anyOf(isDefaultConstructor(), isCopyConstructor(), isMoveConstructor())))))
              .bind("bad-ctor")),
      this);
}

void ForeignExceptions::check(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  if (const auto *node = Result.Nodes.getNodeAs<CXXCatchStmt>("catch")) {
    diag(node->getCatchLoc(),
        "Do not catch exceptions declared outside of Lix except at API "
        "boundaries, otherwise we can't provide useful traces for async "
        "functions. Catch nix::ForeignException instead and use its "
        "as<T>/is<T> methods everywhere else.");
  } else if (const auto *node = Result.Nodes.getNodeAs<CXXThrowExpr>("throw")) {
    if (node->getSubExpr() && node->getSubExpr()->isTypeDependent()) {
      diag(node->getThrowLoc(),
           "Thrown exception is type-dependent. Make sure it derives from "
           "nix::BaseException and mark this site as NOLINT.");
    } else {
      diag(
          node->getThrowLoc(),
          "Do not throw exceptions declared outside of Lix, otherwise we can't "
          "provide useful traces for async functions. Throw "
          "nix::ForeignException instead where possible.");
    }
  } else if (const auto *ctor = Result.Nodes.getNodeAs<CXXConstructExpr>("bad-ctor")) {
    diag(
        ctor->getLocation(),
        "%0 throws non-Lix exceptions. Ensure that they are caught and wrapped "
        "properly, ideally by wrapping the constructor invocation itself.")
        << ctor->getConstructor()->getNameAsString();
  } else {
    llvm_unreachable("bad match");
  }
}
} // namespace nix::clang_tidy
