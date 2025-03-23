#pragma once
///@file

#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/config.hh"

#include <map>
#include <limits>

#include <sys/types.h>

namespace nix {

typedef enum { smEnabled, smRelaxed, smDisabled } SandboxMode;

void to_json(JSON & j, const SandboxMode & e);
void from_json(const JSON & j, SandboxMode & e);

struct MaxBuildJobsSetting : public BaseSetting<unsigned int>
{
    MaxBuildJobsSetting(Config * options,
        unsigned int def,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {},
        const bool documentDefault = true,
        std::optional<ExperimentalFeature> experimentalFeature = std::nullopt,
        bool deprecated = false)
        : BaseSetting<unsigned int>(def, true, name, description, aliases, experimentalFeature, deprecated)
    {
        options->addSetting(this);
    }

    unsigned int parse(const std::string & str, const ApplyConfigOptions & options) const override;
};

struct PluginFilesSetting : public BaseSetting<Paths>
{
    bool pluginsLoaded = false;

    PluginFilesSetting(Config * options,
        const Paths & def,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {},
        const bool documentDefault = true,
        std::optional<ExperimentalFeature> experimentalFeature = std::nullopt,
        bool deprecated = false)
        : BaseSetting<Paths>(def, true, name, description, aliases, experimentalFeature, deprecated)
    {
        options->addSetting(this);
    }

    Paths parse(const std::string & str, const ApplyConfigOptions & options) const override;
};

const uint32_t maxIdsPerBuild =
    #if __linux__
    1 << 16
    #else
    1
    #endif
    ;

class Settings : public Config {

    unsigned int getDefaultCores();

    StringSet getDefaultSystemFeatures();

    StringSet getDefaultExtraPlatforms();

    bool isWSL1();

    Path getDefaultSSLCertFile();

public:

    Settings();

    Path nixPrefix;

    /**
     * The directory where we store sources and derived files.
     */
    Path nixStore;

    Path nixDataDir; /* !!! fix */

    /**
     * The directory where we log various operations.
     */
    Path nixLogDir;

    /**
     * The directory where state is stored.
     */
    Path nixStateDir;

    /**
     * The directory where system configuration files are stored.
     */
    Path nixConfDir;

    /**
     * A list of user configuration files to load.
     */
    std::vector<Path> nixUserConfFiles;

    /**
     * The directory where the main programs are stored.
     */
    Path nixBinDir;

    /**
     * The directory where the man pages are stored.
     */
    Path nixManDir;

    /**
     * File name of the socket the daemon listens to.
     */
    Path nixDaemonSocketFile;

    /**
     * Whether to show build log output in real time.
     */
    bool verboseBuild = true;

    /**
     * Read-only mode.  Don't copy stuff to the store, don't change
     * the database.
     */
    bool readOnlyMode = false;

    #include "libstore-settings.gen.inc"
};


// FIXME: don't use a global variable.
extern Settings settings;

/**
 * This should be called after settings are initialized, but before
 * anything else
 */
void initPlugins();

void loadConfFile();

// Used by the Settings constructor
std::vector<Path> getUserConfigFiles();
std::vector<Path> getHomeConfigFile();

extern const std::string nixVersion;

/**
 * NB: This is not sufficient. You need to call initNix()
 */
void initLibStore();

/**
 * It's important to initialize before doing _anything_, which is why we
 * call upon the programmer to handle this correctly. However, we only add
 * this in a key locations, so as not to litter the code.
 */
void assertLibStoreInitialized();

}
