// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Collect information about the audio system from top to bottom.

mod uptime;

use std::collections::HashMap;
use std::collections::HashSet;
use std::env;
use std::fs;
use std::fs::File;
use std::io::Read;
use std::iter::FromIterator;
use std::process::Command;
use std::process::Stdio;

use anyhow::Context;
use audio_diagnostics::*;
use cras_common::fra;
use cras_common::fra::CrasFRASignal;
use cras_common::fra::FRALog;
use cras_common::logging::SimpleStdoutLogger;
use cras_common::pseudonymization::Salt;
use glob::glob;
use libcras::CrasClient;
use log::LevelFilter;
use log::SetLoggerError;
use regex::Regex;

use crate::uptime::analyze;

static LOGGER: SimpleStdoutLogger = SimpleStdoutLogger;
static CRAS_PROCESS_NAME: &'static str = "cras-main";

fn init_log() -> Result<(), SetLoggerError> {
    log::set_logger(&LOGGER).map(|()| log::set_max_level(LevelFilter::Info))
}

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
                .args(["debug", "--id", card, "--amp", &amp, "--conf", &config]),
        );
    }

    Ok(())
}

fn main() {
    init_log().expect("cannot init log");
    env::set_var("CRAS_PSEUDONYMIZATION_SALT", Salt::instance().to_string());

    // Show CRAS process uptime
    run_command(Command::new("ps").args(["-C", CRAS_PROCESS_NAME, "-o", "comm,etime"]));
    do_analysis_uptime();
    dump_active_node();

    run_command(Command::new("cras_test_client").arg("--dump_server_info"));
    run_command(Command::new("cras_test_client").arg("--dump_dsp_offload"));
    run_command(Command::new("sof_helper").arg("cstate"));
    run_command(Command::new("cras_test_client").arg("--dump_audio_thread"));
    run_command(Command::new("cras_test_client").arg("--dump_main"));
    run_command(Command::new("cras_test_client").arg("--dump_bt"));
    run_command(Command::new("cras_test_client").arg("--dump_events"));
    run_command(Command::new("aplay").arg("-l"));
    run_command(Command::new("arecord").arg("-l"));

    let output_cards = get_cards("aplay");
    let input_cards = get_cards("arecord");

    for id in output_cards.iter().chain(input_cards.iter()) {
        run_command(Command::new("amixer").args(["-c", id, "scontents"]));
        run_command(Command::new("amixer").args(["-c", id, "contents"]));
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
            run_command(Command::new("i2cdump").args(["-f", "-y", &addr]));
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

    run_command(Command::new("dbus-send").args([
        "--system",
        "--print-reply",
        "--dest=org.chromium.cras",
        "/org/chromium/cras",
        "org.chromium.cras.Control.SpeakOnMuteDetectionEnabled",
    ]));

    run_command(Command::new("dbus-send").args([
        "--system",
        "--print-reply",
        "--dest=org.chromium.cras",
        "/org/chromium/cras",
        "org.chromium.cras.Control.DumpS2AsJSON",
    ]));

    crate::settings::print_salted_audio_settings("/home/chronos/Local State");
}

fn print_analysis_result(results: Vec<Analysis>) {
    if results.is_empty() {
        return;
    }

    println!(); // Print empty line to make it easier to read
    for res in results {
        println!("{}", res);
    }
    println!();
}

fn do_analysis_uptime() {
    // Use "etimes" to get the uptime in "second" format.
    let ps_uptime_output = match Command::new("ps")
        .args(["-C", CRAS_PROCESS_NAME, "-o", "etimes="])
        .output()
    {
        Ok(out) => String::from_utf8_lossy(&out.stdout).to_string(),
        Err(err) => {
            eprintln!("do_analysis_uptime failed to run ps: {}", err);
            return;
        }
    };
    let uptime_output = match fs::read_to_string("/proc/uptime") {
        Ok(out) => out,
        Err(err) => {
            eprintln!("do_analysis_uptime failed to read /proc/uptime: {}", err);
            return;
        }
    };

    print_analysis_result(analyze(&ps_uptime_output, &uptime_output));
}

fn dump_active_node() {
    let cras_client = match CrasClient::new() {
        Ok(client) => client,
        Err(err) => {
            eprintln!("dump_active_node failed to new cras_client: {}", err);
            return;
        }
    };

    if let Some(node) = cras_client.output_nodes().find(|node| node.active) {
        fra!(
            CrasFRASignal::ActiveOutputDevice,
            HashMap::from([(String::from("type"), node.type_name.to_lowercase())])
        )
    }

    if let Some(node) = cras_client.input_nodes().find(|node| node.active) {
        fra!(
            CrasFRASignal::ActiveInputDevice,
            HashMap::from([(String::from("type"), node.type_name.to_lowercase())])
        )
    }
}
