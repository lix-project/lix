#include "lix/libutil/environment-variables.hh"
#include "lix/libmain/loggers.hh"
#include "lix/libmain/progress-bar.hh"
#include "lix/libutil/log-format.hh"
#include "lix/libutil/config-impl.hh" // IWYU pragma: keep

namespace nix {

static Logger * makeDefaultLogger() {
    return getLoggerByFormat(loggerSettings.logFormat);
}

void setLogFormat(const LogFormat & logFormat) {
    loggerSettings.logFormat.override(logFormat);
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
        return getLoggerByFormat(loggerSettings.logFormat.autoValue);
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
