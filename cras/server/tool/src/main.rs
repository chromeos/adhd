// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use clap::Args;
use clap::Parser;
use cras_dlc::download_dlcs_until_installed;
use cras_dlc::CrasDlcId128;
use cras_s2::global::cras_s2_init;
use cras_s2::S2;
#[derive(Parser)]
enum Cli {
    /// Print the label-audio_beamforming label for lab devices.
    #[command(name = "label-audio_beamforming")]
    LabelAudioBeamforming(LabelAudioBeamformingCommand),

    /// Install all CRAS managed DLCs.
    InstallDlcs(InstallDlcsCommand),
}

trait Command {
    fn run(self) -> anyhow::Result<()>;
}

impl Command for Cli {
    fn run(self) -> anyhow::Result<()> {
        match self {
            Cli::LabelAudioBeamforming(c) => c.run(),
            Cli::InstallDlcs(c) => c.run(),
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

#[derive(Args)]
struct InstallDlcsCommand;

impl Command for InstallDlcsCommand {
    fn run(self) -> anyhow::Result<()> {
        cras_s2_init();
        download_dlcs_until_installed(dlc_no_op_callback, dlc_no_op_callback);
        Ok(())
    }
}

extern "C" fn dlc_no_op_callback(_id: CrasDlcId128, _elapsed_seconds: i32) -> libc::c_int {
    0
}

fn main() {
    env_logger::Builder::new()
        .filter_level(log::LevelFilter::Info)
        .parse_default_env()
        .init();

    Cli::parse().run().unwrap();
}
