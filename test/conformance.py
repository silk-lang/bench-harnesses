#!/usr/bin/env python3
"""Checks that a harness speaks the driven protocol and prints result lines by hand.

Usage: conformance.py <program>...

Each program must be the example of its harness: benchmarks `Hash/n=1000` and
`Hash/n=100000`, with the FNV-1a checks below.
"""
import os
import re
import subprocess
import sys

NAMES = ["BenchmarkHash/n=1000", "BenchmarkHash/n=100000"]
CHECKS = {NAMES[0]: "10992378149551695325", NAMES[1]: "14180411078876222789"}
RESULT = re.compile(r"^(\S+) (\d+) (\d+\.\d\d) ns/op$")


def fail(program, text):
    sys.exit(f"{program}: {text}")


def driven(program):
    env = dict(os.environ, BENCH_DRIVE="1")
    child = subprocess.Popen(
        [program], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env, text=True
    )

    def ask(command):
        child.stdin.write(command + "\n")
        child.stdin.flush()
        lines = []
        while True:
            line = child.stdout.readline()
            if line == "":
                fail(program, f"no `done` after `{command}`: {lines}")
            if line.rstrip("\n") == "done":
                return lines
            lines.append(line.rstrip("\n"))

    if ask("list") != NAMES:
        fail(program, "`list` did not give the two names")
    for name in NAMES:
        for count in (1, 250):
            answer = ask(f"run {name} {count}")
            if len(answer) != 2 or answer[0] != f"check: {CHECKS[name]}":
                fail(program, f"wrong answer to a run of {name}: {answer}")
            line = RESULT.match(answer[1])
            if not line or line.group(1) != name or int(line.group(2)) != count:
                fail(program, f"wrong result line: {answer[1]}")
            if float(line.group(3)) <= 0:
                fail(program, f"no time in: {answer[1]}")
    for command in ("run BenchmarkMissing 10", f"run {NAMES[0]} 0", f"run {NAMES[0]}", "dance"):
        answer = ask(command)
        if len(answer) != 1 or not answer[0].startswith("error: "):
            fail(program, f"`{command}` should answer one error line: {answer}")
    # An empty line gets no answer.
    child.stdin.write("\nquit\n")
    child.stdin.flush()
    if child.wait(timeout=10) != 0:
        fail(program, "`quit` did not end the harness with status 0")

    # The end of the input ends the harness too.
    ended = subprocess.run([program], stdin=subprocess.DEVNULL, capture_output=True, env=env)
    if ended.returncode != 0 or ended.stdout != b"":
        fail(program, "the end of the input should end the harness quietly")


def by_hand(program):
    env = {key: value for key, value in os.environ.items() if key != "BENCH_DRIVE"}
    out = subprocess.run([program], capture_output=True, env=env, text=True, timeout=120)
    if out.returncode != 0:
        fail(program, f"status {out.returncode} when started by hand")
    lines = out.stdout.splitlines()
    if len(lines) != 40:
        fail(program, f"expected 10 samples of 2 benchmarks, each with a check: {len(lines)} lines")
    for at, name in enumerate(NAMES):
        counts = set()
        for sample in range(10):
            check, result = lines[at * 20 + sample * 2 : at * 20 + sample * 2 + 2]
            line = RESULT.match(result)
            if check != f"check: {CHECKS[name]}" or not line or line.group(1) != name:
                fail(program, f"wrong sample: {check!r} {result!r}")
            counts.add(line.group(2))
            total = int(line.group(2)) * float(line.group(3))
            if total < 25e6:
                fail(program, f"a sample should take about 50 ms, took {total / 1e6:.1f} ms")
        if len(counts) != 1:
            fail(program, f"the samples of {name} differ in iterations: {counts}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for program in sys.argv[1:]:
        driven(program)
        by_hand(program)
        print(f"ok {program}")


if __name__ == "__main__":
    main()
