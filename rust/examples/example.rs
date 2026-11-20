use silk_bench_harness::{keep, Harness};

/// FNV-1a over the numbers below `length`.
fn hash(length: u64) -> u64 {
    let mut total = 14695981039346656037u64;
    for at in 0..length {
        total = (total ^ at).wrapping_mul(1099511628211);
    }
    total
}

fn main() {
    let mut harness = Harness::open(&["Hash/n=1000", "Hash/n=100000"]);
    while harness.next() {
        let length = if harness.position() == 0 { 1000 } else { 100000 };
        let mut total = 0u64;
        for _ in 0..harness.iterations() {
            // One iteration: the work whose time is reported.
            total = keep(hash(keep(length)));
        }
        harness.check_number(total);
    }
}
