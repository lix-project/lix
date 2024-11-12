#include "lix/libutil/environment-variables.hh"
#include "lix/libmain/loggers.hh"
#include "lix/libmain/progress-bar.hh"

namespace nix {

LogFormat defaultLogFormat = LogFormat::raw;

LogFormat parseLogFormat(const std::string & logFormatStr) {
    if (logFormatStr == "raw")
        return LogFormat::raw;
    else if (logFormatStr == "raw-with-logs")
        return LogFormat::rawWithLogs;
    else if (logFormatStr == "internal-json")
        return LogFormat::internalJSON;
    else if (logFormatStr == "bar")
        return LogFormat::bar;
    else if (logFormatStr == "bar-with-logs")
        return LogFormat::barWithLogs;
    else if (logFormatStr == "multiline")
        return LogFormat::multiline;
    else if (logFormatStr == "multiline-with-logs")
        return LogFormat::multilineWithLogs;
    throw Error("option 'log-format' has an invalid value '%s'", logFormatStr);
}

Logger * makeDefaultLogger() {
    switch (defaultLogFormat) {
    case LogFormat::raw:
        return makeSimpleLogger(false);
    case LogFormat::rawWithLogs:
        return makeSimpleLogger(true);
    case LogFormat::internalJSON:
        return makeJSONLogger(*makeSimpleLogger(true));
    case LogFormat::bar:
        return makeProgressBar();
    case LogFormat::barWithLogs: {
        auto logger = makeProgressBar();
        logger->setPrintBuildLogs(true);
        return logger;
    }
    case LogFormat::multiline: {
        auto logger = makeProgressBar();
        logger->setPrintMultiline(true);
        return logger;
    }
    case LogFormat::multilineWithLogs: {
        auto logger = makeProgressBar();
        logger->setPrintMultiline(true);
        logger->setPrintBuildLogs(true);
        return logger;
    }
    default:
        abort();
    }
}

void setLogFormat(const std::string & logFormatStr) {
    setLogFormat(parseLogFormat(logFormatStr));
}

void setLogFormat(const LogFormat & logFormat) {
    defaultLogFormat = logFormat;
    createDefaultLogger();
}

void createDefaultLogger() {
    logger = makeDefaultLogger();
}

}
