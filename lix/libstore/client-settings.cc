#include "lix/libstore/client-settings.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/config.hh"

#include <set>

namespace nix {
void ClientSettings::apply(TrustedFlag trusted)
{
    setVerbosity(verbosity);

    for (auto & i : overrides) {
        auto & name(i.first);
        auto & value(i.second);

        auto setSubstituters = [&](Setting<Strings> & res) {
            if (name != res.name && res.aliases.count(name) == 0) {
                return false;
            }
            StringSet trusted = settings.trustedSubstituters;
            for (auto & s : settings.substituters.get()) {
                trusted.insert(s);
            }
            Strings subs;
            auto ss = tokenizeString<Strings>(value);
            for (auto & s : ss) {
                if (trusted.count(s)) {
                    subs.push_back(s);
                } else if (!s.ends_with("/") && trusted.count(s + "/")) {
                    subs.push_back(s + "/");
                } else {
                    printTaggedWarning(
                        "ignoring untrusted substituter '%s', you are not a trusted user.\n"
                        "Run `man nix.conf` for more information on the `substituters` "
                        "configuration option.",
                        s
                    );
                }
            }
            res.override(subs);
            return true;
        };

        try {
            if (name == "ssh-auth-sock" // obsolete
                || name == "store") // the daemon *is* the store
                ;
            else if (name == experimentalFeatureSettings.experimentalFeatures.name) {
                // We don’t want to forward the experimental features to
                // the daemon, as that could cause some pretty weird stuff
                if (parseFeatures(tokenizeString<StringSet>(value))
                    != experimentalFeatureSettings.experimentalFeatures.get())
                {
                    debug("Ignoring the client-specified experimental features");
                }
            } else if (name == settings.pluginFiles.name) {
                if (tokenizeString<Paths>(value) != settings.pluginFiles.get()) {
                    printTaggedWarning(
                        "Ignoring the client-specified plugin-files.\n"
                        "The client specifying plugins to the daemon never made sense, and was "
                        "removed in Nix."
                    );
                }
            } else if (trusted || name == settings.buildTimeout.name || name == settings.maxSilentTime.name
                       || name == settings.pollInterval.name || name == settings.keepFailed.name
                       || name == settings.keepGoing.name || name == settings.tryFallback.name
                       || name == settings.maxBuildJobs.name || name == settings.buildCores.name
                       || name == settings.useSubstitutes.name
                       || name == fileTransferSettings.maxConnectTimeout.name
                       || fileTransferSettings.initialConnectTimeout.isNameOrAlias(name)
                       || (name == "builders" && value == ""))
            {
                settings.set(name, value);
            } else if (setSubstituters(settings.substituters))
                ;
            else {
                printTaggedWarning(
                    "Ignoring the client-specified setting '%s', because it is a restricted "
                    "setting and you are not a trusted user",
                    name
                );
            }
        } catch (UsageError & e) {
            printTaggedWarning("%1%", Uncolored(e.what()));
        }
    }
}
}
