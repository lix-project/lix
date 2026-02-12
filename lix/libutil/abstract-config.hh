#pragma once
///@file

#include "lix/libutil/apply-config-options.hh"
#include "lix/libutil/json-fwd.hh"
#include "lix/libutil/types.hh"

#include <string>

namespace nix {

class Args;
class AbstractSetting;

class AbstractConfig
{
    // Types.
public:
    struct SettingInfo
    {
        std::string value;
        std::string description;
    };

    // Fields.
protected:
    StringMap unknownSettings;

    // Specials.
protected:
    AbstractConfig(StringMap initials = {});

    // Abstract methods.
public:
    /**
     * Sets the value referenced by `name` to `value`. Returns true if the
     * setting is known, false otherwise.
     */
    virtual bool set(
        const std::string & name,
        const std::string & value,
        const ApplyConfigOptions & options = {}
    ) = 0;

    /**
     * Adds the currently known settings to the given result map `res`.
     * - res: map to store settings in
     * - overriddenOnly: when set to true only overridden settings will be added to `res`
     */
    virtual void getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly = false) = 0;

    /**
     * Adds the currently known settings to the given result map `res` *if* they
     * have non-default values.  This differs from overridden settings retrieved
     * by `getSettings` in that this function returns all settings having a non-
     * default value while overridden-ness can be reset using `resetOverridden`.
     */
    virtual void getChangedSettings(std::map<std::string, SettingInfo> & res) = 0;

    /**
     * Resets the `overridden` flag of all Settings
     */
    virtual void resetOverridden() = 0;

    /**
     * Outputs all settings to JSON
     * - out: JSONObject to write the configuration to
     */
    virtual JSON toJSON() = 0;

    /**
     * Converts settings to `Args` to be used on the command line interface
     * - args: args to write to
     * - category: category of the settings
     */
    virtual void convertToArgs(Args & args, const std::string & category) = 0;

    // Provided methods.
public:
    /**
     * Parses the configuration in `contents` and applies it
     * - contents: configuration contents to be parsed and applied
     * - path: location of the configuration file
     */
    void applyConfig(const std::string & contents, const ApplyConfigOptions & options = {});

    /**
     * Logs a warning for each unregistered setting
     */
    void warnUnknownSettings();

    /**
     * Re-applies all previously attempted changes to unknown settings
     */
    void reapplyUnknownSettings();
};

}
