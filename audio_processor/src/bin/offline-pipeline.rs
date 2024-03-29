// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::num::ParseFloatError;
use std::path::PathBuf;
use std::time::Duration;

use audio_processor::processors::profile;
use audio_processor::processors::profile::Measurements;
use audio_processor::processors::CheckShape;
use audio_processor::processors::DynamicPluginProcessor;
use audio_processor::processors::WavSink;
use audio_processor::processors::WavSource;
use audio_processor::ByteProcessor;
use audio_processor::Error;
use audio_processor::MultiBuffer;
use audio_processor::Shape;
use clap::Parser;
use serde::Serialize;

#[derive(Parser, Debug)]
#[clap(global_setting(clap::AppSettings::DeriveDisplayOrder))]
struct Command {
    /// Path to the plugin library (.so)
    plugin: String,

    /// Path of input WAVE file
    input: PathBuf,
    /// Path of output WAVE file
    output: PathBuf,

    /// Time in seconds to sleep between each processing block iteration.
    /// A value of 0 causes the running thread to yield instead of sleeping.
    #[clap(long = "sleep-sec", value_parser = parse_duration)]
    sleep_duration: Option<Duration>,

    /// Symbol name of the function that creates the plugin processor in `plugin`.
    #[clap(long, default_value = "plugin_processor_create")]
    plugin_name: String,

    /// Block size of the processor, in milliseconds.
    #[clap(long, default_value = "10")]
    block_size_ms: usize,

    /// Block size of the processor, in frames.
    /// Takes precedence over --block-size-ms.
    #[clap(long)]
    block_size_frames: Option<usize>,

    /// Also print JSON profile results in stdout.
    #[clap(long)]
    json: bool,

    /// The number of output channels.
    /// If not specified, assumes output channels equals to input channels.
    #[clap(long)]
    output_channels: Option<u16>,
}

fn parse_duration(arg: &str) -> Result<std::time::Duration, ParseFloatError> {
    let seconds: f64 = arg.parse()?;
    Ok(std::time::Duration::from_secs_f64(seconds))
}

#[derive(Debug, Serialize)]
struct PerfStats {
    #[serde(rename = "min_ms", serialize_with = "duration_as_ms")]
    min: Duration,
    #[serde(rename = "max_ms", serialize_with = "duration_as_ms")]
    max: Duration,
    #[serde(rename = "mean_ms", serialize_with = "duration_as_ms")]
    mean: Duration,
    #[serde(skip_serializing)]
    #[allow(dead_code)] // This is actually used by Debug.
    count: usize,
    real_time_factor: f64,
}

impl PerfStats {
    fn new(m: &profile::Measurement, clip_duration: f64) -> Self {
        Self {
            min: m.min,
            max: m.max,
            mean: m.sum / m.count as u32,
            count: m.count,
            real_time_factor: m.sum.as_secs_f64() / clip_duration,
        }
    }
}

fn duration_as_ms<S: serde::Serializer>(v: &Duration, serializer: S) -> Result<S::Ok, S::Error> {
    (v.as_secs_f64() * 1000.).serialize(serializer)
}

#[derive(Debug, Serialize)]
struct ProfileResult {
    cpu: PerfStats,
    wall: PerfStats,
}

impl ProfileResult {
    fn new(m: &Measurements, clip_duration: f64) -> Self {
        Self {
            cpu: PerfStats::new(&m.cpu_time, clip_duration),
            wall: PerfStats::new(&m.wall_time, clip_duration),
        }
    }
}

impl Command {
    fn compute_block_size(&self, frame_rate: usize) -> usize {
        match self.block_size_frames {
            Some(frames) => frames,
            None => self.block_size_ms * frame_rate / 1000,
        }
    }
}

pub fn main() {
    run(Command::parse());
}

