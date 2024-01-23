// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_FRA_H_
#define CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_FRA_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

enum CRAS_FRA_SIGNAL {
  PeripheralsUsbSoundCard = 0,
  USBAudioConfigureFailed,
  USBAudioListOutputNodeFailed,
  USBAudioStartFailed,
  USBAudioSoftwareVolumeAbnormalRange,
  USBAudioSoftwareVolumeAbnormalSteps,
  USBAudioUCMNoJack,
  USBAudioUCMWrongJack,
  USBAudioResumeFailed,
  ActiveOutputDevice,
  ActiveInputDevice,
  AudioThreadEvent,
  ALSAUCMCaptureChannelMapExceedsNumChannels,
  SecondaryHciDeviceChanged,
};

struct cras_fra_kv_t {
  const char *key;
  const char *value;
};

/**
 * This function is called from C code to log a FRA event.
 *
 * # Arguments
 *
 * * `signal` - The type of FRA event to log.
 * * `num` - The number of context pairs.
 * * `context_arr` - A pointer to an array of `KeyValuePair` structs.
 *
 * # Safety
 * The memory pointed by context_arr must contains valid array of `KeyValuePair` structs.
 * The memory pointed by KeyValuePair::key and KeyValuePair::value must contains a valid nul terminator at the end of the string.
 */
void fralog(enum CRAS_FRA_SIGNAL signal,
            size_t num,
            const struct cras_fra_kv_t *context_arr);

#endif /* CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_FRA_H_ */

#ifdef __cplusplus
}
#endif
