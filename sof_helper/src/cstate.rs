// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::process::Command;
use std::process::Stdio;
use std::str;

use anyhow::Context;
use anyhow::Result;
use clap::Args;
use clap::ValueEnum;
use regex::Regex;

#[derive(Debug, PartialEq, Clone, ValueEnum)]
pub enum ComponentType {
    #[value(name = "nc")]
    NoiseCancellation,
    #[value(name = "aec")]
    EchoCancellation,
    #[value(name = "drc")]
    DynamicRangeCompression,
    #[value(name = "eq")]
    IIREqualizer,
    #[value(name = "waves")]
    WavesPostProcessor,
    #[value(name = "dts")]
    DTSPostProcessor,
    #[value(name = "dsm")]
    DSMSmartAmp,
    #[value(name = "ghd")]
    GoogleHotword,
}

impl fmt::Display for ComponentType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use ComponentType::*;

        match self {
            NoiseCancellation => write!(f, "NC"),
            EchoCancellation => write!(f, "AEC"),
            DynamicRangeCompression => write!(f, "DRC"),
            IIREqualizer => write!(f, "EQ"),
            WavesPostProcessor => write!(f, "WAVES"),
            DTSPostProcessor => write!(f, "DTS"),
            DSMSmartAmp => write!(f, "DSM"),
            GoogleHotword => write!(f, "GHD"),
        }
    }
}

impl ComponentType {
    // Match component controls by names per type.
    fn match_control_name(&self, name: &str) -> bool {
        use ComponentType::*;

        // The expected keyword list will be used to pick the corresponding
        // controls for the given component type out of candicates (if one's
        // name contains any keyword in list, due to variants of naming rules by
        // IPC versions)
        let keywords: Vec<&str> = match self {
            NoiseCancellation => vec!["rtnr_enable"],
            EchoCancellation => vec!["google_rtc_audio_processing"],
            DynamicRangeCompression => vec!["multiband_drc_enable"],
            IIREqualizer => vec!["eqiir"],
            WavesPostProcessor => vec!["waves", "maxxchrome"],
            DTSPostProcessor => vec!["dts"],
            DSMSmartAmp => vec!["smart_amp", "dsm data"],
            GoogleHotword => vec!["ghd"],
        };

        let name_lower = name.to_lowercase();
        for keyword in keywords.iter() {
            if name_lower.contains(keyword) {
                return true;
            }
        }
        return false;
    }

    // Whether support state inspectation via bytes controls.
    fn is_bytes_inspectable(&self) -> bool {
        use ComponentType::*;

        return match self {
            EchoCancellation => true,
            IIREqualizer => true,
            _ => false,
        };
    }

    // Inspect component state per type from the config blob stored in the bytes
    // control, as long as the byte address of enabled flag is known.
    fn inspect_from_bytes(&self, sofctl_out: &str) -> Option<bool> {
        use ComponentType::*;

        match self {
            EchoCancellation => {
                // For AEC, the enabled flag is as the 6th byte on config blob
                //
                // Capture the 6th byte in hex format from sof-ctl output, check
                // value "00" means off, "01" is on, others are unexpected.
                let re = Regex::new(
                    r"(?m)^00000000 (?:[[:xdigit:]]{4} ){2}([[:xdigit:]]{2})[[:xdigit:]]{2}",
                )
                .unwrap();
                if let Some(caps) = re.captures(sofctl_out) {
                    return match caps[1].to_string().to_lowercase().as_str() {
                        "00" => Some(false),
                        "01" => Some(true),
                        _ => None,
                    };
                }
                None
            }
            IIREqualizer => {
                // For EQ, per-channel response indices are located on the 8th
                // Int32 and successive ones. Index -1 is specific to
                // passthrough, a.k.a. EQ effect off.
                //
                // Capture the 32th byte in hex format from sof-ctl output, check
                // value "ff" means off, others are on.
                let re = Regex::new(
                    r"(?m)^00000010 (?:[[:xdigit:]]{4} ){6}[[:xdigit:]]{2}([[:xdigit:]]{2})",
                )
                .unwrap();
                if let Some(caps) = re.captures(sofctl_out) {
                    return match caps[1].to_string().to_lowercase().as_str() {
                        "ff" => Some(false),
                        _ => Some(true),
                    };
                }
                None
            }
            _ => None,
        }
    }
}

