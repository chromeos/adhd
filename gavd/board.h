/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_BOARD_GENERIC_H_)
#define _BOARD_GENERIC_H_
#include <stddef.h>

#define ADHD_TARGET_MACHINE BOARD

/* When ADHD_SET_FACTORY_DEFAULT is defined, 'alsactl restore' will be
 * used when to set all the Alsa controls of the internal devices to
 * their 'factory default values.
 *
 * The setting will occur when the deamon is loaded, exited (for
 * accessibility on the login screen), and when SIGHUP is received.
 *
 * This cannot be enabled until defect 'chromium:97144' is addressed.
 */
#undef ADHD_SET_FACTORY_DEFAULT
#define adhd_set_factory_default 0

#endif
