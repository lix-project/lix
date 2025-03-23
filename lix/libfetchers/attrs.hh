#pragma once
///@file

#include "lix/libutil/json-fwd.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/hash.hh"

#include <variant>

#include <optional>

namespace nix::fetchers {

typedef std::variant<std::string, uint64_t, Explicit<bool>> Attr;
typedef std::map<std::string, Attr> Attrs;

Attrs jsonToAttrs(const JSON & json);

JSON attrsToJSON(const Attrs & attrs);

std::optional<std::string> maybeGetStrAttr(const Attrs & attrs, const std::string & name);

std::string getStrAttr(const Attrs & attrs, const std::string & name);

std::optional<uint64_t> maybeGetIntAttr(const Attrs & attrs, const std::string & name);

uint64_t getIntAttr(const Attrs & attrs, const std::string & name);

std::optional<bool> maybeGetBoolAttr(const Attrs & attrs, const std::string & name);

bool getBoolAttr(const Attrs & attrs, const std::string & name);

std::map<std::string, std::string> attrsToQuery(const Attrs & attrs);

}
