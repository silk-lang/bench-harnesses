// Package harness is a harness for Go benchmarks that a Silk benchmark task drives.
//
// Give the harness the names of the benchmarks and loop on Next. Each time it returns true,
// run the body of Name exactly Iterations times. The harness times from the return of Next to
// the next call.
//
//	h := harness.Open("Hash/n=1000", "Hash/n=100000")
//	for h.Next() {
//		var total uint64
//		for left := h.Iterations(); left > 0; left-- {
//			total = hash(1000)
//		}
//		h.CheckNumber(total)
//	}
//
// Started by a benchmark task (BENCH_DRIVE is set), the harness answers the driven protocol
// on standard input and output. Started by hand, it finds an iteration count that takes about
// 50 ms for each benchmark, takes 10 samples, and prints them as result lines.
package harness

import (
	"bufio"
	"fmt"
	"math"
	"os"
	"strconv"
	"strings"
	"time"
)

const (
	samples = 10
	most    = 1_000_000_000
)

type value struct {
	unit   string
	amount float64
}

// Harness holds the benchmarks of a program and the run that is in progress.
type Harness struct {
	names       []string
	wire        []string
	driven      bool
	active      bool
	current     int
	count       uint64
	timing      bool
	began       time.Time
	spent       uint64
	values      []value
	check       *string
	failure     *string
	calibrating bool
	sampled     uint64
	target      uint64
	in          *bufio.Reader
	out         *bufio.Writer
}

// WireName returns the name as it goes over the wire: "Benchmark", a first lowercase letter
// in uppercase, and "_" for each space.
func WireName(name string) string {
	text := []byte("Benchmark")
	for at := 0; at < len(name); at++ {
		letter := name[at]
		if at == 0 && letter >= 'a' && letter <= 'z' {
			letter -= 32
		} else if letter == ' ' || letter == '\t' {
			letter = '_'
		}
		text = append(text, letter)
	}
	return string(text)
}

// Grown returns the iteration count to try after iterations took elapsed nanoseconds, when a
// sample should take target.
func Grown(iterations, elapsed, target uint64) uint64 {
	factor := 100.0
	if elapsed > 0 {
		factor = float64(target) / float64(elapsed) * 1.2
	}
	factor = math.Min(math.Max(factor, 2), 100)
	next := math.Ceil(float64(iterations) * factor)
	if next > most {
		return most
	}
	return uint64(next)
}

// Make returns a harness for the benchmarks names. driven says whether a task drives it.
func Make(driven bool, names ...string) *Harness {
	wire := make([]string, len(names))
	for at, name := range names {
		wire[at] = WireName(name)
	}
	return &Harness{
		names:       names,
		wire:        wire,
		driven:      driven,
		count:       1,
		calibrating: true,
		target:      50_000_000,
		in:          bufio.NewReader(os.Stdin),
		out:         bufio.NewWriter(os.Stdout),
	}
}

// Open returns a harness that is driven when a benchmark task started the program.
func Open(names ...string) *Harness {
	return Make(os.Getenv("BENCH_DRIVE") != "", names...)
}

// Sample sets how long a sample should take when nobody drives the harness, in milliseconds.
func (h *Harness) Sample(milliseconds uint64) { h.target = milliseconds * 1_000_000 }

// Name returns the name of the benchmark to run, as it was given to the harness.
func (h *Harness) Name() string { return h.names[h.current] }

// Position returns the position of the benchmark to run in the names the harness was given.
func (h *Harness) Position() int { return h.current }

// Iterations returns how many times to run the body.
func (h *Harness) Iterations() uint64 { return h.count }

// Reset starts the timing again from zero, after setup that should not count.
func (h *Harness) Reset() {
	h.spent = 0
	h.began = time.Now()
	h.timing = true
}

// Stop stops the timing, until Start.
func (h *Harness) Stop() {
	if h.timing {
		h.spent += uint64(time.Since(h.began).Nanoseconds())
		h.timing = false
	}
}

