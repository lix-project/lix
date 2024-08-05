#pragma once
///@file

#include <clang-tidy/ClangTidyCheck.h>
#include <clang-tidy/utils/IncludeInserter.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <llvm/ADT/StringRef.h>

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::tidy;

class CharPtrCastCheck : public ClangTidyCheck {
  tidy::utils::IncludeInserter Inserter{
      Options.getLocalOrGlobal("IncludeStyle",
                               tidy::utils::IncludeSorter::IS_Google),
      false};

public:
  CharPtrCastCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}

  void registerPPCallbacks(const SourceManager &, Preprocessor *PP,
                           Preprocessor *) override {
    Inserter.registerPreprocessor(PP);
  }

  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};
} // namespace nix::clang_tidy
