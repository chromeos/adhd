/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_FL_MANAGER_H_
#define CRAS_FL_MANAGER_H_

#include <stdbool.h>

void cras_floss_set_enabled(bool enable);

int cras_floss_get_hfp_enabled();

int cras_floss_get_a2dp_enabled();

#endif /* CRAS_FL_MANAGER_H_ */
