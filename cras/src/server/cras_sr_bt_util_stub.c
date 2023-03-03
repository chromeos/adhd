/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_sr.h"
#include "cras/src/server/cras_sr_bt_util.h"

enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS cras_sr_bt_can_be_enabled() {
  // Pretends disabled.
  return CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_DISABLED;
}

struct cras_sr_model_spec cras_sr_bt_get_model_spec(
    enum cras_sr_bt_model model) {
  return (struct cras_sr_model_spec){};
}

void cras_sr_bt_send_uma_log(struct cras_iodev* iodev,
                             const enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS status,
                             bool is_enabled) {}
