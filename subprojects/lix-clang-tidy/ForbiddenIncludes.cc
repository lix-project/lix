#include "ForbiddenIncludes.hh"
#include <clang/AST/ASTTypeTraits.h>
#include <clang/AST/Expr.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/Transformer/SourceCode.h>
#include <clang/Tooling/Transformer/SourceCodeBuilders.h>
#include <memory>
#include <stack>

namespace nix::clang_tidy {
using namespace clang::ast_matchers;
using namespace clang;

namespace {

struct ForbiddenIncludesCallback : PPCallbacks {
  std::shared_ptr<ForbiddenIncludesCheck::Marks> Marks =
      std::make_shared<ForbiddenIncludesCheck::Marks>();
  SourceManager &SM;
  std::stack<bool> inUserHeader;

  ForbiddenIncludesCallback(Preprocessor &PP) : SM(PP.getSourceManager()) {}

  void LexedFileChanged(FileID, LexedFileChangeReason Reason,
                        SrcMgr::CharacteristicKind FileType, FileID,
                        SourceLocation) override {
    switch (Reason) {
    case LexedFileChangeReason::EnterFile:
      inUserHeader.push(FileType == SrcMgr::CharacteristicKind::C_User);
      break;
    case LexedFileChangeReason::ExitFile:
      inUserHeader.pop();
      break;
    }
  }

  void InclusionDirective(SourceLocation HashLoc, const Token &,
                          StringRef FileName, bool, CharSourceRange,
                          OptionalFileEntryRef, StringRef, StringRef,
                          const Module *, bool,
                          SrcMgr::CharacteristicKind) override {
    if (!inUserHeader.top()) {
      return;
    }
    if (FileName.contains("nlohmann")) {
      auto buf = SM.getBufferData(SM.getFileID(HashLoc));
      buf = buf.substr(SM.getFileOffset(SM.getSpellingLoc(HashLoc)));
      buf = buf.take_until([](char c) { return c != '\r' && c != '\n'; });
      Marks->loc.push_back({HashLoc, FileName});
    }
  }
};
} // namespace

void ForbiddenIncludesCheck::registerMatchers(
    ast_matchers::MatchFinder *Finder) {
  Finder->addMatcher(translationUnitDecl(), this);
}

void ForbiddenIncludesCheck::registerPPCallbacks(const SourceManager &,
                                                 Preprocessor *PP,
                                                 Preprocessor *) {
  auto cb = std::make_unique<ForbiddenIncludesCallback>(*PP);
  marks = cb->Marks;
  PP->addPPCallbacks(std::move(cb));
}

void ForbiddenIncludesCheck::check(
    const ast_matchers::MatchFinder::MatchResult &) {
  for (auto loc : marks->loc) {
    diag(loc.loc, "don't include %0, use the lix wrapper header instead")
        << loc.name;
  }
}
}; // namespace nix::clang_tidy
