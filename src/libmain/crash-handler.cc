#include "crash-handler.hh"
#include "fmt.hh"

#include <boost/core/demangle.hpp>
#include <exception>

namespace nix {

namespace {
void onTerminate()
{
    std::cerr << "Lix crashed. This is a bug. We would appreciate if you report it along with what caused it at https://git.lix.systems/lix-project/lix/issues with the following information included:\n\n";
    try {
        std::exception_ptr eptr = std::current_exception();
        if (eptr) {
            std::rethrow_exception(eptr);
        } else {
            std::cerr << "std::terminate() called without exception\n";
        }
    } catch (const std::exception & ex) {
        std::cerr << "Exception: " << boost::core::demangle(typeid(ex).name()) << ": " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "Unknown exception! Spooky.\n";
    }

    std::cerr << "Stack trace:\n";
    nix::printStackTrace();

    std::abort();
}
}

void registerCrashHandler()
{
    // DO NOT use this for signals. Boost stacktrace is very much not
    // async-signal-safe, and in a world with ASLR, addr2line is pointless.
    //
    // If you want signals, set up a minidump system and do it out-of-process.
    std::set_terminate(onTerminate);
}
}
