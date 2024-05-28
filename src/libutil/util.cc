#include "util.hh"
#include "processes.hh"
#include "strings.hh"
#include "current-process.hh"

#include "sync.hh"
#include "finally.hh"
#include "serialise.hh"
#include "cgroup.hh"
#include "signals.hh"
#include "environment-variables.hh"
#include "file-system.hh"

#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/syscall.h>
#include <mach-o/dyld.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include <cmath>
#endif


#ifdef NDEBUG
#error "Lix may not be built with assertions disabled (i.e. with -DNDEBUG)."
#endif

namespace nix {


//////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////




//////////////////////////////////////////////////////////////////////





void ignoreException(Verbosity lvl)
{
    /* Make sure no exceptions leave this function.
       printError() also throws when remote is closed. */
    try {
        try {
            throw;
        } catch (std::exception & e) {
            printMsg(lvl, "error (ignored): %1%", e.what());
        }
    } catch (...) { }
}


//////////////////////////////////////////////////////////////////////



}
