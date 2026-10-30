/* A header-only subset of POSIX threads for MSVC, used only by the QuickJS Atomics code (see CMakeLists.txt).
   Mutexes are SRWLOCKs taken exclusively, condition variables are CONDITION_VARIABLEs, and the clock is UTC.
   It covers what quickjs.c calls and nothing more. */
#ifndef AMK_MSVC_PTHREAD_H
#define AMK_MSVC_PTHREAD_H

#include <windows.h>

#include <errno.h>
#include <time.h>

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT

// The current UTC time. GetSystemTimeAsFileTime counts 100 ns ticks from 1601-01-01.
static inline void amk_utc_now(struct timespec* ts) {
    FILETIME file_time;
    ULARGE_INTEGER ticks;
    GetSystemTimeAsFileTime(&file_time);
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    unsigned long long since_epoch = ticks.QuadPart - 116444736000000000ULL;  // 1601 to 1970
    ts->tv_sec = (time_t)(since_epoch / 10000000ULL);
    ts->tv_nsec = (long)((since_epoch % 10000000ULL) * 100);
}

static inline int amk_clock_gettime(int clock_id, struct timespec* ts) {
    (void)clock_id;
    amk_utc_now(ts);
    return 0;
}

// quickjs.c calls clock_gettime, which MSVC does not provide. The name is changed here so that it cannot clash.
#define clock_gettime(clock_id, ts) amk_clock_gettime(clock_id, ts)

static inline int pthread_mutex_lock(pthread_mutex_t* mutex) {
    AcquireSRWLockExclusive(mutex);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    ReleaseSRWLockExclusive(mutex);
    return 0;
}

static inline int pthread_cond_init(pthread_cond_t* cond, const void* attributes) {
    (void)attributes;
    InitializeConditionVariable(cond);
    return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t* cond) {
    (void)cond;
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t* cond) {
    WakeConditionVariable(cond);
    return 0;
}

static inline int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    return SleepConditionVariableSRW(cond, mutex, INFINITE, 0) ? 0 : EINVAL;
}

// `absolute` is a UTC time, as quickjs.c computes it with clock_gettime(CLOCK_REALTIME).
static inline int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                                         const struct timespec* absolute) {
    struct timespec now;
    amk_utc_now(&now);
    long long remaining = ((long long)absolute->tv_sec - (long long)now.tv_sec) * 1000 +
                          (absolute->tv_nsec - now.tv_nsec) / 1000000;
    if (remaining < 0) remaining = 0;
    if (SleepConditionVariableSRW(cond, mutex, (DWORD)remaining, 0)) return 0;
    return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
}

#endif  // AMK_MSVC_PTHREAD_H
