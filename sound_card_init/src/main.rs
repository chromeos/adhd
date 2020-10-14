// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//!  `sound_card_init` is an user space binary to perform sound card initialization during boot time.
//!
//!
//!  # Arguments
//!
//!  * `sound_card_id` - The sound card name, ex: sofcmlmax98390d.
//!
//!  Given the `sound_card_id`, this binary parses the CONF_DIR/<sound_card_id>.yaml to perform per sound card initialization.
//!  The upstart job of `sound_card_init` is started by the udev event specified in /lib/udev/rules.d/99-sound_card_init.rules.
#![deny(missing_docs)]
use std::env;
use std::error;
use std::fmt;
use std::fs;
use std::io;
use std::path::PathBuf;
use std::process;
use std::string::String;

use getopts::Options;
use remain::sorted;
use sys_util::{error, info, syslog};

use max98390d::run_max98390d;
use utils::run_time;

type Result<T> = std::result::Result<T, Error>;
const CONF_DIR: &str = "/etc/sound_card_init";

#[derive(Default)]
struct Args {
    pub sound_card_id: String,
}

#[sorted]
#[derive(Debug)]
enum Error {
    MissingOption(String),
    OpenConfigFailed(String, io::Error),
    ParseArgsFailed(getopts::Fail),
    UnsupportedSoundCard(String),
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            MissingOption(option) => write!(f, "missing required option: {}", option),
            OpenConfigFailed(file, e) => write!(f, "failed to open file {}: {}", file, e),
            ParseArgsFailed(e) => write!(f, "parse_args failed: {}", e),
            UnsupportedSoundCard(name) => write!(f, "unsupported sound card: {}", name),
        }
    }
}

fn print_usage(opts: &Options) {
    let brief = "Usage: sound_card_init [options]".to_owned();
    print!("{}", opts.usage(&brief));
}

fn parse_args() -> Result<Args> {
    let mut opts = Options::new();
    opts.optopt("", "id", "sound card id", "ID");
    opts.optflag("h", "help", "print help menu");
    let matches = opts
        .parse(&env::args().collect::<Vec<_>>()[1..])
        .map_err(|e| {
            print_usage(&opts);
            Error::ParseArgsFailed(e)
        })?;

    if matches.opt_present("h") {
        print_usage(&opts);
        process::exit(0);
    }

    let sound_card_id = matches
        .opt_str("id")
        .ok_or_else(|| Error::MissingOption("id".to_owned()))
        .map_err(|e| {
            print_usage(&opts);
            e
        })?;

    Ok(Args { sound_card_id })
}

fn get_config(args: &Args) -> Result<String> {
    let config_path = PathBuf::from(CONF_DIR)
        .join(&args.sound_card_id)
        .with_extension("yaml");

    fs::read_to_string(&config_path)
        .map_err(|e| Error::OpenConfigFailed(config_path.to_string_lossy().to_string(), e))
}

/// Parses the CONF_DIR/<sound_card_id>.yaml and starts sound card initialization.
fn sound_card_init(args: &Args) -> std::result::Result<(), Box<dyn error::Error>> {
    info!("sound_card_id: {}", args.sound_card_id);
    let conf = get_config(args)?;

    match args.sound_card_id.as_str() {
        "sofcmlmax98390d" => {
            run_max98390d(&args.sound_card_id, &conf)?;
            info!("run_max98390d() finished successfully.");
            Ok(())
        }
        _ => Err(Error::UnsupportedSoundCard(args.sound_card_id.clone()).into()),
    }
}

fn main() {
    syslog::init().expect("failed to initialize syslog");
    let args = match parse_args() {
        Ok(args) => args,
        Err(e) => {
            error!("failed to parse arguments: {}", e);
            return;
        }
    };

    if let Err(e) = sound_card_init(&args) {
        error!("sound_card_init: {}", e);
    }

    if let Err(e) = run_time::now_to_file(&args.sound_card_id) {
        error!("failed to create sound_card_init run time file: {}", e);
    }
}
