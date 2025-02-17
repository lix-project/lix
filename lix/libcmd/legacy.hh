#pragma once
///@file

#include "lix/libutil/async.hh"
#include <functional>
#include <list>
#include <map>
#include <string>

namespace nix {

typedef std::function<void(AsyncIoRoot &, std::string, std::list<std::string>)> MainFunction;

struct LegacyCommands
{
    typedef std::map<std::string, MainFunction> Commands;
    static Commands * commands;

    static void add(const std::string & name, MainFunction fun)
    {
        if (!commands) commands = new Commands;
        (*commands)[name] = fun;
    }
};

}
