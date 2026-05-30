#include "ForeignExceptions.hh"
#include <clang/AST/ASTTypeTraits.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/StmtCXX.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ASTMatchers/ASTMatchersMacros.h>
#include <clang/Basic/ExceptionSpecificationType.h>
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

  auto std_path = cxxRecordDecl(hasName("std::filesystem::path"));
  auto fs_error = cxxRecordDecl(hasName("std::filesystem::filesystem_error"));
  auto std_exception = cxxRecordDecl(hasName("std::exception"));
  Finder->addMatcher(
      traverse(
          clang::TK_AsIs,
          callExpr(
              forFunction(functionDecl().bind("fn")),
              callee(functionDecl(
                  hasAncestor(namespaceDecl(hasName("std::filesystem"))),
                  unless(anyOf(
                      // noexcept functions are obviously fine
                      isNoThrow(),
                      // this wants to match ostream << path. templates!
                      allOf(
                          hasOverloadedOperatorName("<<"),
                          hasParameter(0, hasType(references(cxxRecordDecl(
                                              hasName("std::basic_ostream"))))),
                          hasParameter(1, hasType(references(std_path)))),
                      // path / path is fine too, obviously
                      allOf(hasOverloadedOperatorName("/"),
                            hasParameter(0, hasType(references(std_path))),
                            hasParameter(1, hasType(references(std_path)))))))),
              // we just allow fs::path because it is allowed to throw some
              // implementation-defined exceptions, none of which we can in
              // any reasonable way handle in generic code. c++ is *great*.
              unless(callee(cxxMethodDecl(ofClass(std_path)))),
              // allow calls if they're wrapped in a proper try/catch. this
              // doesn't look through immediate-call lambdas by choice; the
              // check would be incomprehensible if we checked lambdas too.
              unless(hasAncestor(cxxTryStmt(
                  forFunction(functionDecl(equalsBoundNode("fn"))),
                  has(cxxCatchStmt(anyOf(
                      isCatchAll(),
                      // allow foreign catches to silence the call site. we
                      // will still warn for the *catch* site later though.
                      has(varDecl(hasType(references(fs_error)))),
                      has(varDecl(hasType(references(std_exception)))))))))))
              .bind("bad-call")),
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
  } else if (const auto *call = Result.Nodes.getNodeAs<CallExpr>("bad-call")) {
    diag(call->getExprLoc(), "throws non-Lix exceptions. Ensure that they are "
                             "caught and wrapped properly.")
        << call->getSourceRange();
  } else {
    llvm_unreachable("bad match");
  }
}
} // namespace nix::clang_tidy
