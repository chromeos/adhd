/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "cras_bt_manager.h"

static void fluoride_start(struct bt_stack *s)
{
}

static void fluoride_stop(struct bt_stack *s)
{
}

static struct bt_stack fluoride = {
	.conn = NULL,
	.start = fluoride_start,
	.stop = fluoride_stop,
};

void cras_fluoride_set_enable(bool enable)
{
	if (enable)
		cras_bt_switch_stack(&fluoride);
	else
		cras_bt_switch_default_stack();
}
