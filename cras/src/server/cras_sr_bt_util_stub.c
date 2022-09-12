/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_sr.h"
#include "cras_sr_bt_util.h"

int cras_sr_bt_can_be_enabled()
{
	return 0;
}

struct cras_sr_model_spec cras_sr_bt_get_model_spec(enum cras_sr_bt_model model)
{
	return (struct cras_sr_model_spec){};
}
