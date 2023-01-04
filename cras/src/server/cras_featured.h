/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CRAS_FEATURED_H_
#define CRAS_FEATURED_H_

#include <stdbool.h>

bool get_hfp_offload_feature_enabled();

bool get_hfp_mic_sr_feature_enabled();

bool get_flexible_loopback_feature_enabled();

#endif /* CRAS_FEATURED_H_ */
