/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_SR_BT_UTIL_H_
#define CRAS_SRC_SERVER_CRAS_SR_BT_UTIL_H_

#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_sr.h"

enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS {
  CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK,
  CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_UNSUPPORTED,
  CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_DISABLED,
  CRAS_SR_BT_CAN_BE_ENABLED_STATUS_DLC_UNAVAILABLE
};

/* Checks if cras_sr_bt can be enabled. It will check the dependencies is
 * fulfilled, like whether featured flag is turned on, whether the feature is
 * forced enabled, or whether dlc is ready.
 *
 * Returns:
 *    CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK if all checks pass.
 *    Otherwise, a status that tells the first failed check.
 */
enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS cras_sr_bt_can_be_enabled();

enum cras_sr_bt_model { SR_BT_NBS, SR_BT_WBS };

/* Gets the model spec of the given model.
 *
 * Args:
 *    cras_sr_bt_model - The type of the model.
 *
 * Returns:
 *    The spec of the specified model.
 */
struct cras_sr_model_spec cras_sr_bt_get_model_spec(enum cras_sr_bt_model);

/* Sends UMA logs.
 *
 * Args:
 *    iodev - The iodev that tries to enable sr_bt.
 *    status - The result of cras_sr_bt_can_be_enabled.
 *    is_enabled - A bool indicating whether sr_bt is enabled successfully or
 *      not.
 */
void cras_sr_bt_send_uma_log(struct cras_iodev* iodev,
                             const enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS status,
                             bool is_enabled);

#endif  // CRAS_SRC_SERVER_CRAS_SR_BT_UTIL_H_
