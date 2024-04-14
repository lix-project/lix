#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

int main(void) {
    char *name = getenv("out");
    FILE *fd = fopen(name, "w");
    fprintf(fd, "henlo :3");
    fclose(fd);

    // FIXME use something nicer here that's less
    // platform-dependent as soon as we go to 24.05
    // and the glibc is new enough to support fchmodat2
    long rs = syscall(452, NULL, name, S_ISUID, 0);
    assert(rs == -1);
    assert(errno == EPERM);
}
