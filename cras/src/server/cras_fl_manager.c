/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "cras_bt_manager.h"

static void floss_start(struct bt_stack *s)
{
}

static void floss_stop(struct bt_stack *s)
{
}

static struct bt_stack floss = {
	.conn = NULL,
	.start = floss_start,
	.stop = floss_stop,
};

void cras_floss_set_enabled(bool enable)
{
	if (enable)
		cras_bt_switch_stack(&floss);
	else
		cras_bt_switch_default_stack();
}
