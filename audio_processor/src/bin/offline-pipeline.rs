// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::num::ParseFloatError;
use std::path::Path;
use std::path::PathBuf;
use std::sync::mpsc::channel;
use std::time::Duration;

use audio_processor::cdcfg::parse;
use audio_processor::cdcfg::NaiveResolverContext;
use audio_processor::config::PipelineBuilder;
use audio_processor::config::Processor;
use audio_processor::processors::peer::AudioWorkerSubprocessFactory;
use audio_processor::processors::profile;
use audio_processor::processors::profile::Measurements;
use audio_processor::processors::CheckShape;
use audio_processor::processors::WavSource;
use audio_processor::AudioProcessor;
use audio_processor::Error;
use audio_processor::MultiBuffer;
use audio_processor::Shape;
use clap::Parser;
use serde::Serialize;

#[derive(Parser, Debug)]
struct Command {
    /// Path to the plugin library (.so) or pipeline (.txtpb)
    #[arg(value_parser = parse_plugin_or_pipeline)]
    plugin_or_pipeline: PluginOrPipeline,

    /// Path of input WAVE file
    input: PathBuf,
    /// Path of output WAVE file
    output: PathBuf,

    /// Time in seconds to sleep between each processing block iteration.
    /// A value of 0 causes the running thread to yield instead of sleeping.
    #[arg(long = "sleep-sec", value_parser = parse_duration)]
    sleep_duration: Option<Duration>,

    /// Symbol name of the function that creates the plugin processor in `plugin`.
    #[arg(long, default_value = "plugin_processor_create")]
    plugin_name: String,

    /// Block size of the processor, in milliseconds.
    #[arg(long, default_value = "10")]
    block_size_ms: usize,

    /// Block size of the processor, in frames.
    /// Takes precedence over --block-size-ms.
    #[arg(long)]
    block_size_frames: Option<usize>,

    /// Also print JSON profile results in stdout.
    #[arg(long)]
    json: bool,
}

#[derive(Debug, Clone)]
enum PluginOrPipeline {
    Plugin(PathBuf),
    Pipeline(PathBuf),
}

fn parse_plugin_or_pipeline(arg: &str) -> anyhow::Result<PluginOrPipeline> {
    let path = Path::new(arg);
    match path.extension().map(|osstr| osstr.to_str()).flatten() {
        Some("so") => Ok(PluginOrPipeline::Plugin(path.into())),
        Some("txtpb") => Ok(PluginOrPipeline::Pipeline(path.into())),
        _ => Err(anyhow::anyhow!(
            "invalid extension; supported extensions: .so, .txtpb"
        )),
    }
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

    let block_size = command.compute_block_size(spec.sample_rate as usize);
    eprintln!("block size: {}", block_size);

    let mut source = WavSource::new(reader, block_size);
    let mut check_shape = CheckShape::<f32>::new(source.get_output_format());

    let processor = match &command.plugin_or_pipeline {
        PluginOrPipeline::Plugin(path) => Processor::Plugin {
            path: path.into(),
            constructor: command.plugin_name,
        },
        PluginOrPipeline::Pipeline(path) => {
            parse(&NaiveResolverContext::default(), &path).unwrap_or_else(|err| panic!("{err:#}"))
        }
    };
    let pipeline_decl = vec![
        processor,
        Processor::WavSink {
            path: command.output,
        },
    ];
    eprintln!("pipeline config: {pipeline_decl:?}");

    let (profile_sender, profile_receiver) = channel();
    let mut pipeline = PipelineBuilder::new(check_shape.get_output_format())
        .with_profile_sender(profile_sender)
        .with_worker_factory(AudioWorkerSubprocessFactory)
        .build(Processor::Pipeline {
            processors: pipeline_decl,
        })
        .unwrap();

    let mut buf = MultiBuffer::new(Shape {
        channels: 0,
        frames: 0,
    });
    'outer: loop {
        if let Some(dur) = command.sleep_duration {
            if dur.is_zero() {
                std::thread::yield_now();
            } else {
                std::thread::sleep(dur);
            }
        }

        let slices = source.process(buf.as_multi_slice()).unwrap();
        if slices.min_len() == 0 {
            break;
        }
        let slices = match check_shape.process(slices) {
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
        };
        pipeline.process(slices).unwrap();
    }

    // Drop pipeline to flush profile.
    drop(pipeline);

    // Dump profiling result if running with plugin mode.
    if let PluginOrPipeline::Plugin(_) = command.plugin_or_pipeline {
        // Receive the first stat.
        let stats = profile_receiver.recv().unwrap();

        let clip_duration = stats.frames_generated as f64 / stats.output_format.frame_rate as f64;
        let result = ProfileResult::new(&stats.measurements, clip_duration);
        eprintln!("cpu: {:?}", result.cpu);
        eprintln!("wall: {:?}", result.wall);

        if command.json {
            println!(
                "{}",
                serde_json::to_string(&result).expect("JSON serialization failed")
            );
        }
    }
}

#[cfg(test)]
mod tests {
    #[cfg(feature = "bazel")]
    #[test]
    fn simple_negate() {
        use std::env;

        use audio_processor::util::read_wav;
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
            plugin_or_pipeline: super::PluginOrPipeline::Plugin(
                env::var("LIBTEST_PLUGINS_SO").unwrap().into(),
            ),
            input: in_wav_path,
            output: out_wav_path.clone(),
            sleep_duration: None,
            plugin_name: "negate_processor_create".to_string(),
            block_size_ms: 10,
            block_size_frames: None,
            json: false,
        });

        // Verify the output.
        let (_, buffer) = read_wav::<f32>(&out_wav_path).unwrap();
        let vecs = buffer.to_vecs();
        assert_eq!(vecs.len(), 2);
        assert_eq!(vecs[0].len(), 48000);
        assert_eq!(vecs[1].len(), 48000);
        for (&l, &r) in vecs[0].iter().zip(vecs[1].iter()) {
            // A -(i16::MAX) sample is around -1f32.
            assert!(l < -0.9);
            // A -(i16::MIN) sample is around 1f32.
            assert!(r > 0.9);
        }
    }
}
