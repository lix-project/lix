#include "lix/libutil/config.hh"
#include "lix/libexpr/primops.hh"
#include <stdlib.h>

using namespace nix;

#ifdef MISSING_REFERENCE
extern void meow();
#else
#define meow() {}
#endif

struct MySettings : Config
{
    Setting<bool> settingSet{this, false, "setting-set",
        "Whether the plugin-defined setting was set"};
};

bool entryCalled = false;

MySettings mySettings;

[[gnu::used, gnu::unused, gnu::retain]]
static void maybeRequireMeowForDlopen() {
    meow();
}

static Value prim_anotherNull(EvalState & state, Value ** args)
{
    assert(entryCalled);
    if (mySettings.settingSet)
        return Value::VNULL;
    else
        return {NewValueAs::boolean, false};
}

extern "C" void nix_plugin_entry()
{
    PluginPrimOps::add({
        .name = "anotherNull",
        .arity = 0,
        .fun = prim_anotherNull,
    });
    GlobalConfig::registerGlobalConfig(mySettings);
    entryCalled = true;
}
