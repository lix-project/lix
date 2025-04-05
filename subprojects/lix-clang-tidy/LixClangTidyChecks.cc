#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>
#include "DisallowedDecls.hh"
#include "ForeignExceptions.hh"
#include "HasPrefixSuffix.hh"
#include "CharPtrCast.hh"
#include "NeverAsync.hh"

namespace nix::clang_tidy {
using namespace clang;
using namespace clang::tidy;

class NixClangTidyChecks : public ClangTidyModule {
    public:
        void addCheckFactories(ClangTidyCheckFactories &CheckFactories) override {
            CheckFactories.registerCheck<HasPrefixSuffixCheck>("lix-hasprefixsuffix");
            CheckFactories.registerCheck<CharPtrCastCheck>("lix-charptrcast");
            CheckFactories.registerCheck<NeverAsync>("lix-never-async");
            CheckFactories.registerCheck<DisallowedDeclsCheck>("lix-disallowed-decls");
            CheckFactories.registerCheck<ForeignExceptions>("lix-foreign-exceptions");
        }
};

static ClangTidyModuleRegistry::Add<NixClangTidyChecks> X("lix-module", "Adds lix specific checks");
};
