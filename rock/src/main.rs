// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cosine;
mod delay;
mod diff;
pub(crate) mod wav;

use std::process::ExitCode;

use clap::Parser;
use clap::Subcommand;

#[derive(Parser)]
struct Cli {
    #[clap(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Compute the delay of two WAVE files
    Delay(delay::DelayCommand),
    /// Compute the cosine similarity of two WAVE files
    Cosine(cosine::CosineCommand),
    /// Compare two WAVE files for content equality, tolerating floating-point inaccuracies
    Diff(diff::DiffCommand),
}

impl Cli {
    fn run(&self) -> anyhow::Result<()> {
        match &self.command {
            Commands::Delay(c) => c.run(),
            Commands::Cosine(c) => c.run(),
            Commands::Diff(c) => c.run(),
        }
    }
}

fn main() -> ExitCode {
    if let Err(e) = Cli::parse().run() {
        eprintln!("{e:?}");
        return ExitCode::from(1);
    }
    ExitCode::SUCCESS
}
