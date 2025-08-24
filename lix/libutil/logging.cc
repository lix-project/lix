#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/config.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/position.hh"
#include "lix/libutil/terminal.hh"
#include "manually-drop.hh"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <sstream>
#include <syslog.h>

namespace nix {

LoggerSettings loggerSettings;

static GlobalConfig::Register rLoggerSettings(&loggerSettings);

Logger * logger = makeSimpleLogger(true);

void Logger::writeToStdout(std::string_view s)
{
    writeFull(
        STDOUT_FILENO,
        filterANSIEscapes(
            s,
            !shouldANSI(StandardOutputStream::Stdout),
            std::numeric_limits<unsigned int>::max(),
            false
        )
    );
    writeFull(STDOUT_FILENO, "\n");
}

class SimpleLogger : public Logger
{
public:

    bool systemd, tty;
    bool printBuildLogs;

    SimpleLogger(bool printBuildLogs)
        : printBuildLogs(printBuildLogs)
    {
        systemd = getEnv("IN_SYSTEMD") == "1";
        tty = shouldANSI();
    }

    bool isVerbose() override {
        return printBuildLogs;
    }

    void log(Verbosity lvl, std::string_view s) override
    {
        if (lvl > verbosity) return;

        std::string prefix;

        if (systemd) {
            char c;
            switch (lvl) {
            case lvlError: c = '3'; break;
            case lvlWarn: c = '4'; break;
            case lvlNotice: case lvlInfo: c = '5'; break;
            case lvlTalkative: case lvlChatty: c = '6'; break;
            case lvlDebug: case lvlVomit:
            default: c = '7'; break; // default case should not happen, and missing enum case is reported by -Werror=switch-enum
            }
            prefix = std::string("<") + c + ">";
        }

        writeLogsToStderr(prefix + filterANSIEscapes(s, !tty) + "\n");
    }

    void logEI(const ErrorInfo & ei) override
    {
        std::stringstream oss;
        showErrorInfo(oss, ei, loggerSettings.showTrace.get());

        log(ei.level, oss.str());
    }

    void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent)
        override
    {
        if (lvl <= verbosity && !s.empty())
            log(lvl, s + "...");
    }

    void result(ActivityId act, ResultType type, const Fields & fields) override
    {
        if (type == resBuildLogLine && printBuildLogs) {
            auto lastLine = fields[0].s;
            printError("%1%", Uncolored(lastLine));
        }
        else if (type == resPostBuildLogLine) {
            auto lastLine = fields[0].s;
            printError("post-build-hook: %1%", Uncolored(lastLine));
        }
    }
};

Verbosity verbosity = lvlInfo;

Verbosity verbosityFromIntClamped(int val)
{
    int clamped = std::clamp(val, int(lvlError), int(lvlVomit));
    return static_cast<Verbosity>(clamped);
}

Logger * makeSimpleLogger(bool printBuildLogs)
{
    return new SimpleLogger(printBuildLogs);
}

std::atomic<uint64_t> nextId{0};

Activity::Activity(Logger & logger, Verbosity lvl, ActivityType type,
    const std::string & s, const Logger::Fields & fields, ActivityId parent)
    : logger(logger), id(nextId++ + (((uint64_t) getpid()) << 32))
{
    logger.startActivity(id, lvl, type, s, fields, parent);
}

void to_json(JSON & json, std::shared_ptr<Pos> pos)
{
    if (pos) {
        json["line"] = pos->line;
        json["column"] = pos->column;
        std::ostringstream str;
        pos->print(str, true);
        json["file"] = str.str();
    } else {
        json["line"] = nullptr;
        json["column"] = nullptr;
        json["file"] = nullptr;
    }
}

struct JSONLogger : Logger {
    Logger & prevLogger;

    JSONLogger(Logger & prevLogger) : prevLogger(prevLogger) { }

    bool isVerbose() override {
        return true;
    }

    void addFields(JSON & json, const Fields & fields)
    {
        if (fields.empty()) return;
        auto & arr = json["fields"] = JSON::array();
        for (auto & f : fields)
            if (f.type == Logger::Field::tInt)
                arr.push_back(f.i);
            else if (f.type == Logger::Field::tString)
                arr.push_back(f.s);
            else
                abort();
    }

    void write(const JSON & json)
    {
        prevLogger.log(lvlError, "@nix " + json.dump(-1, ' ', false, JSON::error_handler_t::replace));
    }

    void log(Verbosity lvl, std::string_view s) override
    {
        JSON json;
        json["action"] = "msg";
        json["level"] = lvl;
        json["msg"] = s;
        write(json);
    }

    void logEI(const ErrorInfo & ei) override
    {
        std::ostringstream oss;
        showErrorInfo(oss, ei, loggerSettings.showTrace.get());

        JSON json;
        json["action"] = "msg";
        json["level"] = ei.level;
        json["msg"] = oss.str();
        json["raw_msg"] = ei.msg.str();
        to_json(json, ei.pos);

        if (loggerSettings.showTrace.get() && !ei.traces.empty()) {
            JSON traces = JSON::array();
            for (auto iter = ei.traces.rbegin(); iter != ei.traces.rend(); ++iter) {
                JSON stackFrame;
                stackFrame["raw_msg"] = iter->hint.str();
                to_json(stackFrame, iter->pos);
                traces.push_back(stackFrame);
            }

            json["trace"] = traces;
        }

        write(json);
    }

