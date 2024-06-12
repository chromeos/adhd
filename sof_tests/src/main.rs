// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cstate;
mod profile;

use std::error;
use std::fmt;

use clap::Args;
use clap::Parser;

#[derive(PartialEq, Debug, Parser)]
enum Command {
    /// Get SOF fw/tplg profile on device
    Profile(ProfileOptions),
    /// Get SOF component state via mixer control
    Cstate(cstate::CstateOptions),
}

#[derive(PartialEq, Debug, Args)]
struct ProfileOptions {
    /// Print in json format
    #[arg(long)]
    json: bool,
}

#[derive(Debug)]
pub enum SofError {
    Cstate(anyhow::Error),
    Profile(anyhow::Error),
}

// To make it an error
impl error::Error for SofError {}

impl fmt::Display for SofError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use SofError::*;
        match self {
            Cstate(e) => write!(f, "cmd cstate: {}", e),
            Profile(e) => write!(f, "cmd profile: {}", e),
        }
    }
}

type Result<T> = std::result::Result<T, SofError>;

fn run() -> Result<()> {
    let cmd = Command::parse();

    match cmd {
        Command::Cstate(opts) => cstate::cstate(opts).map_err(SofError::Cstate),
        Command::Profile(opts) => profile::profile(opts.json).map_err(SofError::Profile),
    }
}

fn main() {
    // Use run() instead of returning a Result from main() so that we can print
    // errors using Display instead of Debug.
    if let Err(e) = run() {
        eprintln!("ERROR: {}", e);
        std::process::exit(-1);
    }
}
