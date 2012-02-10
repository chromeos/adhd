/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Handles various system-level settings.
 *
 * Volume:  The system volume is represented as a value from 0 to 100.  This
 * number will be interpreted by the output device and applied to the hardware.
 * The value will be mapped to dB by the active device as it will know its curve
 * the best.
 */

#ifndef CRAS_SYSTEM_SETTINGS_H_
#define CRAS_SYSTEM_SETTINGS_H_

#include <stddef.h>

#define CRAS_MAX_SYSTEM_VOLUME 100

/* Callback function to be notified when the system volume changes. */
typedef void (*cras_system_volume_changed_cb)(size_t volume);

/* Initialize system settings. */
void cras_system_settings_init();

/* Sets the system volume.  Will be applied by the active device. */
void cras_system_set_volume(size_t volume);
/* Gets the current system volume. */
size_t cras_system_get_volume();
/* Set the callback to call when the volume changes. */
void cras_system_register_volume_changed_cb(cras_system_volume_changed_cb cb);

#endif /* CRAS_SYSTEM_SETTINGS_H_ */
