// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cosine;
mod delay;
pub(crate) mod wav;

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
}

impl Cli {
    fn run(&self) -> anyhow::Result<()> {
        match &self.command {
            Commands::Delay(c) => c.run(),
            Commands::Cosine(c) => c.run(),
        }
    }
}

fn main() {
    if let Err(e) = Cli::parse().run() {
        eprintln!("{e:?}");
    }
}
