// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SERVER_S2_S2_H_
#define CRAS_SERVER_S2_S2_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "cras/common/rust_common.h"

void cras_s2_set_ap_nc_featured_allowed(bool allowed);

void cras_s2_set_ap_nc_segmentation_allowed(bool allowed);

void cras_s2_set_ap_nc_feature_tier_allowed(bool allowed);

void cras_s2_set_dlc_installed(enum CrasDlcId dlc);

bool cras_s2_get_ap_nc_allowed(void);

void cras_s2_set_style_transfer_featured_allowed(bool allowed);

bool cras_s2_get_style_transfer_allowed(void);

bool cras_s2_get_style_transfer_supported(void);

void cras_s2_set_style_transfer_enabled(bool enabled);

bool cras_s2_get_style_transfer_enabled(void);

void cras_s2_init(void);

bool cras_s2_get_beamforming_supported(void);

bool cras_s2_get_beamforming_allowed(void);

char *cras_s2_dump_json(void);

void cras_s2_reset_for_testing(void);

void cras_s2_set_nc_standalone_mode(bool nc_standalone_mode);

bool cras_s2_get_nc_standalone_mode(void);

void cras_s2_set_non_dsp_aec_echo_ref_dev_alive(bool non_dsp_aec_echo_ref_dev_alive);

bool cras_s2_get_non_dsp_aec_echo_ref_dev_alive(void);

void cras_s2_set_aec_on_dsp_is_disallowed(bool aec_on_dsp_is_disallowed);

bool cras_s2_get_aec_on_dsp_is_disallowed(void);

bool cras_s2_get_dsp_input_effects_blocked(void);

#endif  /* CRAS_SERVER_S2_S2_H_ */

#ifdef __cplusplus
}
#endif
