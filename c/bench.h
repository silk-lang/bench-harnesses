/*
 * A harness for C benchmarks that a Silk benchmark task drives.
 *
 * Include this header in one program, give the harness the names of the benchmarks, and loop:
 *
 *     static const char *names[] = {"Hash/n=1000", "Hash/n=100000"};
 *     bench_harness harness;
 *     bench_open(&harness, names, 2);
 *     while (bench_next(&harness)) {
 *         for (uint64_t left = bench_iterations(&harness); left > 0; left--) {
 *             ...the body of bench_name(&harness)...
 *         }
 *         bench_check_number(&harness, total);
 *     }
 *
 * Started by a benchmark task (BENCH_DRIVE is set), the harness answers the driven protocol
 * on standard input and output. Started by hand, it finds an iteration count that takes about
 * 50 ms for each benchmark, takes 10 samples, and prints them as result lines.
 *
 * The harness times from the return of bench_next to the next call.
 */
#ifndef SILK_BENCH_H
#define SILK_BENCH_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BENCH_MAX_VALUES 8
#define BENCH_TEXT 256
#define BENCH_SAMPLES 10
#define BENCH_MOST 1000000000ull

typedef struct bench_harness {
    const char *const *names;
    size_t length;
    int driven;
    int active;
    size_t current;
    uint64_t count;
    int timing;
    struct timespec began;
    uint64_t spent;
    size_t values;
    double amounts[BENCH_MAX_VALUES];
    char units[BENCH_MAX_VALUES][32];
    int checked;
    char check[BENCH_TEXT];
    int failed;
    char failure[BENCH_TEXT];
    int calibrating;
    uint64_t sampled;
    uint64_t target;
} bench_harness;

static inline uint64_t bench_since(const struct timespec *from) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)((int64_t)(now.tv_sec - from->tv_sec) * 1000000000
                      + ((int64_t)now.tv_nsec - (int64_t)from->tv_nsec));
}

/* Writes the name as it goes over the wire: `Benchmark`, a first lowercase letter in
 * uppercase, and `_` for each space. */
static inline void bench_wire(const char *name, char *out, size_t room) {
    size_t at = (size_t)snprintf(out, room, "Benchmark");
    for (size_t i = 0; name[i] != 0 && at + 1 < room; i++) {
        char byte = name[i];
        if (i == 0 && byte >= 'a' && byte <= 'z') {
            byte = (char)(byte - 32);
        } else if (byte == ' ' || byte == '\t') {
            byte = '_';
        }
        out[at++] = byte;
    }
    out[at] = 0;
}

/* Prepares a harness for the benchmarks `names`. `driven` says whether a task drives it. */
static inline void bench_make(bench_harness *h, const char *const *names, size_t length,
                              int driven) {
    memset(h, 0, sizeof *h);
    h->names = names;
    h->length = length;
    h->driven = driven;
    h->count = 1;
    h->calibrating = 1;
    h->target = 50000000;
}

/* Prepares a harness that is driven when a benchmark task started the program. */
static inline void bench_open(bench_harness *h, const char *const *names, size_t length) {
    const char *asked = getenv("BENCH_DRIVE");
    bench_make(h, names, length, asked != NULL && asked[0] != 0);
}

/* Sets how long a sample should take when nobody drives the harness, in milliseconds. */
static inline void bench_sample(bench_harness *h, uint64_t milliseconds) {
    h->target = milliseconds * 1000000;
}

/* The name of the benchmark to run, as it was given to the harness. */
static inline const char *bench_name(const bench_harness *h) { return h->names[h->current]; }

/* The position of the benchmark to run in the names the harness was given. */
static inline size_t bench_position(const bench_harness *h) { return h->current; }

/* How many times to run the body. */
static inline uint64_t bench_iterations(const bench_harness *h) { return h->count; }

/* Starts the timing again from zero, after setup that should not count. */
static inline void bench_reset(bench_harness *h) {
    h->spent = 0;
    clock_gettime(CLOCK_MONOTONIC, &h->began);
    h->timing = 1;
}

/* Stops the timing, until bench_start. */
static inline void bench_stop(bench_harness *h) {
    if (h->timing) {
        h->spent += bench_since(&h->began);
        h->timing = 0;
    }
}

/* Starts the timing again after bench_stop. */
static inline void bench_start(bench_harness *h) {
    if (!h->timing) {
        clock_gettime(CLOCK_MONOTONIC, &h->began);
        h->timing = 1;
    }
}

/* Gives the harness the time of the run, for a body that timed itself. */
static inline void bench_measured(bench_harness *h, uint64_t nanoseconds) {
    h->spent = nanoseconds;
    h->timing = 0;
}

/* Says that the run failed, with the reason on one line. */
static inline void bench_failed(bench_harness *h, const char *text) {
    h->failed = 1;
    snprintf(h->failure, sizeof h->failure, "%s", text);
}

/* Reports what the run computed, such as a checksum. A task does not compare
 * implementations whose checks differ. */
