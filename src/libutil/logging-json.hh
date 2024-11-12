#pragma once
///@file logging-json.hh
///
///@brief Logging functions for json specifically, split due to the cost of
///including nlohmann.

#include "lix/libutil/logging.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

std::optional<nlohmann::json> parseJSONMessage(const std::string & msg);

bool handleJSONLogMessage(nlohmann::json & json,
    const Activity & act, std::map<ActivityId, Activity> & activities,
    bool trusted);

bool handleJSONLogMessage(const std::string & msg,
    const Activity & act, std::map<ActivityId, Activity> & activities,
    bool trusted);

};
