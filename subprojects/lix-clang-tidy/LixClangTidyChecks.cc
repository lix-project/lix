#include "CharPtrCast.hh"
#include "DisallowedDecls.hh"
#include "ForeignExceptions.hh"
#include "HasPrefixSuffix.hh"
#include "NeverAsync.hh"
#include "UnsafeCCalls.hh"
#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

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
            CheckFactories.registerCheck<UnsafeCCalls>("lix-unsafe-c-calls");
        }
};

static ClangTidyModuleRegistry::Add<NixClangTidyChecks> X("lix-module", "Adds lix specific checks");
};
