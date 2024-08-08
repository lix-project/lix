#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>
#include "FixIncludes.hh"
#include "HasPrefixSuffix.hh"
#include "CharPtrCast.hh"

namespace nix::clang_tidy {
using namespace clang;
using namespace clang::tidy;

class NixClangTidyChecks : public ClangTidyModule {
    public:
        void addCheckFactories(ClangTidyCheckFactories &CheckFactories) override {
            CheckFactories.registerCheck<HasPrefixSuffixCheck>("lix-hasprefixsuffix");
            CheckFactories.registerCheck<FixIncludesCheck>("lix-fixincludes");
            CheckFactories.registerCheck<CharPtrCastCheck>("lix-charptrcast");
        }
};

static ClangTidyModuleRegistry::Add<NixClangTidyChecks> X("lix-module", "Adds lix specific checks");
};