#[derive(PartialEq, Debug, Args)]
pub struct CstateOptions {
    /// The component type
    #[arg(value_enum)]
    pub component: Option<ComponentType>,
    /// Print "On"/"Off"/"Exists" for the state, return error if not exist
    #[arg(long, short, requires = "component")]
    pub expect: bool,
}

#[derive(PartialEq, Debug, Clone)]
enum ControlState {
    /// Control detected but unable to judge state
    Exists,
    /// Control in off-state (disabled)
    Off,
    /// Control in on-state (enabled)
    On,
}

impl fmt::Display for ControlState {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ControlState::Exists => write!(f, "exists"),
            ControlState::Off => write!(f, "is off"),
            ControlState::On => write!(f, "is ON"),
        }
    }
}

#[derive(PartialEq, Debug, Clone)]
struct Control {
    /// The card index it belongs
    card: String,
    /// The control index on amixer API
    numid: String,
    /// The control name on amixer API
    name: String,
    /// The control state
    state: ControlState,
}

impl fmt::Display for Control {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{:<6}; detected card{} CTL numid={},name='{}'",
            self.state.to_string(),
            self.card,
            self.numid,
            self.name
        )
    }
}

// Get card indices by command `alsa_helpers -l` (list all cards)
fn get_cards() -> Result<Vec<String>> {
    let output = Command::new("alsa_helpers")
        .args(["-l"])
        .stdin(Stdio::null())
        .output()
        .context("failed running `alsa_helpers`")?;

    anyhow::ensure!(
        output.status.success(),
        format!(
            "`alsa_helpers` has error: {}",
            String::from_utf8_lossy(&output.stderr)
        )
    );

    // The example of stdout:
    // cmd> alsa_helpers -l
    // 0,sof-rt5682
    let re = Regex::new(r"(?m)^\s*(\d+),(\S+)$").unwrap();

    let stdout = String::from_utf8_lossy(&output.stdout);
    let ids: Vec<String> = re
        .captures_iter(&stdout)
        .map(|m| m[1].to_string())
        .collect();
    return Ok(ids);
}

// Get mixer entries for SOF control candidates by command
// `amixer -c <CARD> contents`
fn get_control_candidates(card: &str) -> Result<Vec<Control>> {
    let output = Command::new("amixer")
        .args(["-c", &card, "contents"])
        .stdin(Stdio::null())
        .output()
        .context(format!("failed running `amixer` on card {}", &card))?;

    anyhow::ensure!(
        output.status.success(),
        format!(
            "`amixer` has error: {}",
            String::from_utf8_lossy(&output.stderr)
        )
    );

    // The example of stdout:
    // cmd> amixer -c 0 contents
    // ...
    // numid=40,iface=MIXER,name='eqiir.14.1 DMIC0 Capture IIR Eq'
    //  ; type=BYTES,access=-----RW-,values=1024
    //  ; ASoC TLV Byte control, skipping bytes dump
    // numid=52,iface=MIXER,name='RTNR10.0 rtnr_enable_10'
    //  ; type=BOOLEAN,access=rw------,values=1
    //  : values=off
    // ...
    let re = Regex::new(
        r"(?m)^numid=(\d+),iface=MIXER,name='([[:print:]]+)'
[[:space:]]+; type=(BOOLEAN|BYTES),\S+
[[:space:]]+[:;]{1} ([[:print:]]+)$",
    )
    .unwrap();

    let stdout = String::from_utf8_lossy(&output.stdout);
    let mut controls: Vec<Control> = Vec::new();
    for m in re.captures_iter(&stdout) {
        // m[1]:numid, m[2]:name, m[3]:type, m[4]:note in the 3rd line
        let state = parse_state(&m[3], &m[4])?;
        match state {
            Some(s) => {
                let ctl = Control {
                    card: String::from(card),
                    numid: String::from(&m[1]),
                    name: String::from(&m[2]),
                    state: s,
                };
                controls.push(ctl);
            }
            None => (),
        }
    }
    return Ok(controls);
}

