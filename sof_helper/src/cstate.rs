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
    /// The information note from `amixer content` output
    note: String,
}

impl fmt::Display for Control {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "detected card{} CTL numid={},name='{}'",
            self.card, self.numid, self.name
        )
    }
}

/// Trait for component types that can be recognized by the exposed control and
/// inspected the real-time state.
trait Component {
    /// Get the enum type of this component.
    fn get_type(&self) -> ComponentType;
    /// Return true if this component has the given control.
    fn has_control(&self, ctl: &Control) -> bool;
    /// Get the component state according to the control.
    fn get_state(&self, ctl: &Control) -> Result<ControlState>;
}

struct CompNoiseCancellation {}
impl Component for CompNoiseCancellation {
    fn get_type(&self) -> ComponentType {
        ComponentType::NoiseCancellation
    }

    fn has_control(&self, ctl: &Control) -> bool {
        match_any_substring(&ctl.name, &["rtnr_enable"])
    }

    fn get_state(&self, ctl: &Control) -> Result<ControlState> {
        get_boolean_state(ctl)
    }
}

struct CompEchoCancellation {}
impl Component for CompEchoCancellation {
    fn get_type(&self) -> ComponentType {
        ComponentType::EchoCancellation
    }

    fn has_control(&self, ctl: &Control) -> bool {
        match_any_substring(&ctl.name, &["google_rtc_audio_processing"])
    }

    fn get_state(&self, ctl: &Control) -> Result<ControlState> {
        // AEC has bytes-typed control only. The enabled flag is specified by the
        // 6th byte on config blob. By capturing that byte in hex format via sof-ctl,
        // "00" means off, "01" is on, others are unexpected.
        let stdout = read_bytes_control_data(ctl)?;
        let re =
            Regex::new(r"(?m)^00000000 (?:[[:xdigit:]]{4} ){2}([[:xdigit:]]{2})[[:xdigit:]]{2}")
                .unwrap();
        if let Some(caps) = re.captures(&stdout) {
            return match caps[1].to_string().to_lowercase().as_str() {
                "00" => Ok(ControlState::Off),
                "01" => Ok(ControlState::On),
                byte => anyhow::bail!("failed getting state by unrecognized byte: {}", byte),
            };
        }
        anyhow::bail!("failed getting state")
    }
}

struct CompDynamicRangeCompression {}
impl Component for CompDynamicRangeCompression {
    fn get_type(&self) -> ComponentType {
        ComponentType::DynamicRangeCompression
    }

    fn has_control(&self, ctl: &Control) -> bool {
        match_any_substring(&ctl.name, &["multiband_drc_enable"])
    }

    fn get_state(&self, ctl: &Control) -> Result<ControlState> {
        get_boolean_state(ctl)
    }
}

struct CompIIREqualizer {}
impl Component for CompIIREqualizer {
    fn get_type(&self) -> ComponentType {
        ComponentType::IIREqualizer
    }

    fn has_control(&self, ctl: &Control) -> bool {
        match_any_substring(&ctl.name, &["eqiir"])
    }

    fn get_state(&self, ctl: &Control) -> Result<ControlState> {
        // EQ has the only bytes-typed control, addressing per-channel response
        // indices on the 8th Int32 and successive ones. Index -1 is specific to
        // passthrough, a.k.a. EQ effect off.
        //
        // Capture the 32th byte in hex format from sof-ctl output, check value
        // "ff" stands for effect off, on otherwise.
        let stdout = read_bytes_control_data(ctl)?;
        let re =
            Regex::new(r"(?m)^00000010 (?:[[:xdigit:]]{4} ){6}[[:xdigit:]]{2}([[:xdigit:]]{2})")
                .unwrap();
        if let Some(caps) = re.captures(&stdout) {
            return match caps[1].to_string().to_lowercase().as_str() {
                "ff" => Ok(ControlState::Off),
                _ => Ok(ControlState::On),
            };
        }
        anyhow::bail!("failed getting state")
    }
}

struct CompWavesPostProcessor {}
impl Component for CompWavesPostProcessor {
    fn get_type(&self) -> ComponentType {
        ComponentType::WavesPostProcessor
    }

    fn has_control(&self, ctl: &Control) -> bool {
        match_any_substring(&ctl.name, &["waves", "maxxchrome"])
    }

    fn get_state(&self, ctl: &Control) -> Result<ControlState> {
        get_bytes_state(ctl)
    }
}

struct CompDTSPostProcessor {}
impl Component for CompDTSPostProcessor {
    fn get_type(&self) -> ComponentType {
        ComponentType::DTSPostProcessor
    }

    fn has_control(&self, ctl: &Control) -> bool {
        match_any_substring(&ctl.name, &["dts"])
    }

    fn get_state(&self, ctl: &Control) -> Result<ControlState> {
        get_bytes_state(ctl)
    }
}

struct CompDSMSmartAmp {}
impl Component for CompDSMSmartAmp {
    fn get_type(&self) -> ComponentType {
        ComponentType::DSMSmartAmp
    }

    fn has_control(&self, ctl: &Control) -> bool {
        match_any_substring(&ctl.name, &["smart_amp", "dsm data"])
    }

    fn get_state(&self, ctl: &Control) -> Result<ControlState> {
        get_bytes_state(ctl)
    }
}

struct CompGoogleHotword {}
impl Component for CompGoogleHotword {
    fn get_type(&self) -> ComponentType {
        ComponentType::GoogleHotword
    }

