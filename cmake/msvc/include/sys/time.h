/* The part of sys/time.h that quickjs.c uses, for MSVC (see CMakeLists.txt). gettimeofday reads the same UTC clock as
   pthread.h. */
#ifndef AMK_MSVC_SYS_TIME_H
#define AMK_MSVC_SYS_TIME_H

#include <windows.h>

#include <time.h>

#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif

// FILETIME counts 100 ns ticks from 1601-01-01. The Unix epoch is 11644473600 seconds later.
static inline int gettimeofday(struct timeval* tv, void* zone) {
    FILETIME file_time;
    ULARGE_INTEGER ticks;
    (void)zone;
    GetSystemTimeAsFileTime(&file_time);
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    unsigned long long since_epoch = ticks.QuadPart - 116444736000000000ULL;
    tv->tv_sec = (long)(since_epoch / 10000000ULL);
    tv->tv_usec = (long)((since_epoch % 10000000ULL) / 10);
    return 0;
}

#endif  // AMK_MSVC_SYS_TIME_H
