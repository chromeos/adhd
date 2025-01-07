// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;

fn parse_cpu_model_name(cpuinfo: &str) -> &str {
    for line in cpuinfo.lines() {
        let Some((key, value)) = line.split_once(": ") else {
            continue;
        };
        if key.trim_end() == "model name" {
            return value;
        }
    }
    ""
}

pub fn probe_cpu_model_name() -> anyhow::Result<String> {
    let cpuinfo = std::fs::read_to_string("/proc/cpuinfo").context("cannot read cpuinfo")?;
    Ok(parse_cpu_model_name(&cpuinfo).to_string())
}

pub fn probe_board_name() -> anyhow::Result<String> {
    // TODO(b/259385071): use cros_config /audio/main board
    Ok(std::fs::read_to_string(
        "/run/chromeos-config/v1/arc/build-properties/product",
    )?)
}

#[cfg(test)]
mod tests {
    use super::parse_cpu_model_name;

    #[test]
    fn test_parse_cpu_model_name() {
        assert_eq!(
            parse_cpu_model_name(
                r#"model           : 186
model name      : Intel(R) Core(TM) 7 150U
stepping        : 3
"#
            ),
            "Intel(R) Core(TM) 7 150U"
        );
        assert_eq!(parse_cpu_model_name(""), "");
    }
}
