// Copyright 2020 The ChromiumOS Authors
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
use std::error;
use std::string::String;

use amp::AmpBuilder;
use argh::FromArgs;
use dsm::utils::run_time;
use libchromeos::sys::error;
use libchromeos::sys::info;
use libchromeos::syslog;
use serde::Serialize;

const IDENT: &str = "sound_card_init";

#[derive(FromArgs, PartialEq, Debug)]
#[argh(description = "Utility tool for Smart Amps")]
struct TopLevelCommand {
    #[argh(subcommand)]
    cmd: Command,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum Command {
    BootTimeCalibration(BootTimeCalibrationArgs),
    Debug(DebugArgs),
    ReadAppliedRdc(ReadAppliedRdcArgs),
    ReadCurrentRdc(ReadCurrentRdcArgs),
}

/// read the applied rdc of the input channel
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "read_applied_rdc")]
struct ReadAppliedRdcArgs {
    /// the sound card id
    #[argh(option)]
    pub id: String,
    /// the speaker amp on the device. It should be $(cros_config /audio/main speaker-amp)
    #[argh(option)]
    pub amp: String,
    /// the config file name. It should be $(cros_config /audio/main sound-card-init-conf)
    #[argh(option)]
    pub conf: String,
    #[argh(option, description = "channel index")]
    pub channel: usize,
}

/// read the current rdc of the input channel. Need to start a playback stream to
/// get the correct reading.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "read_current_rdc")]
struct ReadCurrentRdcArgs {
    /// the sound card id
    #[argh(option)]
    pub id: String,
    /// the speaker amp on the device. It should be $(cros_config /audio/main speaker-amp)
    #[argh(option)]
    pub amp: String,
    /// the config file name. It should be $(cros_config /audio/main sound-card-init-conf)
    #[argh(option)]
    pub conf: String,
    /// channel index
    #[argh(option)]
    pub channel: usize,
}

/// run Amp boot time calibration and apply the calibration result to the Amp
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "boot_time_calibration")]
struct BootTimeCalibrationArgs {
    /// the sound card id
    #[argh(option)]
    pub id: String,
    /// the speaker amp on the device. It should be $(cros_config /audio/main speaker-amp)
    #[argh(option)]
    pub amp: String,
    /// the config file name. It should be $(cros_config /audio/main sound-card-init-conf)
    #[argh(option)]
    pub conf: String,
}

/// print the Amp debug information.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "debug")]
struct DebugArgs {
    /// the sound card id.
    #[argh(option)]
    pub id: String,
    /// the config file name. It should be $(cros_config /audio/main sound-card-init-conf).
    #[argh(option)]
    pub amp: String,
    /// the config file name. It should be $(cros_config /audio/main sound-card-init-conf).
    #[argh(option)]
    pub conf: String,
    /// show the debug message in json.
    #[argh(switch)]
    pub json: bool,
}

/// The speaker rdc calibration result applied to the amp.
#[derive(Serialize)]
pub struct AppliedRDC {
    /// The channel index.
    pub channel: usize,
    /// The DC resistence in ohm.
    pub rdc_in_ohm: f32,
}

/// The current speaker rdc estimated by the amp.
#[derive(Serialize)]
pub struct CurrentRDC {
    /// The channel index.
    pub channel: usize,
    /// The DC resistence in ohm.
    pub rdc_in_ohm: Option<f32>,
}

/// Parses the CONF_DIR/${args.conf}.yaml and starts the boot time calibration.
fn sound_card_init(args: &TopLevelCommand) -> std::result::Result<(), Box<dyn error::Error>> {
    match &args.cmd {
        Command::ReadAppliedRdc(param) => {
            let mut amp = AmpBuilder::new(&param.id, &param.amp, &param.conf).build()?;
            let rdc = AppliedRDC {
                channel: param.channel,
                rdc_in_ohm: amp.get_applied_rdc(param.channel)?,
            };
            println!("{}", serde_json::to_string(&rdc)?);
        }
        Command::ReadCurrentRdc(param) => {
            let mut amp = AmpBuilder::new(&param.id, &param.amp, &param.conf).build()?;
            let rdc = CurrentRDC {
                channel: param.channel,
                rdc_in_ohm: amp.get_current_rdc(param.channel)?,
            };
            println!("{}", serde_json::to_string(&rdc)?);
        }
        Command::BootTimeCalibration(param) => {
            let mut amp = AmpBuilder::new(&param.id, &param.amp, &param.conf).build()?;
            info!("sound_card_id: {}, conf:{}", param.id, param.conf);
            amp.boot_time_calibration()?;
            if let Err(e) = run_time::now_to_file(&param.id) {
                error!("failed to create sound_card_init run time file: {}", e);
            }
        }
        Command::Debug(param) => {
            let mut amp = AmpBuilder::new(&param.id, &param.amp, &param.conf).build()?;
            info!("sound_card_id: {}, conf:{}", param.id, param.conf);
            let info = amp.get_debug_info()?;
            if param.json {
                println!("{}", serde_json::to_string(&info)?);
            } else {
                println!("{}", serde_yaml::to_string(&info)?);
            }
        }
    }
    Ok(())
}

fn main() {
    if let Err(e) = syslog::init(IDENT.to_string(), false /* log_to_stderr */) {
        // syslog macros will fail silently if syslog was not initialized.
        // Print error msg to stderr and continue the sound_card_init operations.
        eprintln!("failed to initialize syslog: {}", e);
    }

    let args: TopLevelCommand = argh::from_env();

    match sound_card_init(&args) {
        Ok(_) => info!("sound_card_init finished successfully."),
        Err(e) => {
            error!("sound_card_init: {}", e);
            return;
        }
    }
}