    fn has_control(&self, ctl: &Control) -> bool {
        match_any_substring(&ctl.name, &["ghd"])
    }

    fn get_state(&self, ctl: &Control) -> Result<ControlState> {
        get_bytes_state(ctl)
    }
}

// This array should contain all Component implementions.
const COMPONENTS: [&'static dyn Component; 8] = [
    &CompNoiseCancellation {},
    &CompEchoCancellation {},
    &CompDynamicRangeCompression {},
    &CompIIREqualizer {},
    &CompWavesPostProcessor {},
    &CompDTSPostProcessor {},
    &CompDSMSmartAmp {},
    &CompGoogleHotword {},
];

// Return true if |main_str| contains any of sub-string in |substrs|.
fn match_any_substring(main_str: &str, substrs: &[&str]) -> bool {
    let main_str_lower = main_str.to_lowercase();
    for substr in substrs.iter() {
        if main_str_lower.contains(substr) {
            return true;
        }
    }
    return false;
}

// Run command `sof-ctl -D hw:<CARD> -n <NUMID> -b -r` to obtain the control
// bytes data.
fn read_bytes_control_data(ctl: &Control) -> Result<String> {
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
    Ok(stdout.to_string())
}

// A basic implementation of get_state for boolean-typed controls.
fn get_boolean_state(ctl: &Control) -> Result<ControlState> {
    let boolean_re = Regex::new(r"values=(on|off)").unwrap();
    if let Some(caps) = boolean_re.captures(&ctl.note) {
        if caps[1].to_string() == "on" {
            return Ok(ControlState::On);
        }
        return Ok(ControlState::Off);
    }
    anyhow::bail!("failed getting boolean state from: {}", ctl.note)
}

// A basic implementation of get_state for bytes-typed controls.
fn get_bytes_state(ctl: &Control) -> Result<ControlState> {
    let bytes_re = Regex::new(r"ASoC TLV Byte control").unwrap();
    if bytes_re.is_match(&ctl.note) {
        return Ok(ControlState::Exists);
    }
    anyhow::bail!("failed getting bytes state from: {}", ctl.note)
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
fn get_support_components(card: &str) -> Result<Vec<(Control, &dyn Component)>> {
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
    let mut support_comps: Vec<(Control, &dyn Component)> = Vec::new();
    for m in re.captures_iter(&stdout) {
        // m[1]:numid, m[2]:name, m[3]:type, m[4]:note in the 3rd line
        let ctl = Control {
            card: String::from(card),
            numid: String::from(&m[1]),
            name: String::from(&m[2]),
            note: String::from(&m[4]),
        };

        for &comp in COMPONENTS.iter() {
            if comp.has_control(&ctl) {
                support_comps.push((ctl, comp));
                break;
            }
        }
    }
    Ok(support_comps)
}

// For JSON format printing
#[derive(serde::Serialize, Debug)]
struct CompStateInfo {
    component: String,
    state: String,
    mixer: String,
}

fn print_state_json(support_comps: &[(Control, &dyn Component)]) -> Result<()> {
    let mut infos: Vec<CompStateInfo> = Vec::new();
    for (ctl, comp) in support_comps.iter() {
        let state = comp.get_state(&ctl)?;
        infos.push(CompStateInfo {
            component: comp.get_type().to_string(),
            state: format!("{:?}", state),
            mixer: format!("numid={},name='{}'", ctl.numid, ctl.name),
        });
    }
    println!("{}", serde_json::to_string(&infos)?);
    Ok(())
}

#[derive(PartialEq, Debug, Args)]
pub struct CstateOptions {
    /// The component type
    #[arg(value_enum)]
    pub component: Option<ComponentType>,
    /// Print "On"/"Off"/"Exists" for the state, return error if not exist
    #[arg(long, short, requires = "component")]
    pub expect: bool,
    /// Print all available states in JSON format, use this without [COMPONENT]
    #[arg(long, conflicts_with_all = ["component", "expect"])]
    pub json: bool,
}

pub fn cstate(args: CstateOptions) -> Result<()> {
    let cards = get_cards()?;
    anyhow::ensure!(!cards.is_empty(), "cannot detect any sound card");

    let mut support_comps: Vec<(Control, &dyn Component)> = Vec::new();
    for card in cards.iter() {
        let card_comps = get_support_components(card)?;
        support_comps.extend(card_comps);
    }

    if args.json {
        print_state_json(&support_comps[..])?;
        return Ok(());
    }

    let mut expect_state: Option<ControlState> = None;
    for (ctl, comp) in support_comps.iter() {
        let ctype = comp.get_type();

        // Get state for the specific component type if given by args.component
        if args.component.as_ref().is_some_and(|c| *c != ctype) {
            continue;
        }

        let state = comp.get_state(&ctl)?;

        if args.expect {
            anyhow::ensure!(
                state == expect_state.unwrap_or(state.clone()),
                "state conflict detected; suggest retry command without `--expect`"
            );
        } else {
            // Print out the state information for each component.
            let state_sign = match state {
                ControlState::On => "+",
                ControlState::Off => "-",
                _ => " ",
            };
            println!(
                " {} {:<6} {:<6}; {}",
                state_sign,
                ctype.to_string(),
                state.to_string(),
                ctl
            );
        }
        expect_state = Some(state);
    }

    if args.expect {
        anyhow::ensure!(expect_state.is_some(), "no control is detected");
        println!("{:?}", expect_state.unwrap());
    } else if expect_state.is_none() {
        println!("component not found");
    }
    Ok(())
}
