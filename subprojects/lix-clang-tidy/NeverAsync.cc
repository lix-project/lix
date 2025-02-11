#include "NeverAsync.hh"
#include <clang/AST/ASTTypeTraits.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/AttrKinds.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Tooling/Transformer/SourceCode.h>
#include <clang/Tooling/Transformer/SourceCodeBuilders.h>
#include <llvm/Support/ErrorHandling.h>

namespace nix::clang_tidy {
using namespace clang::ast_matchers;
using namespace clang;
using namespace std::literals;

void NeverAsync::registerMatchers(ast_matchers::MatchFinder *Finder) {
  auto neverAsyncT = cxxRecordDecl(hasName("nix::NeverAsync"));
  auto aioRootT = cxxRecordDecl(hasName("nix::AsyncIoRoot"));
  // e.g.: auto foo = someNeverAsyncValue;
  //                  ^^^^^^^^^^^^^^^^^^^
  // e.g. someFunctionWithNeverAsyncDefaultParam()
  auto neverAsync = cxxConstructExpr(
      allOf(hasDeclaration(cxxConstructorDecl(isCopyConstructor())),
            hasType(neverAsyncT)));

  // Any class from which you can directly get an AsyncIoRoot (since it is illegal to use one of those from async code).
  // 1. class Foo1 : nix::NeverAsync { ... };
  // 2. class Foo2 { nix::AsyncIoRoot &aio; };
  // 3. class Foo3 { nix::AsyncIoRoot &aio(); };
  // 4. class Bar : Foo3 {};
  // 2. 3. and 4. exist particularly to match any of the CLI commands in lix/nix/
  auto neverAsyncClass = anyOf(
      isDerivedFrom(neverAsyncT), has(fieldDecl(hasType(references(aioRootT)))),
      hasMethod(returns(references(aioRootT))),
      hasAnyBase(
          hasType(cxxRecordDecl(hasMethod(returns(references(aioRootT)))))));
  // Explicitly marked never callable from async
  // e.g. void foo(nix::NeverAsync marker = {})
  auto fnMarkedNeverAsync = hasAnyParameter(hasType(neverAsyncT));
  // Functions matching the following:
  // 1. void foo(nix::AioRoot &)
  // 2. void foo(nix::NeverAsync)
  // 3. void foo(nix::NeverAsync &)
  auto fnIsNeverAsync =
      anyOf(fnMarkedNeverAsync, hasAnyParameter(hasType(references(aioRootT))),
            hasAnyParameter(
                anyOf(hasType(cxxRecordDecl(neverAsyncClass)),
                      hasType(references(cxxRecordDecl(neverAsyncClass))))));

  // Call expression that is allowed to block indefinitely (e.g. by calling lockFile or similar).
  // 1. A call expr to a function like: `void foo(nix::NeverAsync marker = {})`
  // 2. void foo(nix::NeverAsync marker = {}) { ... }
  //                   call inside this context ^^^
  // 3. class Foo : nix::NeverAsync { void foo() { ... } };
  //                      call inside this context ^^^
  auto stmtAllowedToBlockIndefinitely = anyOf(
      hasAnyArgument(neverAsync), forCallable(functionDecl(fnIsNeverAsync)),
      forCallable(cxxMethodDecl(ofClass(neverAsyncClass))));

  // e.g. any function like `kj::Promise<void> foo()`
  auto fnIsAsync =
      returns(hasDeclaration(cxxRecordDecl(hasName("kj::Promise"))));

  Finder->addMatcher(
      traverse(clang::TK_AsIs,
               // foo() where foo() has a NeverAsync parameter
               // except if it is inside a function marked as allowed to block indefinitely
               invocation(hasDeclaration(functionDecl(fnMarkedNeverAsync)),
                          forCallable(functionDecl().bind("fn")),
                          unless(stmtAllowedToBlockIndefinitely))
                   .bind("call")),
      this);

  Finder->addMatcher(
      traverse(clang::TK_AsIs,
               // foo() where foo() has a nix::NeverAsync parameter inside a coroutine
               invocation(hasDeclaration(functionDecl(fnMarkedNeverAsync)),
                          forCallable(functionDecl(fnIsAsync).bind("fn")))
                   .bind("invalid-call")),
      this);

  Finder->addMatcher(traverse(clang::TK_AsIs,
                              cxxMethodDecl(ofClass(neverAsyncClass), fnIsAsync)
                                  .bind("bad-method")),
                     this);
}

void NeverAsync::check(const ast_matchers::MatchFinder::MatchResult &Result) {
  if (const auto *call = Result.Nodes.getNodeAs<Expr>("call")) {
    const auto *fn = Result.Nodes.getNodeAs<FunctionDecl>("fn");
    assert(fn);
    diag(call->getExprLoc(),
         "Call to never-async function without either: the calling function having a nix::NeverAsync parameter itself (recommended) or using the nix::always_progresses escape hatch\nSee the definition of nix::NeverAsync in lix/libutil/types.h for details");
  } else if (const auto *call = Result.Nodes.getNodeAs<Expr>("invalid-call")) {
    const auto *fn = Result.Nodes.getNodeAs<FunctionDecl>("fn");
    assert(fn);
    diag(call->getExprLoc(), "Calling never-async functions inside promises is forbidden. See the definition of nix::NeverAsync in lix/libutil/types.h for details");
  } else if (const auto *fn =
                 Result.Nodes.getNodeAs<CXXMethodDecl>("bad-method")) {
    diag(fn->getLocation(), "Defining coroutines inside never-async classes is forbidden.");
  } else {
    llvm_unreachable("bad match");
  }
}
} // namespace nix::clang_tidy
