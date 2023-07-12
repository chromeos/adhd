// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use super::Analysis;

/// Analyze CRAS uptime for crash events
///
/// Arguments:
/// * ps_uptime_output: Output from `ps -C cras -o etimes=`.    Example: "   1234"
/// * uptime_output:    Output from `cat /proc/uptime`.         Example: "100.23 200.34"
pub fn analyze(ps_uptime_output: &str, uptime_output: &str) -> Vec<Analysis> {
    // Duration to count as *recently* crashed. Set to 15 minutes.
    const RECENTLY_CRASH_DUR: Duration = Duration::from_secs(15 * 60);
    // Normal delay for CRAS to start counting uptime compared to system uptime. Set to 1 minute.
    const NORMAL_UPTIME_DIFF: Duration = Duration::from_secs(60);

    let mut res: Vec<Analysis> = Vec::new();

    // If CRAS is not running, ps uptime output will be empty.
    if ps_uptime_output.trim().is_empty() {
        res.push(Analysis {
            name: String::from("uptime-cras-not-running"),
            description: String::from("CRAS is not running"),
            suggestion: String::from("Look into crash reports"),
            additional_info: String::new(),
        });
        return res;
    }

    let mut parse_error = false;
    let cras_uptime =
        Duration::from_secs(ps_uptime_output.trim().parse::<u64>().unwrap_or_else(|_| {
            parse_error = true;
            0
        }));
    let sys_uptime = Duration::from_secs_f64(
        uptime_output
            .split_whitespace()
            .next() // Get the first one after split
            .unwrap_or_else(|| {
                parse_error = true;
                "0"
            })
            .parse::<f64>()
            .unwrap_or_else(|_| {
                parse_error = true;
                0.0
            }),
    );
    if parse_error {
        eprintln!(
            "analyze_uptime: parse uptime error. ps_uptime_output={}. uptime_output={}",
            ps_uptime_output, uptime_output
        );
        return res;
    }

    // Count as crash if system and CRAS uptime difference is higher than NORMAL_UPTIME_DIFF.
    // A low CRAS uptime with similar system uptime mean a recent reboot, not a crash.
    if sys_uptime.saturating_sub(cras_uptime) > NORMAL_UPTIME_DIFF {
        if cras_uptime <= RECENTLY_CRASH_DUR {
            res.push(Analysis {
                name: String::from("uptime-cras-recently-crashed"),
                description: String::from("CRAS crashed within the last 15 minutes"),
                suggestion: String::from("Look into crash reports"),
                additional_info: format!(
                    "cras-uptime={:?} sys-uptime={:?}",
                    cras_uptime, sys_uptime
                ),
            });
        } else {
            res.push(Analysis {
                name: String::from("uptime-cras-crashed"),
                description: String::from("CRAS crashed some time ago"),
                suggestion: String::from("Look into crash reports"),
                additional_info: format!(
                    "cras-uptime={:?} sys-uptime={:?}",
                    cras_uptime, sys_uptime
                ),
            });
        }
    }

    res
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_analyze() {
        // Normal: cras = 10000s, system = 10010s
        let ps = "   10000";
        let uptime = "10010.12 20020.12";
        let res = analyze(ps, uptime);
        assert_eq!(res.len(), 0);

        // Recently crashed: cras=600s, system = 10010s
        let ps = "   600";
        let uptime = "10010.12 20020.12";
        let res = analyze(ps, uptime);
        assert_eq!(res.len(), 1);
        assert_eq!(res[0].name, "uptime-cras-recently-crashed");

        // Crashed some time ago: cras=10000s, system = 20000s
        let ps = "   10000";
        let uptime = "20000.12 40000.12";
        let res = analyze(ps, uptime);
        assert_eq!(res.len(), 1);
        assert_eq!(res[0].name, "uptime-cras-crashed");

        // CRAS not running
        let ps = "";
        let uptime = "";
        let res = analyze(ps, uptime);
        assert_eq!(res.len(), 1);
        assert_eq!(res[0].name, "uptime-cras-not-running");

        // Malformed ps uptime
        let ps = "   xxx";
        let uptime = "10000.12 20000.12";
        let res = analyze(ps, uptime);
        assert_eq!(res.len(), 0);

        // Malformed /proc/uptime
        let ps = "   100";
        let uptime = "10000.xx 20000.12";
        let res = analyze(ps, uptime);
        assert_eq!(res.len(), 0);

        // Malformed - CRAS uptime > sys uptime
        let ps = "   10000";
        let uptime = "100.00 200.12";
        let res = analyze(ps, uptime);
        assert_eq!(res.len(), 0);
    }
}
