// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// An audio sample.
pub trait Sample: dasp_sample::Sample + Default {}

impl Sample for f32 {}
impl Sample for i32 {}
impl Sample for i16 {}
impl Sample for i8 {}
impl Sample for u8 {}

/// A [`Sample`] supporting [`core::ops::Neg`].
pub trait SignedSample: Sample + dasp_sample::SignedSample {}

impl SignedSample for f32 {}
impl SignedSample for i32 {}
impl SignedSample for i16 {}
impl SignedSample for i8 {}
