#include "lix/libutil/environment-variables.hh"
#include "lix/libmain/loggers.hh"
#include "lix/libmain/progress-bar.hh"

namespace nix {

LogFormat defaultLogFormat = LogFormat::Auto;

[[deprecated]]
LogFormat parseLogFormat(const std::string & logFormatStr) {
    if (auto const parsed = LogFormat::parse(logFormatStr)) {
        return *parsed;
    }
    throw Error("setting 'log-format' has an invalid value '%s'", logFormatStr);
}

Logger * makeDefaultLogger() {
    return getLoggerByFormat(defaultLogFormat);
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

Logger * getLoggerByFormat(LogFormat logFormat)
{
    using enum LogFormatValue;
    switch (logFormat) {
    case LogFormat::Auto:
        return getLoggerByFormat(defaultLogFormat);
    case LogFormat::Raw:
        return makeSimpleLogger(false);
    case LogFormat::RawWithLogs:
        return makeSimpleLogger(true);
    case LogFormat::InternalJson:
        return makeJSONLogger(*makeSimpleLogger(true));
    case LogFormat::Bar:
        return makeProgressBar();
    case LogFormat::BarWithLogs: {
        auto logger = makeProgressBar();
        logger->setPrintBuildLogs(true);
        return logger;
    }
    case LogFormat::Multiline: {
        auto logger = makeProgressBar();
        logger->setPrintMultiline(true);
        return logger;
    }
    case LogFormat::MultilineWithLogs: {
        auto logger = makeProgressBar();
        logger->setPrintMultiline(true);
        logger->setPrintBuildLogs(true);
        return logger;
    }
    default:
        assert(false && "unreachable");
    }
}

}
