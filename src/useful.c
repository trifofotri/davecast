#include <time.h>
#include <useful.h>

unsigned long long dch_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000ull + t.tv_nsec / 1000000ull;
}