// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod audio_options;

use std::fs::File;
use std::io::{self, BufReader, BufWriter, Read, Write};

use audio_streams::StreamSource;
use libcras::CrasClient;
use sys_util::{set_rt_prio_limit, set_rt_round_robin};

use crate::audio_options::{AudioOptions, Subcommand};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

fn set_priority_to_realtime() {
    const AUDIO_THREAD_RTPRIO: u16 = 10;
    if set_rt_prio_limit(AUDIO_THREAD_RTPRIO as u64).is_err()
        || set_rt_round_robin(AUDIO_THREAD_RTPRIO as i32).is_err()
    {
        println!("Attempt to use real-time priority failed, running with default scheduler.");
    }
}

fn channel_string(num_channels: usize) -> String {
    match num_channels {
        1 => "Mono".to_string(),
        2 => "Stereo".to_string(),
        _ => format!("{} Channels", num_channels),
    }
}

fn playback(opts: AudioOptions) -> Result<()> {
    let num_channels = opts.num_channels.unwrap_or(2);
    let frame_rate = opts.frame_rate.unwrap_or(48000);

    let mut sample_source: Box<dyn Read> = Box::new(BufReader::new(File::open(&opts.file_name)?));

    println!(
        "Playing raw data '{}' : Signed 16 bit Little Endian, Rate {} Hz, {}",
        opts.file_name.display(),
        frame_rate,
        channel_string(num_channels)
    );

    let mut cras_client = CrasClient::new()?;
    let (_control, mut stream) = cras_client.new_playback_stream(
        num_channels,
        frame_rate,
        opts.buffer_size.unwrap_or(256),
    )?;
    set_priority_to_realtime();

    loop {
        let mut buffer = stream
            .next_playback_buffer()
            .expect("failed to get next playback buffer");

        // We only support S16LE samples.
        const S16LE_SIZE: usize = 2;
        let frame_size = S16LE_SIZE * num_channels;
        let frames = buffer.frame_capacity();

        let mut chunk = (&mut sample_source).take((frames * frame_size) as u64);
        let transferred = io::copy(&mut chunk, &mut buffer)?;
        if transferred == 0 {
            break;
        }
    }
    // Stream and client should gracefully be closed out of this scope

    Ok(())
}

fn capture(opts: AudioOptions) -> Result<()> {
    let num_channels = opts.num_channels.unwrap_or(2);
    let frame_rate = opts.frame_rate.unwrap_or(48000);

    let mut sample_sink: Box<dyn Write> = Box::new(BufWriter::new(File::create(&opts.file_name)?));

    println!(
        "Recording raw data '{}' : Signed 16 bit Little Endian, Rate {} Hz, {}",
        opts.file_name.display(),
        frame_rate,
        channel_string(num_channels)
    );

    let mut cras_client = CrasClient::new()?;
    cras_client.enable_cras_capture();
    let (_control, mut stream) = cras_client.new_capture_stream(
        num_channels,
        frame_rate,
        opts.buffer_size.unwrap_or(256),
    )?;
    set_priority_to_realtime();
    loop {
        let mut buf = stream.next_capture_buffer()?;
        io::copy(&mut buf, &mut sample_sink)?;
    }
}

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let opts = match AudioOptions::parse_from_args(&args)? {
        None => return Ok(()),
        Some(v) => v,
    };

    match opts.subcommand {
        Subcommand::Capture => capture(opts)?,
        Subcommand::Playback => playback(opts)?,
    };
    Ok(())
}
