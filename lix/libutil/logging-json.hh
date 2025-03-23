#pragma once
///@file logging-json.hh
///
///@brief Logging functions for json specifically, split due to the cost of
///including nlohmann.

#include "lix/libutil/json-fwd.hh"
#include "lix/libutil/logging.hh"

namespace nix {

/**
 * @param source A noun phrase describing the source of the message, e.g. "the builder".
 */
std::optional<JSON> parseJSONMessage(const std::string & msg, std::string_view source);

/**
 * @param source A noun phrase describing the source of the message, e.g. "the builder".
 */
bool handleJSONLogMessage(JSON & json,
    const Activity & act, std::map<ActivityId, Activity> & activities,
    std::string_view source,
    bool trusted);

/**
 * @param source A noun phrase describing the source of the message, e.g. "the builder".
 */
bool handleJSONLogMessage(const std::string & msg,
    const Activity & act, std::map<ActivityId, Activity> & activities,
    std::string_view source,
    bool trusted);

};
