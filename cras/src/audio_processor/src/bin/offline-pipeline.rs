// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use clap::Parser;

use audio_processor::{
    processors::{CheckShape, InPlaceNegateAudioProcessor, WavSink, WavSource},
    ByteProcessor, Error, MultiBuffer,
};

#[derive(Parser, Debug)]
#[clap(global_setting(clap::AppSettings::DeriveDisplayOrder))]
struct Command {
    #[clap(short = 'i', long)]
    input: String,

    output: String,
}

pub fn main() {
    let command = Command::parse();
    eprintln!("{:?}", command);

    let reader = hound::WavReader::open(command.input).expect("cannot open input file");
    let spec = reader.spec();
    let writer = hound::WavWriter::create(
        command.output,
        hound::WavSpec {
            channels: spec.channels,
            sample_rate: spec.sample_rate,
            bits_per_sample: 32,
            sample_format: hound::SampleFormat::Float,
        },
    )
    .expect("cannot create output file");

    let block_size = spec.sample_rate as usize / 100;
    eprintln!("block size: {}", block_size);
    let mut source = WavSource::new(reader, block_size);
    let mut check_shape = CheckShape::<f32>::new(spec.channels as usize, block_size);
    let mut ext = InPlaceNegateAudioProcessor::<f32>::new();
    let mut sink = WavSink::new(writer);

    let mut pipeline: Vec<&mut dyn ByteProcessor> =
        vec![&mut source, &mut check_shape, &mut ext, &mut sink];

    let mut buf = MultiBuffer::new(0, 0);
    loop {
        let mut slices = buf.as_multi_slice();
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
                        return;
                    }
                    Error::Wav(_) => panic!("{}", error),
                },
            }
        }
        if slices.min_len() == 0 {
            break;
        }
    }
}
