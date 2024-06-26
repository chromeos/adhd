// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::error;
use std::fmt;

use libcras::AudioDebugInfo;
use libcras::CrasClient;
use libcras::CrasIodevInfo;
use libcras::CrasIonodeInfo;

use crate::arguments::ControlCommand;

/// An enumeration of errors that can occur when running `ControlCommand` using
/// the `control()` function.
#[derive(Debug)]
pub enum Error {
    Libcras(libcras::Error),
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            Libcras(e) => write!(f, "Libcras Error: {}", e),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

fn print_nodes(nodes: impl Iterator<Item = CrasIonodeInfo>) {
    println!(
        "{: <13}{: <7}{: <6}{: <10}{: <13}{: <20} {: <10}",
        "Stable ID", "ID", "Vol", "Plugged", "Time", "Type", "Name"
    );
    for node in nodes {
        let id = format!("{}:{}", node.iodev_index, node.ionode_index);
        let stable_id = format!("({:08x})", node.stable_id);
        let plugged_time = node.plugged_time.tv_sec;
        let active = if node.active { "*" } else { " " };
        println!(
            "{: <13}{: <7}{: <6}{: <10}{: <13}{: <20}{}{: <10}",
            stable_id,
            id,
            node.volume,
            node.plugged,
            plugged_time,
            node.type_name,
            active,
            node.name
        );
    }
}

fn print_audio_debug_info(info: &AudioDebugInfo) {
    println!("Audio Debug Stats:");
    println!("-------------devices------------");
    for device in &info.devices {
        println!("{}", device);
        println!();
    }

    println!("-------------stream_dump------------");
    for stream in &info.streams {
        println!("{}", stream);
        println!();
    }
}

fn print_audio_debug_info_json(info: &AudioDebugInfo) {
    serde_json::to_writer(std::io::stdout(), &info).unwrap();
}

fn print_devices<I>(devices: I, json: bool)
where
    I: Iterator<Item = CrasIodevInfo>,
{
    if json {
        let devices_vec: Vec<_> = devices.collect();
        serde_json::to_writer(std::io::stdout(), &devices_vec).unwrap();
    } else {
        println!("{: <5}{: <10}{: <10}", "ID", "LastOpen", "Name");
        for dev in devices {
            println!(
                "{: <5}{: <10}{: <10}",
                dev.index, dev.last_open_result, dev.name
            );
        }
    }
}

/// Connect to CRAS and run the given `ControlCommand`.
pub fn control(command: ControlCommand) -> Result<()> {
    use ControlCommand::*;
    let mut cras_client = CrasClient::new().map_err(Error::Libcras)?;
    match command {
        GetSystemVolume => println!("{}", cras_client.get_system_volume()),
        SetSystemVolume { volume } => {
            cras_client
                .set_system_volume(volume)
                .map_err(Error::Libcras)?;
        }
        GetSystemMute => println!("{}", cras_client.get_system_mute()),
        SetSystemMute { mute } => {
            cras_client.set_system_mute(mute).map_err(Error::Libcras)?;
        }
        ListOutputDevices { json } => print_devices(cras_client.output_devices(), json),
        ListInputDevices { json } => print_devices(cras_client.input_devices(), json),

        ListOutputNodes => print_nodes(cras_client.output_nodes()),
        ListInputNodes => print_nodes(cras_client.input_nodes()),
        DumpAudioDebugInfo { json } => {
            let debug_info = cras_client.get_audio_debug_info().map_err(Error::Libcras)?;
            if json {
                print_audio_debug_info_json(&debug_info);
            } else {
                print_audio_debug_info(&debug_info);
            }
        }
    };
    Ok(())
}
