// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use clap::Args;
use clap::Parser;
use cras_s2::S2;

#[derive(Parser)]
enum Cli {
    /// Print the label-audio_beamforming label for lab devices.
    #[command(name = "label-audio_beamforming")]
    LabelAudioBeamforming(LabelAudioBeamformingCommand),
}

trait Command {
    fn run(self) -> anyhow::Result<()>;
}

impl Command for Cli {
    fn run(self) -> anyhow::Result<()> {
        match self {
            Cli::LabelAudioBeamforming(c) => c.run(),
        }
    }
}

#[derive(Args)]
struct LabelAudioBeamformingCommand;

impl Command for LabelAudioBeamformingCommand {
    fn run(self) -> anyhow::Result<()> {
        let mut s2 = S2::new();
        s2.read_cras_config();
        println!("{}", s2.output.label_audio_beamforming);
        Ok(())
    }
}

fn main() {
    env_logger::Builder::new()
        .filter_level(log::LevelFilter::Info)
        .parse_default_env()
        .init();

    Cli::parse().run().unwrap();
}
