// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
include!(concat!(env!("OUT_DIR"), "/plugin_processor_binding.rs"));

impl std::fmt::Display for status {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match *self {
            status::StatusOk => write!(f, "ok"),
            status::ErrInvalidProcessor => write!(f, "invalid processor"),
            status::ErrOutOfMemory => write!(f, "out of memory"),
            status::ErrInvalidConfig => write!(f, "invalid config"),
            status::ErrInvalidArgument => write!(f, "invalid argument"),
            other => write!(f, "unknown plugin processor error code {}", other.0),
        }
    }
}
