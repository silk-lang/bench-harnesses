package harness

import "testing"

func TestWireName(t *testing.T) {
	if got := WireName("sum of squares"); got != "BenchmarkSum_of_squares" {
		t.Fatal(got)
	}
	if got := WireName("Parse/file=twitter"); got != "BenchmarkParse/file=twitter" {
		t.Fatal(got)
	}
}

func TestGrown(t *testing.T) {
	cases := [][4]uint64{
		{1, 0, 50_000_000, 100},
		{100, 40_000_000, 50_000_000, 200},
		{100, 10_000_000, 50_000_000, 600},
		{900_000_000, 1, 50_000_000, most},
	}
	for _, c := range cases {
		if got := Grown(c[0], c[1], c[2]); got != c[3] {
			t.Fatal(c, got)
		}
	}
}
