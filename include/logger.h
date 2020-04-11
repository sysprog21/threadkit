#ifndef DBG_H
#define DBG_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define clean_errno() (errno == 0 ? "None" : strerror(errno))

#define log_err(M, ...)                                                       \
    fprintf(stderr, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, \
            clean_errno(), ##__VA_ARGS__)

#define log_info(M, ...) \
    fprintf(stderr, "[INFO] (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define check(A, M, ...)                               \
    if (!(A)) {                                        \
        log_err(M "\n", ##__VA_ARGS__); /* exit(1); */ \
    }

#define check_exit(A, M, ...)           \
    if (!(A)) {                         \
        log_err(M "\n", ##__VA_ARGS__); \
        exit(1);                        \
    }

#define check_debug(A, M, ...)                       \
    if (!(A)) {                                      \
        debug(M "\n", ##__VA_ARGS__); /* exit(1); */ \
    }

#endif
