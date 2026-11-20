# bench-harnesses

Reference harnesses that let a C, Rust or Go program take part in a Silk benchmark suite
(`silk.bench`) as a driven implementation, next to a Silk program that uses
`silk.bench.harness`.

A driven implementation is a program that stays alive while the benchmark task asks it for
one sample at a time, in turns with the other implementations. That keeps a slow minute of
the computer from falling on one language, and it lets the task pick the iteration counts.

| Language | Harness | Example |
| --- | --- | --- |
| C | [`c/bench.h`](c/bench.h), one header | [`c/example.c`](c/example.c) |
| Rust | [`rust/`](rust/src/lib.rs), a crate without dependencies | [`rust/examples/example.rs`](rust/examples/example.rs) |
| Go | [`go/`](go/harness.go), a package without dependencies | [`go/example/main.go`](go/example/main.go) |
| Silk | `silk.bench.harness` in the standard library | [`example/src/main.silk`](example/src/main.silk) |

## Writing a benchmark

Every harness has the same shape. Give it the names of the benchmarks, then loop: each time
`next` returns true, run the body of the named benchmark exactly `iterations` times.

```c
#include "bench.h"

int main(void) {
    static const char *names[] = {"Hash/n=1000", "Hash/n=100000"};
    bench_harness harness;
    bench_open(&harness, names, 2);
    while (bench_next(&harness)) {
        uint64_t length = bench_position(&harness) == 0 ? 1000 : 100000;
        uint64_t total = 0;
        for (uint64_t left = bench_iterations(&harness); left > 0; left--) {
            total = bench_keep(hash(bench_keep(length)));
        }
        bench_check_number(&harness, total);
    }
    return 0;
}
```

The harness times from the return of `next` to the next call.

| Operation | C | Rust | Go |
| --- | --- | --- | --- |
| Open | `bench_open` | `Harness::open` | `harness.Open` |
| Next run | `bench_next` | `next` | `Next` |
| Which benchmark | `bench_name`, `bench_position` | `name`, `position` | `Name`, `Position` |
| How many times | `bench_iterations` | `iterations` | `Iterations` |
| Leave setup out | `bench_reset`, `bench_stop`, `bench_start` | `reset`, `stop`, `start` | `Reset`, `Stop`, `Start` |
| Report what was computed | `bench_check`, `bench_check_number` | `check`, `check_number` | `Check`, `CheckNumber` |
| Add a value in another unit | `bench_value` | `value` | `Value` |
| Report a failed run | `bench_failed` | `failed` | `Failed` |
| Keep the compiler honest | `bench_keep` | `keep` | `harness.Keep` |

Two rules make a comparison fair:

- Give the result of each iteration, and its input, to `keep`. Otherwise a compiler may
  remove the body or run it once.
- Report a check that does not depend on the iteration count. The task refuses to compare
  implementations whose checks differ, which catches a port that does different work.

## Running a harness

Started by a benchmark task, a harness answers the driven protocol on standard input and
output. The task sets the environment variable `BENCH_DRIVE`.

```text
task     list
harness  BenchmarkHash/n=1000
harness  BenchmarkHash/n=100000
harness  done
task     run BenchmarkHash/n=1000 2000
harness  check: 10992378149551695325
harness  BenchmarkHash/n=1000 2000 1190.56 ns/op
harness  done
task     quit
```

Started by hand, the same program finds an iteration count that takes about 50 ms for each
benchmark, takes 10 samples, and prints them as result lines in the format of Go's `testing`
package. `benchstat` reads them, and so does a suite that declares the program with
`Implementation.resultLines`.

On the wire a name is `Benchmark`, then the name with a first lowercase letter in uppercase
and `_` for each space.

## The example suite

[`example/`](example) is a Silk package whose `bench` task drives all four programs:

```sh
make                       # builds the C, Rust and Go examples into example/bin
cd example
silk task bench --optimization speed
```

```text
Hash/n=1000  5 runs, reference c, ns/op
  implementation   median     95% interval  fastest  vs c
  silk            1.20 µs  [1.19, 1.23] µs  1.19 µs  1.00x [0.99, 1.02] no difference
  c               1.20 µs  [1.19, 1.20] µs  1.19 µs  reference
  rust            1.20 µs  [1.07, 1.24] µs  1.07 µs  1.00x [0.89, 1.03] no difference
  go              1.21 µs  [1.19, 1.21] µs  1.19 µs  1.01x [0.99, 1.02] no difference
```

It needs a Silk release that has `silk.bench` and `Harness.keep`.

## Testing a harness

`make test` builds the three examples and runs [`test/conformance.py`](test/conformance.py)
on each. The script checks the protocol (names, runs, checks, errors, quitting) and the
result lines a program prints by hand. A harness for another language passes if its example
program implements the same two `Hash` benchmarks.
