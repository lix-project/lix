#include "FixIncludes.hh"
#include <clang-tidy/ClangTidyCheck.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Debug.h>
#include <memory>
#include <set>
#include <string>

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::tidy;

class FixIncludesCallbacks : public PPCallbacks {
public:
  ClangTidyCheck &Check;
  Preprocessor &PP;
  FixIncludesCallbacks(ClangTidyCheck &Check, Preprocessor &PP)
      : Check(Check), PP(PP) {}

private:
  bool Ignore = false;
  virtual void LexedFileChanged(FileID FID, LexedFileChangeReason Reason,
                                SrcMgr::CharacteristicKind FileType,
                                FileID PrevFID, SourceLocation Loc) override;

  virtual void InclusionDirective(SourceLocation HashLoc,
                                  const Token &IncludeTok, StringRef FileName,
                                  bool IsAngled, CharSourceRange FilenameRange,
                                  OptionalFileEntryRef File,
                                  StringRef SearchPath, StringRef RelativePath,
                                  const Module *Imported,
                                  SrcMgr::CharacteristicKind FileType) override;
};

void FixIncludesCallbacks::LexedFileChanged(FileID, LexedFileChangeReason,
                                            SrcMgr::CharacteristicKind FileType,
                                            FileID, SourceLocation) {
  Ignore = FileType != SrcMgr::C_User;
}

void FixIncludesCallbacks::InclusionDirective(
    SourceLocation, const Token &, StringRef, bool,
    CharSourceRange FilenameRange, OptionalFileEntryRef File, StringRef,
    StringRef, const Module *, SrcMgr::CharacteristicKind) {
  if (Ignore)
    return;

  // FIXME: this is kinda evil, but this is a one-time fixup
  const std::string SourceDir = "src/";

  if (File && File->getNameAsRequested().contains(SourceDir)) {
    StringRef Name = File->getNameAsRequested();
    auto Idx = Name.find(SourceDir);
    assert(Idx != std::string::npos);
    StringRef Suffix = Name.drop_front(Idx + SourceDir.length());

    if (!Suffix.starts_with("lib")) {
      llvm::dbgs() << "ignored: " << Suffix << "\n";
      return;
    }

    auto Diag = Check.diag(FilenameRange.getBegin(),
                           "include needs to specify the source subdir");

    Diag << FilenameRange
         << FixItHint::CreateReplacement(FilenameRange,
                                         ("\"" + Suffix + "\"").str());
  }
}

void FixIncludesCheck::registerPPCallbacks(const SourceManager &,
                                           Preprocessor *PP, Preprocessor *) {
  PP->addPPCallbacks(std::make_unique<FixIncludesCallbacks>(*this, *PP));
}

}; // namespace nix::clang_tidy
