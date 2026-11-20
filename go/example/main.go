package main

import harness "github.com/silk-lang/bench-harnesses/go"

// hash is FNV-1a over the numbers below length.
func hash(length uint64) uint64 {
	total := uint64(14695981039346656037)
	for at := uint64(0); at < length; at++ {
		total = (total ^ at) * 1099511628211
	}
	return total
}

func main() {
	h := harness.Open("Hash/n=1000", "Hash/n=100000")
	for h.Next() {
		length := uint64(1000)
		if h.Position() == 1 {
			length = 100000
		}
		var total uint64
		for left := h.Iterations(); left > 0; left-- {
			// One iteration: the work whose time is reported.
			total = harness.Keep(hash(harness.Keep(length)))
		}
		h.CheckNumber(total)
	}
}
