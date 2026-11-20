//! A harness for Rust benchmarks that a Silk benchmark task drives.
//!
//! Give the harness the names of the benchmarks and loop on [`Harness::next`]. Each time it
//! returns `true`, run the body of [`Harness::name`] exactly [`Harness::iterations`] times.
//! The harness times from the return of `next` to the next call.
//!
//! Started by a benchmark task (`BENCH_DRIVE` is set), the harness answers the driven protocol
//! on standard input and output. Started by hand, it finds an iteration count that takes about
//! 50 ms for each benchmark, takes 10 samples, and prints them as result lines.
//!
//! ```no_run
//! use silk_bench_harness::{keep, Harness};
//!
//! let mut harness = Harness::open(&["Hash/n=1000"]);
//! while harness.next() {
//!     let mut total = 0u64;
//!     for _ in 0..harness.iterations() {
//!         total = keep((0..keep(1000u64)).fold(7, |hash, at| (hash ^ at).wrapping_mul(31)));
//!     }
//!     harness.check_number(total);
//! }
//! ```

use std::io::{BufRead, Write};
use std::time::Instant;

const SAMPLES: u64 = 10;
const MOST: u64 = 1_000_000_000;

/// Keeps the compiler from removing the work that computed `value`.
pub fn keep<T>(value: T) -> T {
    std::hint::black_box(value)
}

/// The benchmarks of a program, and the run that is in progress.
pub struct Harness {
    names: Vec<String>,
    wire: Vec<String>,
    driven: bool,
    active: bool,
    current: usize,
    count: u64,
    began: Option<Instant>,
    spent: u64,
    values: Vec<(String, f64)>,
    check: Option<String>,
    failure: Option<String>,
    calibrating: bool,
    sampled: u64,
    target: u64,
}

/// Returns the name as it goes over the wire: `Benchmark`, a first lowercase letter in
/// uppercase, and `_` for each space.
pub fn wire_name(name: &str) -> String {
    let mut text = String::from("Benchmark");
    for (at, letter) in name.chars().enumerate() {
        if at == 0 && letter.is_ascii_lowercase() {
            text.push(letter.to_ascii_uppercase());
        } else if letter == ' ' || letter == '\t' {
            text.push('_');
        } else {
            text.push(letter);
        }
    }
    text
}

/// Returns the iteration count to try after `iterations` took `elapsed` nanoseconds, when a
/// sample should take `target`.
pub fn grown(iterations: u64, elapsed: u64, target: u64) -> u64 {
    let mut factor = 100.0;
    if elapsed > 0 {
        factor = target as f64 / elapsed as f64 * 1.2;
    }
    let factor = f64::clamp(factor, 2.0, 100.0);
    let next = (iterations as f64 * factor).ceil();
    if next > MOST as f64 {
        MOST
    } else {
        next as u64
    }
}

impl Harness {
    /// Returns a harness for the benchmarks `names`. `driven` says whether a task drives it.
    pub fn make(names: &[&str], driven: bool) -> Harness {
        Harness {
            names: names.iter().map(|name| name.to_string()).collect(),
            wire: names.iter().map(|name| wire_name(name)).collect(),
            driven,
            active: false,
            current: 0,
            count: 1,
            began: None,
            spent: 0,
            values: Vec::new(),
            check: None,
            failure: None,
            calibrating: true,
            sampled: 0,
            target: 50_000_000,
        }
    }

    /// Returns a harness that is driven when a benchmark task started the program.
    pub fn open(names: &[&str]) -> Harness {
        let driven = std::env::var_os("BENCH_DRIVE").is_some_and(|value| !value.is_empty());
        Harness::make(names, driven)
    }

    /// Sets how long a sample should take when nobody drives the harness, in milliseconds.
    pub fn sample(&mut self, milliseconds: u64) {
        self.target = milliseconds * 1_000_000;
    }

    /// The name of the benchmark to run, as it was given to the harness.
    pub fn name(&self) -> &str {
        &self.names[self.current]
    }

    /// The position of the benchmark to run in the names the harness was given.
    pub fn position(&self) -> usize {
        self.current
    }

    /// How many times to run the body.
    pub fn iterations(&self) -> u64 {
        self.count
    }

    /// Starts the timing again from zero, after setup that should not count.
    pub fn reset(&mut self) {
        self.spent = 0;
        self.began = Some(Instant::now());
    }

    /// Stops the timing, until [`Harness::start`].
    pub fn stop(&mut self) {
        if let Some(began) = self.began.take() {
            self.spent += began.elapsed().as_nanos() as u64;
        }
    }

    /// Starts the timing again after [`Harness::stop`].
    pub fn start(&mut self) {
        if self.began.is_none() {
            self.began = Some(Instant::now());
        }
    }

