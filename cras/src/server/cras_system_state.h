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

#ifndef CRAS_SYSTEM_STATE_H_
#define CRAS_SYSTEM_STATE_H_

#include <stddef.h>

#define CRAS_MAX_SYSTEM_VOLUME 100

/* Callback functions to be notified when settings change. data is a user
 * provided argument that will be passed back. */
typedef void (*cras_system_volume_changed_cb)(void *data);

/* Initialize system settings. */
void cras_system_state_init();

/* Sets the system volume.  Will be applied by the active device. */
void cras_system_set_volume(size_t volume);
/* Gets the current system volume. */
size_t cras_system_get_volume();

/* Adds a callback to call when the volume changes.
 * Args:
 *    cb - Function to call when volume changes.
 *    arg - Value to pass back to callback.
 */
int cras_system_register_volume_changed_cb(cras_system_volume_changed_cb cb,
					   void *arg);

/* Removes a callback to call when the volume changes.  Only removes the entry
 * if both cb and arg match the values passed to the register function.
 * Args:
 *    cb - Function to call when volume changes.
 *    arg - Value to passed back to callback.
 */
int cras_system_remove_volume_changed_cb(cras_system_volume_changed_cb cb,
					 void *arg);

/* Sets the system capture volume.  Will be applied by the active device. */
void cras_system_set_capture_gain(long gain);
/* Gets the current system capture volume. */
long cras_system_get_capture_gain();

/* Adds a callback to call when the capture volume changes.
 * Args:
 *    cb - Function to call when capture volume changes.
 *    arg - Value to pass back to callback.
 */
int cras_system_register_capture_gain_changed_cb(
		cras_system_volume_changed_cb cb, void *arg);

/* Removes a callback to call when the capture volume changes.  Only removes the
 * entry if both cb and arg match the values passed to the register function.
 * Args:
 *    cb - Function to call when capture volume changes.
 *    arg - Value to passed back to callback.
 */
int cras_system_remove_capture_gain_changed_cb(cras_system_volume_changed_cb cb,
					       void *arg);

/* Sets if the system is muted or not. */
void cras_system_set_mute(int muted);
/* Gets the current mute state of the system. */
int cras_system_get_mute();

/* Adds a callback to call when the mute state changes.
 * Args:
 *    cb - Function to call when mute state changes.
 *    arg - Value to pass back to callback.
 */
int cras_system_register_mute_changed_cb(cras_system_volume_changed_cb cb,
					 void *arg);

/* Removes a callback to call when the mute state changes.  Only removes the
 * entry if both cb and arg match the values passed to the register function.
 * Args:
 *    cb - Function to call when volume changes.
 *    arg - Value to passed back to callback.
 */
int cras_system_remove_mute_changed_cb(cras_system_volume_changed_cb cb,
				       void *arg);

/* Sets if the system capture path is muted or not. */
void cras_system_set_capture_mute(int muted);
/* Gets the current mute state of the system capture path. */
int cras_system_get_capture_mute();

/* Adds a callback to call when the capture mute state changes.
 * Args:
 *    cb - Function to call when the capture mute state changes.
 *    arg - Value to pass back to callback.
 */
int cras_system_register_capture_mute_changed_cb(
		cras_system_volume_changed_cb cb, void *arg);

/* Removes a callback to call when the capture mute state changes.  Only removes
 * the entry if both cb and arg match the values passed to the register
 * function.
 * Args:
 *    cb - Function to call when volume changes.
 *    arg - Value to passed back to callback.
 */
int cras_system_remove_capture_mute_changed_cb(cras_system_volume_changed_cb cb,
					       void *arg);

/* Adds a card at the given index to the system.  When a new card is found
 * (through a udev event notification) this will add the card to the system,
 * causing its devices to become available for playback/capture.
 * Args:
 *    alsa_card_index - Index ALSA uses to refer to the card.  The X in "hw:X".
 * Returns:
 *    0 on success, negative error on failure (Can't create or card already
 *    exists).
 */
int cras_system_add_alsa_card(size_t alsa_card_index);

/* Removes a card.  When a device is removed this will do the cleanup.  Device
 * at index must have been added using cras_system_add_alsa_card().
 * Args:
 *    alsa_card_index - Index ALSA uses to refer to the card.  The X in "hw:X".
 * Returns:
 *    0 on success, negative error on failure (Can't destroy or card doesn't
 *    exist).
 */
int cras_system_remove_alsa_card(size_t alsa_card_index);

#endif /* CRAS_SYSTEM_STATE_H_ */