    void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent) override
    {
        JSON json;
        json["action"] = "start";
        json["id"] = act;
        json["level"] = lvl;
        json["type"] = type;
        json["text"] = s;
        json["parent"] = parent;
        addFields(json, fields);
        write(json);
    }

    void stopActivity(ActivityId act) override
    {
        JSON json;
        json["action"] = "stop";
        json["id"] = act;
        write(json);
    }

    void result(ActivityId act, ResultType type, const Fields & fields) override
    {
        JSON json;
        json["action"] = "result";
        json["id"] = act;
        json["type"] = type;
        addFields(json, fields);
        write(json);
    }
};

Logger * makeJSONLogger(Logger & prevLogger)
{
    return new JSONLogger(prevLogger);
}

static Logger::Fields getFields(JSON & json)
{
    Logger::Fields fields;
    for (auto & f : json) {
        if (f.type() == JSON::value_t::number_unsigned)
            fields.emplace_back(Logger::Field(f.get<uint64_t>()));
        else if (f.type() == JSON::value_t::string)
            fields.emplace_back(Logger::Field(f.get<std::string>()));
        else throw Error("unsupported JSON type %d", (int) f.type());
    }
    return fields;
}

std::optional<JSON> parseJSONMessage(const std::string & msg, std::string_view source)
{
    if (!msg.starts_with("@nix ")) return std::nullopt;
    try {
        return json::parse(std::string(msg, 5));
    } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
        printError("bad JSON log message from %s: %s",
            Uncolored(source),
            e.what());
    }
    return std::nullopt;
}

bool handleJSONLogMessage(JSON & json,
    const Activity & act, std::map<ActivityId, Activity> & activities,
    std::string_view source, bool trusted)
{
    try {
        std::string action = json["action"];

        if (action == "start") {
            auto type = (ActivityType) json["type"];
            if (trusted || type == actFileTransfer)
                activities.emplace(std::piecewise_construct,
                    std::forward_as_tuple(json["id"]),
                    std::forward_as_tuple(*logger, (Verbosity) json["level"], type,
                        json["text"], getFields(json["fields"]), act.id));
        }

        else if (action == "stop")
            activities.erase((ActivityId) json["id"]);

        else if (action == "result") {
            auto i = activities.find((ActivityId) json["id"]);
            if (i != activities.end())
                i->second.result((ResultType) json["type"], getFields(json["fields"]));
        }

        else if (action == "setPhase") {
            std::string phase = json["phase"];
            act.result(resSetPhase, phase);
        }

        else if (action == "msg") {
            std::string msg = json["msg"];
            logger->log((Verbosity) json["level"], msg);
        }

        return true;
    } catch (JSON::exception &e) { // NOLINT(lix-foreign-exceptions)
        printTaggedWarning(
            "Unable to handle a JSON message from %s: %s", Uncolored(source), e.what()
        );
        return false;
    }
}

bool handleJSONLogMessage(const std::string & msg,
    const Activity & act, std::map<ActivityId, Activity> & activities, std::string_view source, bool trusted)
{
    auto json = parseJSONMessage(msg, source);
    if (!json) return false;

    return handleJSONLogMessage(*json, act, activities, source, trusted);
}

Activity::~Activity()
{
    try {
        logger.stopActivity(id);
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void writeLogsToStderr(std::string_view s)
{
    // NOTE: If this lock is a regular static item (and not something
    // indestructible), then it will be destructed when Nix shuts down. When
    // other threads are running, it becomes possible for a static to be
    // destructed before Nix ends, leading to errors.
    //
    // Therefore, we use a wrapper type to block it ever getting destroyed.
    //
    // TODO: Audit other statics for this issue?
    //
    // See: https://git.lix.systems/lix-project/lix/issues/702
    // See: https://stackoverflow.com/a/27671727/5719760
    static ManuallyDrop<std::mutex> lock {std::in_place_t{}};

    // make sure only one thread uses this function at any given time.
    // multiple concurrent threads can have deleterious effects on log
    // output, especially when layering structured formats (like JSON)
    // on top of a SimpleLogger which is itself not thread-safe. every
    // Logger instance should be thread-safe in an ideal world, but we
    // cannot really enforce that on a per-logger level at this point.
    std::unique_lock _lock(*lock);
    try {
        writeFull(STDERR_FILENO, s, false);
    } catch (SysError & e) {
        /* Ignore failing writes to stderr.  We need to ignore write
           errors to ensure that cleanup code that logs to stderr runs
           to completion if the other side of stderr has been closed
           unexpectedly. */
    }
}

void logFatal(std::string const & s)
{
    writeLogsToStderr(s + "\n");
    // std::string for guaranteed null termination
    syslog(LOG_CRIT, "%s", s.c_str());
}

}
