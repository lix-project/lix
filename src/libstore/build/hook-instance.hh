#pragma once
///@file

#include "logging.hh"
#include "processes.hh"
#include "serialise.hh"

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
     * Pipe for the builder's standard output/error.
     */
    AutoCloseFD builderOut;

    /**
     * The process ID of the hook.
     */
    Pid pid;

    FdSink sink;

    std::map<ActivityId, Activity> activities;

    HookInstance();

    ~HookInstance();
};

}
