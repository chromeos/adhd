// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::path::Path;
use std::path::PathBuf;

use anyhow::Context;
use audio_processor::cdcfg;
use serde::Serialize;

#[derive(Serialize)]
pub enum BeamformingConfig {
    Supported(BeamformingProperties),
    Unsupported { reason: String },
}

impl Default for BeamformingConfig {
    fn default() -> Self {
        BeamformingConfig::Unsupported {
            reason: "not probed yet".to_string(),
        }
    }
}

impl BeamformingConfig {
    pub fn probe(cras_config_dir: &str) -> Self {
        match BeamformingProperties::probe(cras_config_dir) {
            Ok(properties) => Self::Supported(properties),
            Err(err) => Self::Unsupported {
                reason: format!("{err:#}"),
            },
        }
    }
}

#[derive(Default, Serialize)]
pub struct BeamformingProperties {
    /// The pipeline path to use for beamforming.
    pub pipeline_path: PathBuf,
    /// The set of DLCs required for beamforming to function.
    pub required_dlcs: HashSet<String>,
}

impl BeamformingProperties {
    fn probe(cras_config_dir: &str) -> anyhow::Result<Self> {
        let board_ini = cras_ini::parse_file(
            &Path::new("/etc/cras")
                .join(cras_config_dir)
                .join("board.ini"),
        )
        .context("cannot parse board.ini")?;
        let pipeline_file = board_ini
            .get("processing")
            .context("processing section not found in board.ini")?
            .get("beamforming_pipeline_file")
            .context("beamforming_pipeline_file not found in [processing] in board.ini")?
            .as_str()
            .to_string();
        let pipeline_path = Path::new("/etc/cras/processor").join(&pipeline_file);
        let required_dlcs = cdcfg::get_required_dlcs(&pipeline_path)
            .context("cannot get required DLCs from pipeline file")?;
        Ok(BeamformingProperties {
            pipeline_path,
            required_dlcs,
        })
    }
}
