// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use getopts::Options;

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

fn show_subcommand_usage(program_name: &str, subcommand: &str, opts: &Options) {
    let brief = format!("Usage: {} {} [options]", program_name, subcommand);
    print!("{}", opts.usage(&brief));
}

pub fn show_usage(program_name: &str) {
    println!("Usage: {} [subcommand] <subcommand args>", program_name);
    println!("\nSubcommands:\n");
    println!("capture - Test capture function");
    println!("playback - Test playback function");
    println!("\nhelp - Print help message");
}

pub struct AudioOptions {
    pub file_name: String,
    pub buffer_size: usize,
    pub num_channels: usize,
    pub frame_rate: usize,
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
