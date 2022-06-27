/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SR_BT_UTIL_H_
#define CRAS_SR_BT_UTIL_H_

#include "cras_sr.h"

/* Checks if cras_sr_bt can be enabled. It will check the dependencies is
 * fulfilled, i.e. featured flag is turned on and the dlc is ready.
 *
 * Returns:
 *    1 if True. Otherwise, 0.
 */
int cras_sr_bt_can_be_enabled();

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

#endif /* CRAS_SR_BT_UTIL_H_ */
