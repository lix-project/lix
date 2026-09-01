#include "lix/libcmd/built-path.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/result.hh"

#include <kj/async.h>

namespace nix {

#define CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, COMPARATOR) \
    bool MY_TYPE ::operator COMPARATOR (const MY_TYPE & other) const \
    { \
        const MY_TYPE* me = this; \
        auto fields1 = std::tie(me->drvPath, me->FIELD); \
        me = &other; \
        auto fields2 = std::tie(me->drvPath, me->FIELD); \
        return fields1 COMPARATOR fields2; \
    }
#define CMP(CHILD_TYPE, MY_TYPE, FIELD) \
    CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, ==) \
    CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, !=) \
    CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, <)

#define FIELD_TYPE std::map<std::string, StorePath>
CMP(SingleBuiltPath, BuiltPathBuilt, outputs)
#undef FIELD_TYPE

#undef CMP
#undef CMP_ONE

StorePathSet BuiltPath::outPaths() const
{
    return std::visit(
        overloaded{
            [](const BuiltPath::Opaque & p) { return StorePathSet{p.path}; },
            [](const BuiltPath::Built & b) {
                StorePathSet res;
                for (auto & [_, path] : b.outputs)
                    res.insert(path);
                return res;
            },
        }, raw()
    );
}

kj::Promise<Result<JSON>> BuiltPath::Built::toJSON(const Store & store) const
try {
    JSON res;
    res["drvPath"] = TRY_AWAIT(drvPath.toJSON(store));
    for (const auto & [outputName, outputPath] : outputs) {
        res["outputs"][outputName] = store.printStorePath(outputPath);
    }
    co_return res;
} catch (...) {
    co_return result::current_exception();
}
}
