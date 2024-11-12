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

MySettings mySettings;

static GlobalConfig::Register rs(&mySettings);

[[gnu::used, gnu::unused, gnu::retain]]
static void maybeRequireMeowForDlopen() {
    meow();
}

static void prim_anotherNull (EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    if (mySettings.settingSet)
        v.mkNull();
    else
        v.mkBool(false);
}

static RegisterPrimOp rp({
    .name = "anotherNull",
    .arity = 0,
    .fun = prim_anotherNull,
});
