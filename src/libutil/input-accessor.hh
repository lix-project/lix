#pragma once
///@file


#include <map>
#include <optional>
#include <string>

namespace nix {

struct InputAccessor
{
    enum Type {
      tRegular, tSymlink, tDirectory,
      /**
        Any other node types that may be encountered on the file system, such as device nodes, sockets, named pipe, and possibly even more exotic things.

        Responsible for `"unknown"` from `builtins.readFileType "/dev/null"`.

        Unlike `DT_UNKNOWN`, this must not be used for deferring the lookup of types.
      */
      tMisc
    };

    struct Stat
    {
        Type type = tMisc;
        //uint64_t fileSize = 0; // regular files only
        bool isExecutable = false; // regular files only
    };

    typedef std::optional<Type> DirEntry;

    typedef std::map<std::string, DirEntry> DirEntries;
};

}
