#pragma once
///@file

#include <chrono>
#include <thread>

#include "lix/libutil/logging.hh"
#include "lix/libutil/sync.hh"

namespace nix {

struct ProgressBar : public Logger
{
    struct ActInfo
    {
        using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

        std::string s, lastLine, phase;
        ActivityType type = actUnknown;
        uint64_t done = 0;
        uint64_t expected = 0;
        uint64_t running = 0;
        uint64_t failed = 0;
        std::map<ActivityType, uint64_t> expectedByType;
        bool visible = true;
        ActivityId parent;
        std::optional<std::string> name;
        TimePoint startTime;
    };

    struct ActivitiesByType
    {
        std::map<ActivityId, std::list<ActInfo>::iterator> its;
        uint64_t done = 0;
        uint64_t expected = 0;
        uint64_t failed = 0;
    };

    struct State
    {
        std::list<ActInfo> activities;
        std::map<ActivityId, std::list<ActInfo>::iterator> its;

        std::map<ActivityType, ActivitiesByType> activitiesByType;

        int lastLines = 0;

        uint64_t filesLinked = 0, bytesLinked = 0;

        uint64_t corruptedPaths = 0, untrustedPaths = 0;

        uint32_t paused = 1;
        bool haveUpdate = false;
    };

    Sync<State> state_;

    std::thread updateThread;

    std::condition_variable quitCV, updateCV;

    bool printBuildLogs = false;
    bool printMultiline = false;
    bool isTTY;

    ProgressBar(bool isTTY);

    ~ProgressBar();

    void pause() override;

    void resetProgress() override;

    void resume() override;

    bool isVerbose() override;

    void log(Verbosity lvl, std::string_view s) override;

    void logEI(const ErrorInfo & ei) override;

    void log(State & state, Verbosity lvl, std::string_view s);

    void startActivity(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        const Fields & fields,
        ActivityId parent
    ) override;

    bool hasAncestor(State & state, ActivityType type, ActivityId act);

    void stopActivity(ActivityId act) override;

    void result(ActivityId act, ResultType type, const std::vector<Field> & fields) override;

    void update(State & state);

    std::string getStatus(State & state);

    void writeToStdout(std::string_view s) override;

    std::optional<char> ask(std::string_view msg) override;

    void setPrintBuildLogs(bool printBuildLogs) override;

    void setPrintMultiline(bool printMultiline) override;

private:
    void eraseProgressDisplay(State & state);

    std::chrono::milliseconds restoreProgressDisplay(State & state);
};

Logger * makeProgressBar();

}
