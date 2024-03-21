#pragma once
///@file
/// This is an example of a clang-tidy automated refactoring against the Nix
/// codebase. The refactoring has been completed in
/// https://gerrit.lix.systems/c/lix/+/565 so this code is around as
/// an example.

#include <clang-tidy/ClangTidyCheck.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <llvm/ADT/StringRef.h>

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::tidy;
using namespace llvm;

class HasPrefixSuffixCheck : public ClangTidyCheck {
public:
  HasPrefixSuffixCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};
}; // namespace nix::clang_tidy
