#pragma once
///@file

#include "lix/libutil/args.hh"
#include "lix/libutil/repair-flag.hh"

namespace nix {

static constexpr auto loggingCategory = "Logging-related options";
static constexpr auto miscCategory = "Miscellaneous global options";

class MixCommonArgs : public virtual Args
{
    void initialFlagsProcessed() override;
public:
    std::string programName;
    MixCommonArgs(const std::string & programName);
protected:
    virtual void pluginsInited() {}
};

struct MixDryRun : virtual Args
{
    bool dryRun = false;

    MixDryRun()
    {
        addFlag({
            .longName = "dry-run",
            .description = "Show what this command would do without doing it.",
            .handler = {&dryRun, true},
        });
    }
};

struct MixJSON : virtual Args
{
    bool json = false;

    MixJSON()
    {
        addFlag({
            .longName = "json",
            .description = "Produce output in JSON format, suitable for consumption by another program.",
            .handler = {&json, true},
        });
    }
};

struct MixRepair : virtual Args
{
    RepairFlag repair = NoRepair;

    MixRepair()
    {
        addFlag({
            .longName = "repair",
            .description =
                "During evaluation, rewrite missing or corrupted files in the Nix store. "
                "During building, rebuild missing or corrupted store paths.",
            .category = miscCategory,
            .handler = {&repair, Repair},
        });
    }
};

}
