#pragma once
///@file

#include "lix/libutil/config.hh"
#include "lix/libutil/json.hh"

namespace nix {
template<typename T>
std::map<std::string, JSON> BaseSetting<T>::toJSONObject() const
{
    auto obj = AbstractSetting::toJSONObject();
    obj.emplace("value", value);
    obj.emplace("defaultValue", defaultValue);
    obj.emplace("documentDefault", documentDefault);
    return obj;
}
}
