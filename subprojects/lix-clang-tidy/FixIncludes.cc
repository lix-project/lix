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
    SourceLocation, const Token &, StringRef FileName, bool IsAngled,
    CharSourceRange FilenameRange, OptionalFileEntryRef File, StringRef,
    StringRef, const Module *, SrcMgr::CharacteristicKind) {
  if (Ignore)
    return;

  // FIXME: this is kinda evil, but this is a one-time fixup
  const std::vector<std::string> SourceDirs = {"src/", "include/lix/"};

  const auto Bracketize = [IsAngled](StringRef s) {
    return IsAngled ? ("<" + s + ">").str() : ("\"" + s + "\"").str();
  };

  for (const auto &SourceDir : SourceDirs) {
    // Ignore generated files, since they are often only used internally within
    // a library anyhow and they are not in the normal source dir.
    const bool IsAlreadyFixed = FileName.starts_with("lix/lib") || FileName.contains(".gen.") || FileName.ends_with(".md");
    if (File && File->getNameAsRequested().contains(SourceDir) &&
        !IsAlreadyFixed) {
      StringRef Name = File->getNameAsRequested();
      auto Idx = Name.find(SourceDir);
      assert(Idx != std::string::npos);
      std::string Suffix = Name.drop_front(Idx + SourceDir.length()).str();

      if (!Suffix.starts_with("lib")) {
        llvm::dbgs() << "ignored: " << Suffix << "\n";
        return;
      }

      Suffix = "lix/" + Suffix;

      auto Diag = Check.diag(FilenameRange.getBegin(),
                             "include needs to specify the source subdir");

      Diag << FilenameRange
           << FixItHint::CreateReplacement(FilenameRange, Bracketize(Suffix));
    }
  }
}

void FixIncludesCheck::registerPPCallbacks(const SourceManager &,
                                           Preprocessor *PP, Preprocessor *) {
  PP->addPPCallbacks(std::make_unique<FixIncludesCallbacks>(*this, *PP));
}

}; // namespace nix::clang_tidy
