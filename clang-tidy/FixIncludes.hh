#pragma once
///@file

#include <clang-tidy/ClangTidyCheck.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <llvm/ADT/StringRef.h>

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::tidy;

class FixIncludesCheck : public ClangTidyCheck {
    public:
    FixIncludesCheck(StringRef Name, ClangTidyContext *Context)
            : ClangTidyCheck(Name, Context) {}

    void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP, Preprocessor *ModuleExpanderPP) override;
};

};
