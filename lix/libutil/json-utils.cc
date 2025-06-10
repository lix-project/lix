#include "lix/libutil/json.hh"
#include "lix/libutil/error.hh"

namespace nix {

const JSON * get(const JSON & map, const std::string & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &*i;
}

JSON * get(JSON & map, const std::string & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &*i;
}

const JSON & valueAt(
    const JSON & map,
    const std::string & key)
{
    if (!map.contains(key))
        throw Error("Expected JSON object to contain key '%s' but it doesn't", key);

    return map[key];
}

const JSON & ensureType(
    const JSON & value,
    JSON::value_type expectedType
    )
{
    if (value.type() != expectedType)
        throw Error(
            "Expected JSON value to be of type '%s' but it is of type '%s'",
            JSON(expectedType).type_name(),
            value.type_name());

    return value; // NOLINT(bugprone-return-const-ref-from-parameter)
}
}
