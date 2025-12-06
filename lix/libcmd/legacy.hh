#pragma once
///@file

#include "lix/libutil/async.hh"
#include <functional>
#include <list>
#include <map>
#include <span>
#include <string>

namespace nix {

struct LegacyCommandRegistry
{
    typedef std::function<int(AsyncIoRoot &, std::string, std::list<std::string>)> MainFunction;
    typedef std::function<
        int(AsyncIoRoot &, std::string, std::list<std::string>, std::span<char *>)>
        RawMainFunction;

    using LegacyCommandMap = std::map<std::string, RawMainFunction>;
    static LegacyCommandMap * commands;

    static void add(const std::string & name, MainFunction fun)
    {
        addWithRaw(
            name,
            [fun](AsyncIoRoot & aio, std::string name, std::list<std::string> args, std::span<char *>) {
                return fun(aio, name, args);
            }
        );
    }

    static void addWithRaw(const std::string & name, RawMainFunction fun)
    {
        if (!commands) commands = new LegacyCommandMap;
        (*commands)[name] = fun;
    }
};

}
