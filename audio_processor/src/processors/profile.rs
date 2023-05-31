// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Display;
use std::time::Duration;
use std::time::Instant;

use crate::AudioProcessor;
use crate::MultiSlice;

pub struct Profile<T: AudioProcessor> {
    pub inner: T,
    pub frames_processed: usize,
    pub cpu_time: Measurement,
    pub wall_time: Measurement,
}

impl<T: AudioProcessor> AudioProcessor for Profile<T> {
    type I = T::I;
    type O = T::O;

    fn process<'a>(
        &'a mut self,
        input: MultiSlice<'a, T::I>,
    ) -> crate::Result<MultiSlice<'a, T::O>> {
        let cpu = cpu_time();
        let wall = Instant::now();

        let output = self.inner.process(input)?;

        let cpu_time = cpu_time() - cpu;
        let wall_time = Instant::elapsed(&wall);
        self.cpu_time.add(cpu_time);
        self.wall_time.add(wall_time);
        self.frames_processed += output.min_len();

        Ok(output)
    }
}

impl<T: AudioProcessor> Profile<T> {
    pub fn new(processor: T) -> Self {
        Self {
            inner: processor,
            frames_processed: 0,
            cpu_time: Measurement::new(),
            wall_time: Measurement::new(),
        }
    }
}

pub struct Measurement {
    /// Sum of all measurements
    pub sum: Duration,
    /// Minimum of all measurements
    pub min: Duration,
    /// Maximum of all measurements
    pub max: Duration,
    /// Number of measurements
    pub count: usize,
}

impl Measurement {
    fn new() -> Measurement {
        Measurement {
            sum: Duration::ZERO,
            min: Duration::MAX,
            max: Duration::ZERO,
            count: 0,
        }
    }

    /// Add one measurement to the `ProfileStats`.
    fn add(&mut self, time: Duration) {
        self.sum += time;
        self.min = self.min.min(time);
        self.max = self.max.max(time);
        self.count += 1;
    }
}

impl Display for Measurement {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!(
            "avg={:?} max={:?} min={:?} sum={:?} count={}",
            if self.count == 0 {
                Duration::ZERO
            } else {
                self.sum / self.count as u32
            },
            self.max,
            self.min,
            self.sum,
            self.count
        ))
    }
}

fn cpu_time() -> Duration {
    use std::mem::MaybeUninit;

    let mut usage = MaybeUninit::<libc::rusage>::zeroed();
    let error = unsafe { libc::getrusage(libc::RUSAGE_SELF, usage.as_mut_ptr()) };

    if error != 0 {
        // getrusage returning non-zero is always a bug in the program
        panic!("getrusage returned {}", error);
    }

    let usage = unsafe { usage.assume_init() };
    duration_from_timeval(usage.ru_utime).expect("cannot convert timeval to Duration")
}

fn duration_from_timeval(value: libc::timeval) -> Result<Duration, <i64 as TryFrom<u64>>::Error> {
    Ok(Duration::from_secs(value.tv_sec.try_into()?)
        + Duration::from_micros(value.tv_usec.try_into()?))
}

#[cfg(test)]
mod tests {
    use std::time::Duration;
    use std::time::Instant;

    use super::Measurement;
    use super::Profile;
    use crate::processors::profile::cpu_time;
    use crate::processors::InPlaceNegateAudioProcessor;
    use crate::AudioProcessor;
    use crate::MultiBuffer;

    #[test]
    fn cpu_time_smoke() {
        super::cpu_time();
    }

    #[test]
    fn measurement_add() {
        let mut m = Measurement::new();
        m.add(Duration::from_secs(3));
        assert_eq!(m.min, Duration::from_secs(3));
        assert_eq!(m.max, Duration::from_secs(3));
        assert_eq!(m.sum, Duration::from_secs(3));
        assert_eq!(m.count, 1);

        m.add(Duration::from_millis(700));
        assert_eq!(m.min, Duration::from_millis(700));
        assert_eq!(m.max, Duration::from_secs(3));
        assert_eq!(m.sum, Duration::from_millis(3700));
        assert_eq!(m.count, 2);
    }

    #[test]
    fn measurement_display() {
        let m = Measurement {
            sum: Duration::from_secs(1),
            min: Duration::from_secs(2),
            max: Duration::from_secs(3),
            count: 4,
        };
        assert_eq!(format!("{}", m), "avg=250ms max=3s min=2s sum=1s count=4");
    }

    #[test]
    fn measurement_display_empty() {
        let m = Measurement::new();
        format!("{}", m); // should not panic, e.g. division by zero
    }

    #[test]
    fn duration_from_timeval() {
        assert_eq!(
            super::duration_from_timeval(libc::timeval {
                tv_sec: 1,
                tv_usec: 500_000,
            })
            .unwrap(),
            Duration::from_millis(1500)
        );
    }

    #[test]
    fn profile() {
        let mut p = Profile::new(InPlaceNegateAudioProcessor::<i32>::new());
        let mut buf = MultiBuffer::from(vec![vec![1i32, 2, 3, 4], vec![5, 6, 7, 8]]);

        let cpu_start = super::cpu_time();
        let wall_start = Instant::now();

        assert_eq!(
            p.process(buf.as_multi_slice()).unwrap().into_raw(),
            vec![vec![-1i32, -2, -3, -4], vec![-5, -6, -7, -8]]
        );
        assert_eq!(
            p.process(buf.as_multi_slice()).unwrap().into_raw(),
            vec![vec![1i32, 2, 3, 4], vec![5, 6, 7, 8]]
        );

        let cpu_all = cpu_time() - cpu_start;
        let wall_all = Instant::elapsed(&wall_start);

        assert_eq!(p.frames_processed, 8);

        assert!(p.cpu_time.min <= p.cpu_time.max);
        assert!(p.cpu_time.max <= p.cpu_time.sum);
        assert!(p.cpu_time.sum <= cpu_all);

        assert!(p.wall_time.min <= p.wall_time.max);
        assert!(p.wall_time.max <= p.wall_time.sum);
        assert!(p.wall_time.sum <= wall_all);
    }
}
