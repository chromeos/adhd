// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::binding;

#[derive(thiserror::Error, Debug)]
pub enum PluginError {
    #[error("binding: {0}")]
    /// Error returned from the C binding.
    Binding(binding::status),

    #[error("unexpected null: {0}")]
    UnexpectedNull(String),

    #[error("too many channels {0}")]
    TooManyChannels(usize),

    #[error("{func}: {error}")]
    /// Error from dl*() C functions.
    Dl { func: String, error: String },
}

#[cfg(test)]
mod tests {
    use super::binding;
    use super::PluginError;

    #[test]
    fn plugin_processor_error() {
        assert_eq!(
            PluginError::Binding(binding::status::StatusOk).to_string(),
            "binding: ok"
        );

        assert_eq!(
            PluginError::Binding(binding::status::ErrInvalidProcessor).to_string(),
            "binding: invalid processor"
        );

        assert_eq!(
            PluginError::Binding(binding::status::ErrOutOfMemory).to_string(),
            "binding: out of memory"
        );

        assert_eq!(
            PluginError::Binding(binding::status::ErrInvalidConfig).to_string(),
            "binding: invalid config",
        );

        assert_eq!(
            PluginError::Binding(binding::status::ErrInvalidArgument).to_string(),
            "binding: invalid argument",
        );

        assert_eq!(
            PluginError::Binding(binding::status(999)).to_string(),
            "binding: unknown plugin processor error code 999"
        );
    }
}
