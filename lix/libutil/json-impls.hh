#pragma once
///@file

#include "lix/libutil/json-fwd.hh"

// Following https://github.com/nlohmann/json#how-can-i-use-get-for-non-default-constructiblenon-copyable-types
#define JSON_IMPL(TYPE)                                                \
    namespace nlohmann {                                               \
        using namespace nix;                                           \
        template <>                                                    \
        struct adl_serializer<TYPE> {                                  \
            static TYPE from_json(const JSON & json);                  \
            static void to_json(JSON & json, TYPE t);                  \
        };                                                             \
    }
