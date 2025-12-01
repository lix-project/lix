#pragma once
///@file

#include "lix/libutil/log-format.hh"
#include "lix/libutil/types.hh"

namespace nix {

class Logger;

/** Overrides the current log format, and re-creates the current logger. */
void setLogFormat(const LogFormat & logFormat);

void createDefaultLogger();

Logger * getLoggerByFormat(LogFormat logFormat);

}
