// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(clippy::unreadable_literal)]
#![allow(clippy::cognitive_complexity)]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
// FIXME: See b/239850356.
// This should be removed when that bug is fixed.
#![warn(unaligned_references)]

pub mod bindings;
#[allow(unused_imports)]
pub use bindings::sof_abi_hdr;