fn parse_state(mtype: &str, note: &str) -> Result<Option<ControlState>> {
    match mtype {
        "BOOLEAN" => {
            let boolean_re = Regex::new(r"values=(on|off)").unwrap();
            if let Some(caps) = boolean_re.captures(note) {
                if caps[1].to_string() == "on" {
                    return Ok(Some(ControlState::On));
                }
                return Ok(Some(ControlState::Off));
            }
            anyhow::bail!("cannot parse bool from `{note}`");
        }
        "BYTES" => {
            let bytes_re = Regex::new(r"ASoC TLV Byte control").unwrap();
            if bytes_re.is_match(note) {
                return Ok(Some(ControlState::Exists));
            }
            // This bytes control is not an SOF control, just ignore it.
            return Ok(None);
        }
        &_ => std::unreachable!(),
    }
}

// Inspect the component state for bytes control by command
// `sof-ctl -D hw:<CARD> -n <NUMID> -b -r`
fn with_inspect_state(ctype: &ComponentType, ctl: &Control) -> Result<Control> {
    // Bypass state inspectation for a control when its state is already
    // determined, or its type is not inspectable.
    if ctl.state != ControlState::Exists || !ctype.is_bytes_inspectable() {
        return Ok(ctl.clone());
    }

    let output = Command::new("sof-ctl")
        .args([
            "-D",
            format!("hw:{}", ctl.card).as_str(),
            "-n",
            &ctl.numid,
            "-b",
            "-r",
        ])
        .stdin(Stdio::null())
        .output()
        .context("failed running `sof-ctl`")?;

    anyhow::ensure!(
        output.status.success(),
        format!(
            "`sof-ctl` has error: {}",
            String::from_utf8_lossy(&output.stderr)
        )
    );

    let stdout = String::from_utf8_lossy(&output.stdout);
    let state = match ctype.inspect_from_bytes(&stdout) {
        Some(true) => ControlState::On,
        Some(false) => ControlState::Off,
        None => anyhow::bail!("failed inspecting state for control {}", &ctl.name),
    };

    Ok(Control {
        card: ctl.card.clone(),
        numid: ctl.numid.clone(),
        name: ctl.name.clone(),
        state: state,
    })
}

fn print_comp_state(ctype: &ComponentType, ctl: &Control) {
    let state_sign = match ctl.state {
        ControlState::On => "+",
        ControlState::Off => "-",
        _ => " ",
    };
    println!(" {} {:<6} {}", state_sign, ctype.to_string(), ctl);
}

fn display_comp_state(ctype: &ComponentType, controls: &[Control], expect: bool) -> Result<usize> {
    // Pick out the corresponding controls for the given component type, then
    // inspect the component state for bytes control.
    let comp_ctls: Result<Vec<Control>, _> = controls
        .iter()
        .filter(|ctl| ctype.match_control_name(&ctl.name))
        .map(|ctl| with_inspect_state(ctype, ctl))
        .collect();
    let comp_ctls = comp_ctls?;

    if expect {
        anyhow::ensure!(!comp_ctls.is_empty(), "no control is detected");
        anyhow::ensure!(
            comp_ctls.windows(2).all(|c| c[0].state == c[1].state),
            "state conflict detected; suggest retry command without `--expect`"
        );
        println!("{:?}", comp_ctls[0].state);
    } else {
        comp_ctls.iter().for_each(|c| print_comp_state(ctype, c));
    }
    Ok(comp_ctls.len())
}

pub fn cstate(args: CstateOptions) -> Result<()> {
    let cards = get_cards()?;
    anyhow::ensure!(!cards.is_empty(), "cannot detect any sound card");

    let mut controls: Vec<Control> = Vec::new();
    for card in cards.iter() {
        let ctls = get_control_candidates(card)?;
        controls.extend(ctls);
    }

    match args.component {
        Some(ctype) => {
            return match display_comp_state(&ctype, &controls[..], args.expect) {
                Ok(0) => {
                    if !args.expect {
                        println!("{ctype} doesn't exist");
                    }
                    return Ok(());
                }
                Ok(_) => Ok(()),
                Err(e) => Err(e),
            }
        }
        // Display all component states if not specified
        None => {
            println!("list all available components:");
            for ctype in ComponentType::value_variants().iter() {
                let _ = display_comp_state(ctype, &controls[..], false)?;
            }
            return Ok(());
        }
    }
}
