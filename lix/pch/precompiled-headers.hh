#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/format.hpp>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

// This stuff is here to force the compiler to actually apply the extern
// template directives in all compilation units. To borrow a term, under
// complex microarchitectural conditions, clang ignores the extern template
// declaration, as revealed in the profile.
//
// In most cases, extern template works fine in the header itself. We don't
// have any idea why this happens.

// Here because of all the regexes everywhere (it is infeasible to block instantiation everywhere)
// For some reason this does not actually prevent the instantiation of
// regex::_M_compile, and the regex compiler (my interpretation of what this is
// supposed to do is make the template bits out-of-line), but it *does* prevent
// a bunch of codegen of regex stuff, which seems to save about 30s on-cpu.
// Instantiated in libutil/regex.cc.
extern template class std::basic_regex<char>;
