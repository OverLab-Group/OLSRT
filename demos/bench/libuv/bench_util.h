/**
 * @file bench_util.h
 * @brief Shared timing / memory helpers for the benchmark demos.
 *
 * No libuv dependency. Works on the OLSRT side and the libuv side
 * identically. All metrics printed by the demos are grouped under
 * a single "--- metrics ---" header so bench-run.sh can parse them
 * with grep/awk.
 */

#ifndef BENCH_UTIL_H
#define BENCH_UTIL_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

/* Monotonic nanosecond clock. */
static inline uint64_t bench_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Current RSS in KB from /proc/self/status. -1 on non-Linux. */
static inline long bench_rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long rss = -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %ld kB", &rss) == 1) break;
    }
    fclose(f);
    return rss;
}

/* User + system CPU milliseconds for the whole process. */
static inline void bench_cpu_ms(double *user_ms, double *sys_ms)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    *user_ms = ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0;
    *sys_ms  = ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;
}

/* Standard footer. Prints wall/cpu/rss and, if ops_name is non-NULL,
 * a normalized ops-per-second line. */
static inline void bench_footer(uint64_t start_ns, long long ops,
                                const char *ops_name)
{
    double wall_ms = (bench_now_ns() - start_ns) / 1e6;
    double u, s;
    bench_cpu_ms(&u, &s);

    printf("\n--- metrics ---\n");
    printf("wall_time_ms:       %.3f\n", wall_ms);
    printf("cpu_user_ms:        %.3f\n", u);
    printf("cpu_sys_ms:         %.3f\n", s);
    printf("rss_kb:             %ld\n", bench_rss_kb());
    if (ops_name && wall_ms > 0) {
        printf("ops_total:          %lld\n", ops);
        printf("ops_per_sec:        %.0f\n", ops / (wall_ms / 1000.0));
        printf("ops_name:           %s\n", ops_name);
    }
}

#endif /* BENCH_UTIL_H */
