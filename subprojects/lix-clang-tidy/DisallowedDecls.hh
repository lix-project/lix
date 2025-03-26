#pragma once
///@file

#include <clang-tidy/ClangTidyCheck.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <llvm/ADT/StringRef.h>

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::tidy;
using namespace llvm;

class DisallowedDeclsCheck : public ClangTidyCheck {
public:
  DisallowedDeclsCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};
}; // namespace nix::clang_tidy