    /// Gives the harness the time of the run, for a body that timed itself.
    pub fn measured(&mut self, nanoseconds: u64) {
        self.spent = nanoseconds;
        self.began = None;
    }

    /// Says that the run failed, with the reason on one line.
    pub fn failed(&mut self, text: &str) {
        self.failure = Some(text.to_string());
    }

    /// Reports what the run computed, such as a checksum. A task does not compare
    /// implementations whose checks differ.
    pub fn check(&mut self, text: &str) {
        self.check = Some(text.to_string());
    }

    /// Reports `number` as what the run computed.
    pub fn check_number(&mut self, number: u64) {
        self.check = Some(number.to_string());
    }

    /// Adds a value in another unit to the result line, such as 1024 `B/op`.
    pub fn value(&mut self, unit: &str, amount: f64) {
        self.values.push((unit.to_string(), amount));
    }

    fn begin(&mut self) {
        self.values.clear();
        self.check = None;
        self.failure = None;
        self.active = true;
        self.spent = 0;
        self.began = Some(Instant::now());
    }

    fn answer(&self, out: &mut impl Write) {
        if let Some(reason) = &self.failure {
            let _ = writeln!(out, "error: {reason}");
            return;
        }
        if let Some(check) = &self.check {
            let _ = writeln!(out, "check: {check}");
        }
        let each = self.spent as f64 / self.count as f64;
        let _ = write!(out, "{} {} {:.2} ns/op", self.wire[self.current], self.count, each);
        for (unit, amount) in &self.values {
            let _ = write!(out, " {amount:.2} {unit}");
        }
        let _ = writeln!(out);
    }

    fn advance(&mut self) {
        self.current += 1;
        self.calibrating = true;
        self.sampled = 0;
        self.count = 1;
    }

    /// Finishes the run in progress, if there is one, and returns whether there is another
    /// run to do. After `true`, run the body of [`Harness::name`] [`Harness::iterations`]
    /// times and call `next` again.
    #[allow(clippy::should_implement_trait)]
    pub fn next(&mut self) -> bool {
        let mut out = std::io::stdout().lock();
        if self.active {
            self.stop();
            self.active = false;
            if self.driven {
                self.answer(&mut out);
                let _ = writeln!(out, "done");
                let _ = out.flush();
            } else if self.failure.is_some() {
                self.answer(&mut out);
                self.advance();
            } else if self.calibrating {
                if self.spent >= self.target || self.count >= MOST {
                    self.calibrating = false;
                } else {
                    self.count = grown(self.count, self.spent, self.target);
                }
            } else {
                self.answer(&mut out);
                self.sampled += 1;
                if self.sampled == SAMPLES {
                    self.advance();
                }
            }
        }
        if !self.driven {
            if self.current >= self.names.len() {
                return false;
            }
            self.begin();
            return true;
        }
        let mut line = String::new();
        loop {
            line.clear();
            match std::io::stdin().lock().read_line(&mut line) {
                Ok(0) | Err(_) => return false,
                Ok(_) => {}
            }
            let command = line.trim();
            if command == "quit" {
                return false;
            }
            if command == "list" {
                for name in &self.wire {
                    let _ = writeln!(out, "{name}");
                }
            } else if let Some(rest) = command.strip_prefix("run ") {
                // `run <name> <iterations>`: a name holds no space.
                let asked = rest.rsplit_once(' ').and_then(|(name, count)| {
                    let found = self.wire.iter().rposition(|wire| wire == name)?;
                    let count = count.parse::<u64>().ok().filter(|count| *count > 0)?;
                    Some((found, count))
                });
                if let Some((found, count)) = asked {
                    self.current = found;
                    self.count = count;
                    self.begin();
                    return true;
                }
                let _ = writeln!(out, "error: no such benchmark, or no iteration count");
            } else if !command.is_empty() {
                let _ = writeln!(out, "error: unknown command");
            } else {
                continue;
            }
            let _ = writeln!(out, "done");
            let _ = out.flush();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_go_over_the_wire() {
        assert_eq!(wire_name("sum of squares"), "BenchmarkSum_of_squares");
        assert_eq!(wire_name("Parse/file=twitter"), "BenchmarkParse/file=twitter");
    }

    #[test]
    fn counts_grow_within_bounds() {
        assert_eq!(grown(1, 0, 50_000_000), 100);
        assert_eq!(grown(100, 40_000_000, 50_000_000), 200);
        assert_eq!(grown(100, 10_000_000, 50_000_000), 600);
        assert_eq!(grown(900_000_000, 1, 50_000_000), MOST);
    }
}
