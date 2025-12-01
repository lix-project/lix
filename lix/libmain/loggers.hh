#pragma once
///@file

#include "lix/libutil/log-format.hh"
#include "lix/libutil/types.hh"

namespace nix {

class Logger;

[[deprecated]]
void setLogFormat(const std::string & logFormatStr);
[[deprecated]]
void setLogFormat(const LogFormat & logFormat);

void createDefaultLogger();

Logger * getLoggerByFormat(LogFormat logFormat);

}