fn run(command: Command) {
    eprintln!("{:?}", command);
    let reader = hound::WavReader::open(command.input.clone()).expect("cannot open input file");
    let spec = reader.spec();
    let writer = hound::WavWriter::create(
        command.output.clone(),
        hound::WavSpec {
            channels: command.output_channels.unwrap_or(spec.channels),
            sample_rate: spec.sample_rate,
            bits_per_sample: 32,
            sample_format: hound::SampleFormat::Float,
        },
    )
    .expect("cannot create output file");

    let block_size = command.compute_block_size(spec.sample_rate as usize);
    eprintln!("block size: {}", block_size);
    let mut source = WavSource::new(reader, block_size);
    let frame_rate =
        usize::try_from(spec.sample_rate).expect("Sample rate failed to fit into usize");
    let mut check_shape = CheckShape::<f32>::new(spec.channels as usize, block_size, frame_rate);
    let ext = DynamicPluginProcessor::new(
        &command.plugin,
        &command.plugin_name,
        block_size,
        spec.channels as usize,
        spec.sample_rate as usize,
    )
    .expect("Cannot load plugin");
    let mut profile = profile::Profile::new(ext);
    let mut sink = WavSink::new(writer);

    let mut pipeline: Vec<&mut dyn ByteProcessor> =
        vec![&mut source, &mut check_shape, &mut profile, &mut sink];

    let mut buf = MultiBuffer::new(Shape {
        channels: 0,
        frames: 0,
    });
    'outer: loop {
        let mut slices = buf.as_multi_slice();

        if let Some(dur) = command.sleep_duration {
            if dur.is_zero() {
                std::thread::yield_now();
            } else {
                std::thread::sleep(dur);
            }
        }

        for processor in pipeline.iter_mut() {
            slices = match processor.process_bytes(slices) {
                Ok(output) => output,
                Err(error) => match error {
                    Error::InvalidShape {
                        want_frames,
                        got_frames,
                        want_channels,
                        got_channels,
                    } => {
                        assert_eq!(
                            want_channels, got_channels,
                            "WavSource returned invalid channels: want {} got {}",
                            want_channels, got_channels,
                        );
                        if got_frames > 0 {
                            eprintln!(
                                "dropped last {} frames which do not fit into a {}-frame block",
                                got_frames, want_frames,
                            );
                        }
                        break 'outer;
                    }
                    _ => panic!("{}", error),
                },
            }
        }
        if slices.min_len() == 0 {
            break;
        }
    }

    let clip_duration = profile.frames_processed as f64 / spec.sample_rate as f64;
    let result = ProfileResult::new(&profile.measurements, clip_duration);
    eprintln!("cpu: {:?}", result.cpu);
    eprintln!("wall: {:?}", result.wall);

    if command.json {
        println!(
            "{}",
            serde_json::to_string(&result).expect("JSON serialization failed")
        );
    }
}

#[cfg(test)]
mod tests {
    #[cfg(feature = "bazel")]
    #[test]
    fn simple_negate() {
        use std::env;

        use assert_matches::assert_matches;
        use tempfile::TempDir;

        let dir = TempDir::new().unwrap();
        let in_wav_path = dir.path().join("input.wav");
        let out_wav_path = dir.path().join("output.wav");

        let mut writer = hound::WavWriter::create(
            &in_wav_path,
            hound::WavSpec {
                channels: 2,
                sample_rate: 48000,
                bits_per_sample: 16,
                sample_format: hound::SampleFormat::Int,
            },
        )
        .unwrap();

        // Write 1 second of audio samples.
        for _ in 0..48000 {
            // Write positive for L.
            writer.write_sample(i16::MAX).unwrap();
            // Write negative for R.
            writer.write_sample(i16::MIN).unwrap();
        }
        drop(writer);

        super::run(crate::Command {
            plugin: env::var("LIBTEST_PLUGINS_SO").unwrap(),
            input: in_wav_path,
            output: out_wav_path.clone(),
            sleep_duration: None,
            plugin_name: "negate_processor_create".to_string(),
            block_size_ms: 10,
            block_size_frames: None,
            json: false,
            output_channels: None,
        });

        // Verify the output.
        let mut reader = hound::WavReader::open(out_wav_path).unwrap();
        assert_eq!(
            reader.spec(),
            hound::WavSpec {
                channels: 2,
                sample_rate: 48000,
                bits_per_sample: 32,
                sample_format: hound::SampleFormat::Float,
            }
        );
        let mut samples = reader.samples::<f32>();
        for _ in 0..48000 {
            // A -(i16::MAX) sample is around -1f32.
            assert!(samples.next().unwrap().unwrap() < -0.9);
            // A -(i16::MIN) sample is around 1f32.
            assert!(samples.next().unwrap().unwrap() > 0.9);
        }
        assert_matches!(samples.next(), None);
    }
}
