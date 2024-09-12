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

#define NUM_CRAS_DLCS 3

#define CRAS_DLC_ID_STRING_MAX_LENGTH 50

/**
 * All supported DLCs in CRAS.
 */
enum CrasDlcId {
  CrasDlcSrBt,
  CrasDlcNcAp,
  CrasDlcIntelligoBeamforming,
};

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

typedef uint64_t CRAS_STREAM_ACTIVE_AP_EFFECT;
#define CRAS_STREAM_ACTIVE_AP_EFFECT_ECHO_CANCELLATION (uint64_t)(1 << 0)
#define CRAS_STREAM_ACTIVE_AP_EFFECT_NOISE_SUPPRESSION (uint64_t)(1 << 1)
#define CRAS_STREAM_ACTIVE_AP_EFFECT_VOICE_ACTIVITY_DETECTION (uint64_t)(1 << 2)
#define CRAS_STREAM_ACTIVE_AP_EFFECT_NEGATE (uint64_t)(1 << 3)
#define CRAS_STREAM_ACTIVE_AP_EFFECT_NOISE_CANCELLATION (uint64_t)(1 << 4)
#define CRAS_STREAM_ACTIVE_AP_EFFECT_STYLE_TRANSFER (uint64_t)(1 << 5)
#define CRAS_STREAM_ACTIVE_AP_EFFECT_BEAMFORMING (uint64_t)(1 << 6)
#define CRAS_STREAM_ACTIVE_AP_EFFECT_PROCESSOR_OVERRIDDEN (uint64_t)(1 << 7)

typedef uint32_t CRAS_NC_PROVIDER;
#define CRAS_NC_PROVIDER_NONE (uint32_t)0
#define CRAS_NC_PROVIDER_DSP (uint32_t)(1 << 0)
#define CRAS_NC_PROVIDER_AP (uint32_t)(1 << 1)
#define CRAS_NC_PROVIDER_AST (uint32_t)(1 << 2)
#define CRAS_NC_PROVIDER_BF (uint32_t)(1 << 3)

typedef uint32_t EFFECT_TYPE;
#define EFFECT_TYPE_NONE (uint32_t)0
#define EFFECT_TYPE_NOISE_CANCELLATION (uint32_t)(1 << 0)
#define EFFECT_TYPE_HFP_MIC_SR (uint32_t)(1 << 1)
#define EFFECT_TYPE_STYLE_TRANSFER (uint32_t)(1 << 2)
#define EFFECT_TYPE_BEAMFORMING (uint32_t)(1 << 3)

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
 * Returns the names of active effects as a string.
 * The resulting string should be freed with cras_rust_free_string.
 */
char *cras_stream_active_ap_effects_string(CRAS_STREAM_ACTIVE_AP_EFFECT effect);

/**
 * Returns the name of the NC provider as a string.
 * The ownership of the string is static in Rust, so no need to free in C.
 */
const char *cras_nc_provider_to_str(CRAS_NC_PROVIDER nc_provider);

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
 * Returns the names of active effects as a string.
 * The resulting string should be freed with cras_rust_free_string.
 */
char *cras_stream_active_ap_effects_string(CRAS_STREAM_ACTIVE_AP_EFFECT effect);

/**
 * Returns the name of the NC provider as a string.
 * The ownership of the string is static in Rust, so no need to free in C.
 */
const char *cras_nc_provider_to_str(CRAS_NC_PROVIDER nc_provider);

#endif  /* CRAS_COMMON_RUST_COMMON_H_ */

#ifdef __cplusplus
}
#endif
