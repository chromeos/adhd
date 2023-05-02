/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdbool.h>
#include <stddef.h>

#include "cras/src/server/rust/include/cras_dlc.h"

bool cras_dlc_install(enum CrasDlcId id) {
  return false;
}

bool cras_dlc_is_available(enum CrasDlcId id) {
  return false;
}

const char* cras_dlc_get_root_path(enum CrasDlcId id) {
  return NULL;
}
