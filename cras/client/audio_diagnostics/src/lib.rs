// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub struct Analysis {
    pub name: String,            // Name of the event for FRA to grep.
    pub description: String,     // Description of what this event mean.
    pub suggestion: String,      // Suggestion for human to do when debugging.
    pub additional_info: String, // Additional info for further debugging.
}

impl std::fmt::Display for Analysis {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "ANALYSIS: {}: {}. SUGGESTION: {}. {}",
            self.name, self.description, self.suggestion, self.additional_info
        )
    }
}

pub mod uptime;
