#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>

int main() {
    struct rlimit lim;

    int res = getrlimit(RLIMIT_STACK, &lim);
    if (res == -1) {
        return errno;
    }
    printf("stack size: %lu\n", lim.rlim_cur);

    res = getrlimit(RLIMIT_NPROC, &lim);
    if (res == -1) {
        return errno;
    }
    printf("process limit: %lu\n", lim.rlim_cur);

    res = getrlimit(RLIMIT_NOFILE, &lim);
    if (res == -1) {
        return errno;
    }
    printf("max file descriptors: %lu\n", lim.rlim_cur);

    return 0;
}
