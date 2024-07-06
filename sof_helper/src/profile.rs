// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::path::PathBuf;
use std::str;

use anyhow::Context;
use anyhow::Result;

const SOF_DEBUGFS_PATH: &str = "/sys/kernel/debug/sof";
const FW_PROFILE_DIR: &str = "fw_profile";

const FW_PREFIX: &str = "fw_path";
const FW_NAME: &str = "fw_name";
const TPLG_PREFIX: &str = "tplg_path";
const TPLG_NAME: &str = "tplg_name";

const FIRMWARE_PATH: &str = "/lib/firmware";

/// The fw/tplg build file artifact for SOF profile
#[derive(serde::Serialize, Debug)]
struct Artifact {
    /// The name of loading path stored in debugfs
    name: String,
    /// The prefix of loading path stored in debugfs
    prefix: String,
    /// Store true if loading path is symbolic link
    is_symlink: bool,
    /// The canonicalized path with symlink resolved
    resolved_path: String,
}

impl Artifact {
    fn try_from_name_prefix(name: &str, prefix: &str) -> Result<Self> {
        let base = Path::new(FIRMWARE_PATH);
        let dirpath = base.join(prefix);
        let path = dirpath.join(name);
        let is_symlink = path.is_symlink();
        let resolved_path = path
            .canonicalize()
            .with_context(|| format!("failed to resolve path {}", path.display()))?;

        Ok(Artifact {
            name: name.to_string(),
            prefix: prefix.to_string(),
            is_symlink: is_symlink,
            resolved_path: resolved_path.to_str().unwrap().to_string(),
        })
    }
}

fn print_artifact(artifact: Artifact, build: &str) {
    println!("[{}]", build);
    println!("  name: {}", artifact.name.as_str());
    println!("  prefix: {}", artifact.prefix.as_str());
    println!("  is_symlink? {}", artifact.is_symlink);
    println!("  resolved_path: {}", artifact.resolved_path.as_str());
}

fn read_entry(path: PathBuf, name: &str) -> Result<String> {
    let entry_path = path.join(name);

    let data: Vec<u8> = std::fs::read(entry_path.clone())
        .with_context(|| format!("failed to read {}", entry_path.display()))?;
    let data_str = str::from_utf8(&data).with_context(|| "failed to convert bytes")?;

    Ok(data_str.trim_end().to_string())
}

pub fn profile(json: bool) -> Result<()> {
    let basepath = Path::new(SOF_DEBUGFS_PATH);
    anyhow::ensure!(
        basepath.exists(),
        "cannot detect SOF firmware; device is not SOF-backed?"
    );

    let profile_path = basepath.join(FW_PROFILE_DIR);
    anyhow::ensure!(
        profile_path.exists(),
        "cannot find profile on debugfs; kernel version < 6.1?"
    );

    let fw_name = read_entry(profile_path.clone(), FW_NAME)?;
    let fw_prefix = read_entry(profile_path.clone(), FW_PREFIX)?;
    let fw_artifact = Artifact::try_from_name_prefix(&fw_name, &fw_prefix)?;

    let tplg_name = read_entry(profile_path.clone(), TPLG_NAME)?;
    let tplg_prefix = read_entry(profile_path.clone(), TPLG_PREFIX)?;
    let tplg_artifact = Artifact::try_from_name_prefix(&tplg_name, &tplg_prefix)?;

    if json {
        let profile_json = serde_json::json!({
            "fw": fw_artifact,
            "tplg": tplg_artifact
        });
        println!("{}", profile_json.to_string());
    } else {
        println!("SOF Profile:");
        print_artifact(fw_artifact, "fw");
        print_artifact(tplg_artifact, "tplg");
    }

    Ok(())
}
