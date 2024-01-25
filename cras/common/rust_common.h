// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_COMMON_RUST_COMMON_H_
#define CRAS_COMMON_RUST_COMMON_H_

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

/**
 * Initialize logging for cras_rust.
 * Recommended to be called before all other cras_rust functions.
 */
int cras_rust_init_logging(void);

/**
 * Pseudonymize the stable_id using the global salt.
 * Returns the salted stable_id.
 */
uint32_t pseudonymize_stable_id(uint32_t stable_id);

/**
 * Initialize logging for cras_rust.
 * Recommended to be called before all other cras_rust functions.
 */
int cras_rust_init_logging(void);

/**
 * Pseudonymize the stable_id using the global salt.
 * Returns the salted stable_id.
 */
uint32_t pseudonymize_stable_id(uint32_t stable_id);

#endif /* CRAS_COMMON_RUST_COMMON_H_ */

#ifdef __cplusplus
}
#endif