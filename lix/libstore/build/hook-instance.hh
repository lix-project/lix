#pragma once
///@file

#include "lix/libutil/logging.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/serialise.hh"

namespace nix {

struct HookInstance
{
    /**
     * Pipe for talking to the build hook.
     */
    AutoCloseFD toHook;

    /**
     * Pipe for the hook's standard output/error.
     */
    AutoCloseFD fromHook;

    /**
     * The process ID of the hook.
     */
    Pid pid;

    std::unique_ptr<FdSink> sink;

    std::map<ActivityId, Activity> activities;

    HookInstance();

    ~HookInstance();
};

}
