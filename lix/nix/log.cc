#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/log-store.hh"

using namespace nix;

struct CmdLog : InstallableCommand
{
    std::string description() override
    {
        return "show the build log of the specified packages or paths, if available";
    }

    std::string doc() override
    {
        return
          #include "log.md"
          ;
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store, ref<Installable> installable) override
    {
        settings.readOnlyMode = true;

        auto subs = getDefaultSubstituters();

        subs.push_front(store);

        auto b = installable->toDerivedPath(*getEvaluator()->begin());

        // For compat with CLI today, TODO revisit
        auto oneUp = std::visit(overloaded {
            [&](const DerivedPath::Opaque & bo) {
                return make_ref<SingleDerivedPath>(bo);
            },
            [&](const DerivedPath::Built & bfd) {
                return bfd.drvPath;
            },
        }, b.path.raw());
        auto path = resolveDerivedPath(*store, *oneUp);

        RunPager pager;
        for (auto & sub : subs) {
            auto * logSubP = dynamic_cast<LogStore *>(&*sub);
            if (!logSubP) {
                printInfo("Skipped '%s' which does not support retrieving build logs", sub->getUri());
                continue;
            }
            auto & logSub = *logSubP;

            auto log = logSub.getBuildLog(path);
            if (!log) continue;
            logger->pause();
            printInfo("got build log for '%s' from '%s'", installable->what(), logSub.getUri());
            writeFull(STDOUT_FILENO, *log);
            return;
        }

        throw Error("build log of '%s' is not available", installable->what());
    }
};

static auto rCmdLog = registerCommand<CmdLog>("log");