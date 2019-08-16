// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fs::File;
use std::io::{self, BufRead, BufReader, Write};
use std::thread::spawn;
use sys_util::{set_rt_prio_limit, set_rt_round_robin};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

use getopts::Options;

use audio_streams::StreamSource;
use libcras::CrasClient;

fn set_priority_to_realtime() {
    const AUDIO_THREAD_RTPRIO: u16 = 10;
    if set_rt_prio_limit(AUDIO_THREAD_RTPRIO as u64).is_err()
        || set_rt_round_robin(AUDIO_THREAD_RTPRIO as i32).is_err()
    {
        println!("Attempt to use real-time priority failed, running with default scheduler.");
    }
}

fn show_subcommand_usage(program_name: &str, subcommand: &str, opts: &Options) {
    let brief = format!("Usage: {} {} [options]", program_name, subcommand);
    print!("{}", opts.usage(&brief));
}

struct AudioOptions {
    file_name: String,
    buffer_size: usize,
    num_channels: usize,
    frame_rate: usize,
}

impl AudioOptions {
    pub fn parse_from_args(args: &[String]) -> Result<Option<Self>> {
        let mut opts = Options::new();
        opts.optopt("b", "buffer_size", "Buffer size in frames", "SIZE")
            .optopt("c", "", "Number of channels", "NUM")
            .optopt("f", "file", "Path to playback file", "FILE")
            .optopt("r", "rate", "Audio frame rate (Hz)", "RATE")
            .optflag("h", "help", "Print help message");
        let matches = match opts.parse(&args[1..]) {
            Ok(m) => m,
            Err(e) => {
                show_subcommand_usage(&args[0], &args[1], &opts);
                return Err(Box::new(e));
            }
        };
        if matches.opt_present("h") {
            show_subcommand_usage(&args[0], &args[1], &opts);
            return Ok(None);
        }
        let file_name = match matches.opt_str("f") {
            None => {
                println!("Must input playback file name.");
                show_subcommand_usage(&args[0], &args[1], &opts);
                return Ok(None);
            }
            Some(file_name) => file_name,
        };
        let buffer_size = matches.opt_get_default::<usize>("b", 256)?;
        let num_channels = matches.opt_get_default::<usize>("c", 2)?;
        let frame_rate = matches.opt_get_default::<usize>("r", 48000)?;

        Ok(Some(AudioOptions {
            file_name,
            buffer_size,
            num_channels,
            frame_rate,
        }))
    }
}

fn playback(opts: AudioOptions) -> Result<()> {
    let file = File::open(&opts.file_name).expect("failed to open file");
    let mut buffered_file = BufReader::new(file);

    let mut cras_client = CrasClient::new()?;
    let (_control, mut stream) =
        cras_client.new_playback_stream(opts.num_channels, opts.frame_rate, opts.buffer_size)?;
    let thread = spawn(move || {
        set_priority_to_realtime();
        loop {
            let local_buffer = buffered_file
                .fill_buf()
                .expect("failed to read from input file");

            // Reached EOF
            if local_buffer.len() == 0 {
                break;
            }

            // Gets writable buffer from stream
            let mut buffer = stream
                .next_playback_buffer()
                .expect("failed to get next playback buffer");

            // Writes data to stream buffer
            let write_frames = buffer
                .write(&local_buffer)
                .expect("failed to write output data to buffer");

            // Mark the file data as written
            buffered_file.consume(write_frames);
        }
    });
    thread.join().expect("Failed to join playback thread");
    // Stream and client should gracefully be closed out of this scope

    Ok(())
}

fn capture(opts: AudioOptions) -> Result<()> {
    let mut cras_client = CrasClient::new()?;
    cras_client.enable_cras_capture();
    let (_control, mut stream) =
        cras_client.new_capture_stream(opts.num_channels, opts.frame_rate, opts.buffer_size)?;
    let mut file = File::create(&opts.file_name).unwrap();
    loop {
        let _frames = match stream.next_capture_buffer() {
            Err(e) => {
                return Err(e.into());
            }
            Ok(mut buf) => {
                let written = io::copy(&mut buf, &mut file)?;
                written
            }
        };
    }
}

fn show_usage(program_name: &str) {
    println!("Usage: {} [subcommand] <subcommand args>", program_name);
    println!("\nSubcommands:\n");
    println!("capture - Test capture function");
    println!("playback - Test playback function");
    println!("\nhelp - Print help message");
}

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        println!("Must specify a subcommand.");
        show_usage(&args[0]);
        return Err(Box::new(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "No subcommand",
        )));
    }

    if args[1] == "help" {
        show_usage(&args[0]);
        return Ok(());
    }

    let opts = match AudioOptions::parse_from_args(&args[1..])? {
        None => return Ok(()),
        Some(v) => v,
    };

    match args[1].as_ref() {
        "capture" => capture(opts)?,
        "playback" => playback(opts)?,
        subcommand => {
            println!("Subcommand \"{}\" does not exist.", subcommand);
            show_usage(&args[0]);
            return Err(Box::new(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "Subcommand does not exist",
            )));
        }
    };
    Ok(())
}
