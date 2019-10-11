// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod arguments;

use std::fs::File;
use std::io::{self, BufReader, BufWriter, Read, Write};
use std::os::raw::c_int;
use std::sync::atomic::{AtomicBool, Ordering};

use audio_streams::{SampleFormat, StreamSource};
use libcras::CrasClient;
use sys_util::{register_signal_handler, set_rt_prio_limit, set_rt_round_robin};

use crate::arguments::{AudioOptions, Command};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

static INTERRUPTED: AtomicBool = AtomicBool::new(false);

extern "C" fn sigint_handler() {
    // Check if we've already received one SIGINT. If we have, the program may
    // be misbehaving and not terminating, so to be safe we'll forcefully exit.
    if INTERRUPTED.load(Ordering::Acquire) {
        std::process::exit(1);
    }
    INTERRUPTED.store(true, Ordering::Release);
}

fn add_sigint_handler() -> Result<()> {
    const SIGINT: c_int = 2;
    unsafe { register_signal_handler(SIGINT, sigint_handler)? };
    Ok(())
}

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

fn sample_format_string(format: SampleFormat) -> String {
    use SampleFormat::*;
    let s = match format {
        U8 => "Unsigned 8 bit",
        S16LE => "Signed 16 bit Little Endian",
        S24LE => "Signed 24 bit Little Endian",
        S32LE => "Signed 32 bit Little Endian",
    };
    s.to_string()
}

fn playback(opts: AudioOptions) -> Result<()> {
    let num_channels = opts.num_channels.unwrap_or(2);
    let format = opts.format.unwrap_or(SampleFormat::S16LE);
    let frame_rate = opts.frame_rate.unwrap_or(48000);

    let mut sample_source: Box<dyn Read> = Box::new(BufReader::new(File::open(&opts.file_name)?));

    println!(
        "Playing raw data '{}' : {}, Rate {} Hz, {}",
        opts.file_name.display(),
        sample_format_string(format),
        frame_rate,
        channel_string(num_channels)
    );

    let mut cras_client = CrasClient::new()?;
    let (_control, mut stream) = cras_client.new_playback_stream(
        num_channels,
        format,
        frame_rate,
        opts.buffer_size.unwrap_or(256),
    )?;
    set_priority_to_realtime();

    add_sigint_handler()?;
    while !INTERRUPTED.load(Ordering::Acquire) {
        let mut buffer = stream
            .next_playback_buffer()
            .expect("failed to get next playback buffer");

        let frame_size = num_channels * format.sample_bytes();
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
    let format = opts.format.unwrap_or(SampleFormat::S16LE);
    let frame_rate = opts.frame_rate.unwrap_or(48000);

    let mut sample_sink: Box<dyn Write> = Box::new(BufWriter::new(File::create(&opts.file_name)?));

    println!(
        "Recording raw data '{}' : {}, Rate {} Hz, {}",
        opts.file_name.display(),
        sample_format_string(format),
        frame_rate,
        channel_string(num_channels)
    );

    let mut cras_client = CrasClient::new()?;
    cras_client.enable_cras_capture();
    let (_control, mut stream) = cras_client.new_capture_stream(
        num_channels,
        format,
        frame_rate,
        opts.buffer_size.unwrap_or(256),
    )?;
    set_priority_to_realtime();
    add_sigint_handler()?;
    while !INTERRUPTED.load(Ordering::Acquire) {
        let mut buf = stream.next_capture_buffer()?;
        io::copy(&mut buf, &mut sample_sink)?;
    }
    Ok(())
}

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let opts = match AudioOptions::parse_from_args(&args)? {
        None => return Ok(()),
        Some(v) => v,
    };

    match opts.command {
        Command::Capture => capture(opts)?,
        Command::Playback => playback(opts)?,
    };
    Ok(())
}
