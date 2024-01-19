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

void cras_s2_set_ap_nc_featured_allowed(bool allowed);

void cras_s2_set_ap_nc_segmentation_allowed(bool allowed);

bool cras_s2_get_ap_nc_allowed(void);

char *cras_s2_dump_json(void);

#endif /* CRAS_SERVER_S2_S2_H_ */

#ifdef __cplusplus
}
#endif
