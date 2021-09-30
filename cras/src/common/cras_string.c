/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE

#include <string.h>

#include "cras_string.h"

const char *cras_strerror(int errnum)
{
	static __thread char buf[1024];
	return strerror_r(errnum, buf, sizeof(buf));
}
