#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>
#include "HasPrefixSuffix.hh"

namespace nix::clang_tidy {
using namespace clang;
using namespace clang::tidy;

class NixClangTidyChecks : public ClangTidyModule {
    public:
        void addCheckFactories(ClangTidyCheckFactories &CheckFactories) override {
            CheckFactories.registerCheck<HasPrefixSuffixCheck>("nix-hasprefixsuffix");
        }
};

static ClangTidyModuleRegistry::Add<NixClangTidyChecks> X("nix-module", "Adds nix specific checks");
};
