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

    long rs = syscall(SYS_fchmodat2, NULL, name, S_ISUID, 0);
    assert(rs == -1);
    assert(errno == EPERM);
}
