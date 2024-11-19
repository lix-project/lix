#include <pthread.h>
#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <pthread_np.h>
#endif

namespace nix {

void setCurrentThreadName(const char * name)
{
    // https://stackoverflow.com/questions/2369738/how-to-set-the-name-of-a-thread-in-linux-pthreads/7989973
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__FreeBSD__) || defined(__OpenBSD__)
    pthread_set_name_np(pthread_self(), name);
#endif
}

}
