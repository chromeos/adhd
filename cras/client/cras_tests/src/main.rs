// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fs::File;
use std::io::{Read, Write};
use std::thread::spawn;
type Result<T> = std::result::Result<T, Box<std::error::Error>>;

use getopts::Options;

use audio_streams::StreamSource;
use libcras::CrasClient;

fn show_subcommand_usage(program_name: &str, subcommand: &str, opts: &Options) {
    let brief = format!("Usage: {} {} [options]", program_name, subcommand);
    print!("{}", opts.usage(&brief));
}

fn playback(args: &[String]) -> Result<()> {
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
        return Ok(());
    }
    let file_name = match matches.opt_str("f") {
        None => {
            println!("Must input playback file name.");
            show_subcommand_usage(&args[0], &args[1], &opts);
            return Ok(());
        }
        Some(file_name) => file_name,
    };
    let buffer_size = matches.opt_get_default::<usize>("b", 256)?;
    let num_channels = matches.opt_get_default::<usize>("c", 2)?;
    let frame_rate = matches.opt_get_default::<usize>("r", 48000)?;
    let mut cras_client = CrasClient::new()?;
    let (_control, mut stream) =
        cras_client.new_playback_stream(num_channels, frame_rate, buffer_size)?;

    let mut file = File::open(&file_name).unwrap();
    let thread = spawn(move || {
        // Play samples from a file
        let mut local_buffer = vec![0u8; buffer_size * num_channels * 2];
        loop {
            // Reads data to local buffer
            let read_count = file.read(&mut local_buffer).unwrap();
            if read_count == 0 {
                break;
            }
            // Gets writable buffer from stream and
            let mut buffer = stream.next_playback_buffer().unwrap();
            // Writes data to stream buffer
            let _write_frames = buffer.write(&local_buffer[..read_count]).unwrap();
        }
    });
    thread.join().expect("Failed to join playback thread");
    // Stream and client should gracefully be closed out of this scope

    Ok(())
}

fn capture(args: &[String]) -> Result<()> {
    let mut opts = Options::new();
    opts.optopt("b", "buffer_size", "Buffer size in frames", "SIZE")
        .optopt("c", "", "Number of channels", "NUM")
        .optopt("f", "file", "Path to capture file", "FILE")
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
        return Ok(());
    }
    let file_name = match matches.opt_str("f") {
        None => {
            println!("Must input capture file name.");
            show_subcommand_usage(&args[0], &args[1], &opts);
            return Ok(());
        }
        Some(file_name) => file_name,
    };
    let buffer_size = matches.opt_get_default::<usize>("b", 256)?;
    let num_channels = matches.opt_get_default::<usize>("c", 2)?;
    let frame_rate = matches.opt_get_default::<usize>("r", 48000)?;

    let mut cras_client = CrasClient::new()?;
    cras_client.enable_cras_capture();
    let (_control, mut stream) =
        cras_client.new_capture_stream(num_channels, frame_rate, buffer_size)?;
    let mut file = File::create(&file_name).unwrap();
    let mut local_buffer = vec![0u8; buffer_size * num_channels * 2];
    loop {
        let _frames = match stream.next_capture_buffer() {
            Err(e) => {
                return Err(e.into());
            }
            Ok(mut buf) => {
                buf.read(&mut local_buffer)?;
                file.write(local_buffer.as_ref())?
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

    match args[1].as_ref() {
        "capture" => capture(&args)?,
        "playback" => playback(&args)?,
        "help" => show_usage(&args[0]),
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
