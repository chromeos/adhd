/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_UDEV_H_
#define CRAS_UDEV_H_

//#define __USE_UNIX98		/* For pthread_mutexattr_settype et al. */
#include <pthread.h>

void cras_udev_start_sound_subsystem_monitor(void);
void cras_udev_stop_sound_subsystem_monitor(void);

#endif /* CRAS_UDEV_H_ */
