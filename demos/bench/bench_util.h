/**
 * @file bench_util.h
 * @brief Shared metrics helper. Used by both sides (OLSRT and libuv)
 * so the outputs are byte-identical in structure.
 */

#ifndef BENCH_UTIL_H
#define BENCH_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static inline uint64_t bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline long bench_rss_kb(void) {
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

static inline void bench_cpu_ms(double *u, double *s) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    *u = ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0;
    *s = ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;
}

static inline void bench_footer(uint64_t t0, long long ops, const char *opname) {
    double wall = (bench_now_ns() - t0) / 1e6;
    double u, s;
    bench_cpu_ms(&u, &s);
    printf("\n--- metrics ---\n");
    printf("wall_time_ms:       %.3f\n", wall);
    printf("cpu_user_ms:        %.3f\n", u);
    printf("cpu_sys_ms:         %.3f\n", s);
    printf("rss_kb:             %ld\n", bench_rss_kb());
    printf("ops_total:          %lld\n", ops);
    printf("ops_per_sec:        %.0f\n", wall > 0 ? ops / (wall / 1000.0) : 0.0);
    printf("ops_name:           %s\n", opname);
}

#endif
