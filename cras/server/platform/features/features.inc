// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Syntax:
// DEFINE_FEATURE(name, default_enabled)
DEFINE_FEATURE(CrOSLateBootUnknown, false)
DEFINE_FEATURE(CrOSLateBootDisabledByDefault, false)
DEFINE_FEATURE(CrOSLateBootEnabledByDefault, true)
DEFINE_FEATURE(CrOSLateBootAudioHFPOffload, false)
DEFINE_FEATURE(CrOSLateBootAudioAPNoiseCancellation, false)
DEFINE_FEATURE(CrOSLateBootAudioStyleTransfer, true)
DEFINE_FEATURE(CrOSLateBootAudioA2DPAdvancedCodecs, false)
DEFINE_FEATURE(CrOSLateBootAudioEmptyAPMForCrasProcessor, true)
DEFINE_FEATURE(CrOSLateBootAudioSuppressSetRTCAudioActive, false)
DEFINE_FEATURE(CrOSLateBootAudioOffloadCrasDSPToSOF, false)
DEFINE_FEATURE(CrOSLateBootCrasProcessorDedicatedThread, true)
DEFINE_FEATURE(CrOSLateBootCrasProcessorWavDump, false)
DEFINE_FEATURE(CrOSLateBootBluetoothAudioLEAudioOnly, false)
DEFINE_FEATURE(CrOSLateBootCrasAecFixedCaptureDelay320Samples, false)
