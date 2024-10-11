// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::proto::cras_processor::CrasProcessorOverride;

fn read_config(path: &str) -> CrasProcessorOverride {
    match std::fs::read(path) {
        Err(_) => Default::default(),
        Ok(data) => match String::from_utf8(data) {
            Err(err) => {
                log::error!("cannot read from {path}: {err:?}");
                Default::default()
            }
            Ok(text) => match protobuf::text_format::parse_from_str(&text) {
                Ok(msg) => msg,
                Err(err) => {
                    log::error!("cannot parse from {path}: {err:?}");
                    Default::default()
                }
            },
        },
    }
}

pub fn read_system_config() -> CrasProcessorOverride {
    read_config("/etc/cras/processor_override.txtpb")
}
