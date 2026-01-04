#pragma once
///@file

#include <clang-tidy/ClangTidyCheck.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Basic/SourceLocation.h>
#include <llvm/ADT/StringRef.h>

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::tidy;
using namespace llvm;

class ForbiddenIncludesCheck : public ClangTidyCheck {
public:
  struct Mark {
    SourceLocation loc;
    StringRef name;
  };
  struct Marks {
    std::vector<Mark> loc;
  };

  std::shared_ptr<Marks> marks;

  ForbiddenIncludesCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP,
                           Preprocessor *ModuleExpanderPP) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};
}; // namespace nix::clang_tidy
