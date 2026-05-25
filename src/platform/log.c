#include "log.h"
#include <stdio.h>

_Safe void log_request(const char* _Nonnull method, const char* _Nonnull path, int status) {
    _Unsafe { fprintf(stderr, "%s %s -> %d\n", method, path, status); }
}

_Safe void log_msg(const char* _Nonnull msg) {
    _Unsafe { fprintf(stderr, "%s\n", msg); }
}