// Start starts the timing again after Stop.
func (h *Harness) Start() {
	if !h.timing {
		h.began = time.Now()
		h.timing = true
	}
}

// Measured gives the harness the time of the run, for a body that timed itself.
func (h *Harness) Measured(nanoseconds uint64) {
	h.spent = nanoseconds
	h.timing = false
}

// Failed says that the run failed, with the reason on one line.
func (h *Harness) Failed(text string) { h.failure = &text }

// Check reports what the run computed, such as a checksum. A task does not compare
// implementations whose checks differ.
func (h *Harness) Check(text string) { h.check = &text }

// CheckNumber reports number as what the run computed.
func (h *Harness) CheckNumber(number uint64) { h.Check(strconv.FormatUint(number, 10)) }

// Value adds a value in another unit to the result line, such as 1024 "B/op".
func (h *Harness) Value(unit string, amount float64) {
	h.values = append(h.values, value{unit, amount})
}

func (h *Harness) begin() {
	h.values = h.values[:0]
	h.check = nil
	h.failure = nil
	h.active = true
	h.spent = 0
	h.timing = true
	h.began = time.Now()
}

func (h *Harness) answer() {
	if h.failure != nil {
		fmt.Fprintf(h.out, "error: %s\n", *h.failure)
		return
	}
	if h.check != nil {
		fmt.Fprintf(h.out, "check: %s\n", *h.check)
	}
	each := float64(h.spent) / float64(h.count)
	fmt.Fprintf(h.out, "%s %d %.2f ns/op", h.wire[h.current], h.count, each)
	for _, extra := range h.values {
		fmt.Fprintf(h.out, " %.2f %s", extra.amount, extra.unit)
	}
	fmt.Fprintln(h.out)
}

func (h *Harness) advance() {
	h.current++
	h.calibrating = true
	h.sampled = 0
	h.count = 1
}

// Next finishes the run in progress, if there is one, and returns whether there is another
// run to do. After true, run the body of Name Iterations times and call Next again.
func (h *Harness) Next() bool {
	if h.active {
		h.Stop()
		h.active = false
		switch {
		case h.driven:
			h.answer()
			fmt.Fprintln(h.out, "done")
			h.out.Flush()
		case h.failure != nil:
			h.answer()
			h.advance()
		case h.calibrating:
			if h.spent >= h.target || h.count >= most {
				h.calibrating = false
			} else {
				h.count = Grown(h.count, h.spent, h.target)
			}
		default:
			h.answer()
			h.sampled++
			if h.sampled == samples {
				h.advance()
			}
		}
	}
	if !h.driven {
		if h.current >= len(h.names) {
			h.out.Flush()
			return false
		}
		h.begin()
		return true
	}
	for {
		line, err := h.in.ReadString('\n')
		command := strings.TrimSpace(line)
		if command == "quit" || (err != nil && command == "") {
			return false
		}
		switch {
		case command == "list":
			for _, name := range h.wire {
				fmt.Fprintln(h.out, name)
			}
		case strings.HasPrefix(command, "run "):
			// "run <name> <iterations>": a name holds no space.
			rest := command[4:]
			found := len(h.wire)
			var count uint64
			if space := strings.LastIndexByte(rest, ' '); space >= 0 {
				for at, name := range h.wire {
					if name == rest[:space] {
						found = at
					}
				}
				count, _ = strconv.ParseUint(rest[space+1:], 10, 64)
			}
			if found < len(h.wire) && count > 0 {
				h.current = found
				h.count = count
				h.begin()
				return true
			}
			fmt.Fprintln(h.out, "error: no such benchmark, or no iteration count")
		case command != "":
			fmt.Fprintln(h.out, "error: unknown command")
		default:
			continue
		}
		fmt.Fprintln(h.out, "done")
		h.out.Flush()
	}
}

var kept uint64

// Keep keeps the compiler from removing the work that computed value.
//
//go:noinline
func Keep(value uint64) uint64 {
	kept = value
	return kept
}
