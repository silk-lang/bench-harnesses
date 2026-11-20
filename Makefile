# Builds the example program of each harness into example/bin, where the example suite
# expects them.
all: example/bin/c example/bin/rust example/bin/go

example/bin/c: c/example.c c/bench.h
	mkdir -p example/bin
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror -o $@ c/example.c -lm

example/bin/rust: rust/examples/example.rs rust/src/lib.rs
	mkdir -p example/bin
	cd rust && cargo build --quiet --release --example example
	cp rust/target/release/examples/example $@

example/bin/go: go/example/main.go go/harness.go
	mkdir -p example/bin
	cd go && go build -o ../$@ ./example

test: all
	python3 test/conformance.py example/bin/c example/bin/rust example/bin/go

clean:
	rm -rf example/bin rust/target

.PHONY: all test clean
