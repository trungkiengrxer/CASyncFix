// How much slower does one thread's own CPU time get under background load? Two kernels, each reported as work per
// second of THREAD CPU time (so being descheduled does not count, only running slower does):
//   alu: integer arithmetic in registers     -> sensitive to core frequency and to a busy hyperthread sibling
//   mem: blend over a 96 MB buffer (render-like) -> also sensitive to cache and memory bandwidth contention
// Usage: ./tools/cpucontend [seconds=6]      Build: clang -O2 -o tools/cpucontend tools/cpucontend.c
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double cpuNow(void) {
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    double secs = argc > 1 ? atof(argv[1]) : 6;
    size_t n = 96u << 20;
    uint8_t *a = malloc(n), *b = malloc(n);
    memset(a, 1, n); memset(b, 2, n);
    volatile uint64_t sink = 0;

    double t0 = cpuNow(), t = t0; uint64_t alu = 0, x = 88172645463325252ULL;
    while ((t = cpuNow()) - t0 < secs / 2) {
        for (int i = 0; i < 2000000; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; }
        alu++;
    }
    double aluRate = alu / (t - t0);
    sink += x;

    t0 = cpuNow(); uint64_t mem = 0;
    while ((t = cpuNow()) - t0 < secs / 2) {
        for (size_t i = 0; i < n; i++) a[i] = (uint8_t)((a[i] * 3 + b[i]) >> 2);
        mem++;
    }
    double memRate = mem / (t - t0);
    sink += a[n / 2];
    printf("alu %.1f /cpu-s   mem %.2f /cpu-s\n", aluRate, memRate);
    return 0;
}
