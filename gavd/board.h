/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_BOARD_GENERIC_H_)
#define _BOARD_GENERIC_H_
#include <stddef.h>
#include "codec_wm8903.h"
#include "codec_max98095.h"

#define ADHD_TARGET_MACHINE BOARD
#include ADHD_BOARD_INCLUDE

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

/* TODO(thutt):
 *
 *   When /etc/asound.rc is loaded at login, the command for
 *   initializing the sound system should be entirely removed from all
 *   board files.
 */
#if defined(ADHD_INITIALIZE_SOUND_COMMAND)
#define adhd_initialize_sound_command 1
#else
#define adhd_initialize_sound_command 0
#define ADHD_INITIALIZE_SOUND_COMMAND NULL /* For compilation only. */
#endif

/* gavd manages the multiplexing between the internal speakers or
 * headphone jack, depending on the state of the jack switch.  The
 * 'ADHD_GPIO_HEADPHONE_INSERT_COMMAND' identifier contains the shell
 * command to execute when the headphones are inserted, or NULL.
 */
#if defined(ADHD_GPIO_HEADPHONE_INSERT_COMMAND)
#define adhd_headphone_insert_command 1
#else
#define adhd_headphone_insert_command 0
#define ADHD_GPIO_HEADPHONE_INSERT_COMMAND NULL /* For compilation only. */
#endif

/* gavd manages the multiplexing between the internal speakers or
 * headphone jack, depending on the state of the jack switch.  The
 * 'ADHD_GPIO_HEADPHONE_REMOVE_COMMAND' identifier contains the shell
 * command to execute when the headphones are removed, or NULL.
 */
#if defined(ADHD_GPIO_HEADPHONE_REMOVE_COMMAND)
#define adhd_headphone_remove_command 1
#else
#define adhd_headphone_remove_command 0
#define ADHD_GPIO_HEADPHONE_REMOVE_COMMAND NULL
#endif

#endif
