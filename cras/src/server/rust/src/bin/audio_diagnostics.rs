// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Collect information about the audio system from top to bottom.

use std::collections::HashSet;
use std::fs;
use std::fs::File;
use std::io::Read;
use std::iter::FromIterator;
use std::process::Command;
use std::process::Stdio;

use anyhow::Context;
use glob::glob;
use regex::Regex;

/// Fancy wrapper to run a Command.
fn run_command(cmd: &mut Command) {
    println!(
        "=== {} {} ===",
        cmd.get_program().to_string_lossy(),
        cmd.get_args()
            .map(|arg| arg.to_string_lossy())
            .collect::<Vec<_>>()
            .join(" ")
    );
    match cmd.stdin(Stdio::null()).status() {
        Ok(status) if status.success() => (),
        error => {
            println!("audio_diagnostics: run_command failed: {:?}", error);
        }
    }
}

/// Get cards of the specified direction. `dir` must be "aplay" or "arecord".
fn get_cards(dir: &str) -> Vec<String> {
    let re = Regex::new(r"card (\d+):").unwrap();

    match Command::new(dir).arg("-l").output() {
        Ok(output) => {
            let stdout = String::from_utf8_lossy(&output.stdout);
            let mut ids: Vec<String> = re
                .captures_iter(&stdout)
                .map(|m| m[1].to_string())
                .collect();
            ids.sort();
            ids.dedup();
            ids
        }
        Err(_) => {
            eprintln!("Cannot run command {} to get cards", dir);
            vec![]
        }
    }
}

#[derive(Debug)]
enum CrosConfigError {
    KeyMissing,
    Other(anyhow::Error),
}

impl From<anyhow::Error> for CrosConfigError {
    fn from(e: anyhow::Error) -> Self {
        Self::Other(e)
    }
}

fn cros_config(path: &str, key: &str) -> Result<String, CrosConfigError> {
    let output = Command::new("cros_config")
        .arg(path)
        .arg(key)
        .output()
        .with_context(|| "cannot launch command")?;

    match output.status.code() {
        Some(0) => Ok(String::from_utf8(output.stdout)
            .with_context(|| "non-UTF-8 output")?
            .trim_end()
            .to_string()),
        Some(1) => Err(CrosConfigError::KeyMissing),
        Some(code) => Err(CrosConfigError::Other(anyhow::anyhow!(
            "unexpected return code: {}",
            code
        ))),
        None => Err(CrosConfigError::Other(anyhow::anyhow!(
            "terminated by signal"
        ))),
    }
}

fn dump_amp(output_cards: &[String]) -> Result<(), anyhow::Error> {
    let amp = match cros_config("/audio/main", "speaker-amp") {
        Ok(amp) => amp,
        Err(CrosConfigError::KeyMissing) => return Ok(()),
        Err(CrosConfigError::Other(err)) => return Err(err),
    };
    let config = match cros_config("/audio/main", "sound-card-init-conf") {
        Ok(config) => config,
        Err(CrosConfigError::KeyMissing) => return Ok(()),
        Err(CrosConfigError::Other(err)) => return Err(err),
    };
    println!("Note: the current RDC of MAX98373 needs to be measured while an non-zero output stream is playing.");
    for card in output_cards {
        run_command(
            Command::new("sound_card_init")
                .args(&["debug", "--id", card, "--amp", &amp, "--conf", &config]),
        );
    }

    Ok(())
}

fn main() {
    run_command(Command::new("cras_test_client").arg("--dump_server_info"));
    run_command(Command::new("cras_test_client").arg("--dump_audio_thread"));
    run_command(Command::new("cras_test_client").arg("--dump_main"));
    run_command(Command::new("cras_test_client").arg("--dump_bt"));
    run_command(Command::new("cras_test_client").arg("--dump_events"));
    run_command(Command::new("aplay").arg("-l"));
    run_command(Command::new("arecord").arg("-l"));

    let output_cards = get_cards("aplay");
    let input_cards = get_cards("arecord");

    for id in output_cards.iter().chain(input_cards.iter()) {
        run_command(Command::new("amixer").args(&["-c", id, "scontents"]));
        run_command(Command::new("amixer").args(&["-c", id, "contents"]));
    }

    // HDA codec for codecs on x86.
    for path in glob("/proc/asound/*card*/codec#*")
        .expect("invalid glob pattern")
        .flatten()
    {
        println!("=== codec: {} ===", path.to_string_lossy());
        match fs::read_to_string(path.clone()) {
            Ok(contents) => print!("{}", contents),
            Err(err) => eprintln!("Cannot read {}: {}", path.to_string_lossy(), err),
        }
    }

    // I2C dump for codecs on arm.
    // Find lines like "max98088.7-0010" and extract "7 0x0010" from it.
    let re = Regex::new(r"^\([^.-]\+\)\.\([0-9]\+\)-\([0-9]\+\)").unwrap();
    if let Ok(codecs) = fs::read_to_string("/sys/kernel/debug/asoc/codecs") {
        for m in re.captures_iter(&codecs) {
            let addr = format!("{} 0x{}", &m[2].to_string(), &m[3].to_string());
            run_command(Command::new("i2cdump").args(&["-f", "-y", &addr]));
        }
    }

    // # Dump registers from regmaps

    // List of audio components
    // For kernel>=4.14, it is in /sys/kernel/debug/asoc/components
    // For kernel<4.14, it is in /sys/kernel/debug/asoc/codecs
    let audio_comps =
        fs::read_to_string("/sys/kernel/debug/asoc/components").unwrap_or_else(|_| {
            fs::read_to_string("/sys/kernel/debug/asoc/codecs").unwrap_or_default()
        });

    // Blocklist regmap name of dumping registers (tracked by b/154177454)
    let name_blocklist: HashSet<&str> =
        HashSet::from_iter(["snd_hda_codec_hdmi", "rockchip-spdif", "sc7280-lpass-cpu"]);

    for file_path in glob("/sys/kernel/debug/regmap/*")
        .expect("invalid glob pattern")
        .flatten()
    {
        let component = file_path
            .file_name()
            .expect("file name gone for glob result")
            .to_string_lossy();

        if !audio_comps
            .split_ascii_whitespace()
            .into_iter()
            .any(|comp| comp == component)
        {
            continue;
        }

        match fs::read_to_string(file_path.join("name")) {
            Ok(name) => {
                let name = name.trim_end_matches('\n');
                println!(
                    "=== dump registers component: {} name: {} ===",
                    component, name
                );
                if name_blocklist.contains(name) {
                    println!("skipped dumping due to b/154177454");
                } else {
                    let registers_path = file_path.join("registers");
                    match File::open(file_path.join("registers")) {
                        Ok(mut file) => {
                            // The regmap requires reading in a single read system call.
                            // Allocate a large buffer to read it.
                            let mut registers = [0u8; 131072];
                            match file.read(&mut registers) {
                                Ok(len) => {
                                    print!("{}", String::from_utf8_lossy(&registers[..len]));
                                }
                                Err(err) => println!(
                                    "Cannot read {}: {}",
                                    registers_path.to_string_lossy(),
                                    err
                                ),
                            }
                        }
                        Err(err) => {
                            println!("Cannot open {}: {}", registers_path.to_string_lossy(), err)
                        }
                    }
                }
            }
            Err(err) => {
                println!(
                    "Failed at dump registers: {}: {}",
                    file_path.to_string_lossy(),
                    err
                );
            }
        }
    }

    if let Err(err) = dump_amp(&output_cards) {
        eprintln!("Cannot dump amp: {}", err)
    }
}
