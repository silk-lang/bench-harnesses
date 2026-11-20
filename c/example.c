#include "bench.h"

/* FNV-1a over the numbers below `length`. */
static uint64_t hash(uint64_t length) {
    uint64_t total = 14695981039346656037ull;
    for (uint64_t at = 0; at < length; at++) {
        total = (total ^ at) * 1099511628211ull;
    }
    return total;
}

int main(void) {
    static const char *names[] = {"Hash/n=1000", "Hash/n=100000"};
    bench_harness harness;
    bench_open(&harness, names, 2);
    while (bench_next(&harness)) {
        uint64_t length = bench_position(&harness) == 0 ? 1000 : 100000;
        uint64_t total = 0;
        for (uint64_t left = bench_iterations(&harness); left > 0; left--) {
            /* One iteration: the work whose time is reported. */
            total = bench_keep(hash(bench_keep(length)));
        }
        bench_check_number(&harness, total);
    }
    return 0;
}