static inline void bench_check(bench_harness *h, const char *text) {
    h->checked = 1;
    snprintf(h->check, sizeof h->check, "%s", text);
}

static inline void bench_check_number(bench_harness *h, uint64_t number) {
    h->checked = 1;
    snprintf(h->check, sizeof h->check, "%" PRIu64, number);
}

/* Adds a value in another unit to the result line, such as 1024 `B/op`. */
static inline void bench_value(bench_harness *h, const char *unit, double amount) {
    if (h->values < BENCH_MAX_VALUES) {
        snprintf(h->units[h->values], sizeof h->units[0], "%s", unit);
        h->amounts[h->values] = amount;
        h->values++;
    }
}

/* Keeps the compiler from removing the work that computed `value`. */
static inline uint64_t bench_keep(uint64_t value) {
    volatile uint64_t kept = value;
    return kept;
}

static inline uint64_t bench_grown(uint64_t iterations, uint64_t elapsed, uint64_t target) {
    double factor = 100.0;
    if (elapsed > 0) {
        factor = (double)target / (double)elapsed * 1.2;
    }
    if (factor < 2.0) factor = 2.0;
    if (factor > 100.0) factor = 100.0;
    double next = ceil((double)iterations * factor);
    return next > (double)BENCH_MOST ? BENCH_MOST : (uint64_t)next;
}

static inline void bench_begin(bench_harness *h) {
    h->values = 0;
    h->checked = 0;
    h->failed = 0;
    h->active = 1;
    h->spent = 0;
    h->timing = 1;
    clock_gettime(CLOCK_MONOTONIC, &h->began);
}

static inline void bench_answer(bench_harness *h) {
    char wire[BENCH_TEXT];
    if (h->failed) {
        printf("error: %s\n", h->failure);
        return;
    }
    if (h->checked) {
        printf("check: %s\n", h->check);
    }
    bench_wire(h->names[h->current], wire, sizeof wire);
    printf("%s %" PRIu64 " %.2f ns/op", wire, h->count, (double)h->spent / (double)h->count);
    for (size_t at = 0; at < h->values; at++) {
        printf(" %.2f %s", h->amounts[at], h->units[at]);
    }
    printf("\n");
}

static inline void bench_advance(bench_harness *h) {
    h->current++;
    h->calibrating = 1;
    h->sampled = 0;
    h->count = 1;
}

/* Finishes the run in progress, if there is one, and returns whether there is another run
 * to do. After 1, run the body of bench_name bench_iterations times and call it again. */
static inline int bench_next(bench_harness *h) {
    char line[1024];
    char wire[BENCH_TEXT];
    if (h->active) {
        bench_stop(h);
        h->active = 0;
        if (h->driven) {
            bench_answer(h);
            printf("done\n");
            fflush(stdout);
        } else if (h->failed) {
            bench_answer(h);
            bench_advance(h);
        } else if (h->calibrating) {
            if (h->spent >= h->target || h->count >= BENCH_MOST) {
                h->calibrating = 0;
            } else {
                h->count = bench_grown(h->count, h->spent, h->target);
            }
        } else {
            bench_answer(h);
            h->sampled++;
            if (h->sampled == BENCH_SAMPLES) {
                bench_advance(h);
            }
        }
    }
    if (!h->driven) {
        if (h->current >= h->length) {
            fflush(stdout);
            return 0;
        }
        bench_begin(h);
        return 1;
    }
    while (fgets(line, sizeof line, stdin) != NULL) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'
                              || line[length - 1] == ' ')) {
            line[--length] = 0;
        }
        char *command = line;
        while (*command == ' ') command++;
        if (strcmp(command, "quit") == 0) {
            return 0;
        }
        if (strcmp(command, "list") == 0) {
            for (size_t at = 0; at < h->length; at++) {
                bench_wire(h->names[at], wire, sizeof wire);
                printf("%s\n", wire);
            }
        } else if (strncmp(command, "run ", 4) == 0) {
            /* `run <name> <iterations>`: a name holds no space. */
            char *rest = command + 4;
            char *space = strrchr(rest, ' ');
            size_t found = h->length;
            uint64_t count = 0;
            if (space != NULL) {
                *space = 0;
                for (size_t at = 0; at < h->length; at++) {
                    bench_wire(h->names[at], wire, sizeof wire);
                    if (strcmp(wire, rest) == 0) found = at;
                }
                char *end = NULL;
                count = strtoull(space + 1, &end, 10);
                if (end == space + 1 || *end != 0) count = 0;
            }
            if (found < h->length && count > 0) {
                h->current = found;
                h->count = count;
                bench_begin(h);
                return 1;
            }
            printf("error: no such benchmark, or no iteration count\n");
        } else if (command[0] != 0) {
            printf("error: unknown command\n");
        } else {
            continue;
        }
        printf("done\n");
        fflush(stdout);
    }
    return 0;
}

#endif
