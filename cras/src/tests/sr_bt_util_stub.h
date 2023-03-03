/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_TESTS_SR_BT_UTIL_STUB_H_
#define CRAS_SRC_TESTS_SR_BT_UTIL_STUB_H_

extern "C" {
#include "cras/src/server/cras_sr_bt_util.h"
}

// The original cras_bt_sr_util.h is included.
// The following functions are added for testing.

void enable_cras_sr_bt();

void disable_cras_sr_bt();

#endif  // CRAS_SRC_TESTS_SR_BT_UTIL_STUB_H_
