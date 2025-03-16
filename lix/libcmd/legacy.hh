#pragma once
///@file

#include "lix/libutil/async.hh"
#include <functional>
#include <list>
#include <map>
#include <string>

namespace nix {

typedef std::function<void(AsyncIoRoot &, std::string, std::list<std::string>)> MainFunction;

struct LegacyCommandRegistry
{
    using LegacyCommandMap = std::map<std::string, MainFunction>;
    static LegacyCommandMap * commands;

    static void add(const std::string & name, MainFunction fun)
    {
        if (!commands) commands = new LegacyCommandMap;
        (*commands)[name] = fun;
    }
};

}
