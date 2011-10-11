/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_CODEC_WM8903_H_)
#define _CODEC_WM8903_H_
#include "adhd_alsa_defs.h"

/* TODO(thutt):
 *
 *    The command strings in this file are intended to be temporarily
 *    used to facilitate the removal of 'headphone-jack-monitor'
 *    without all the necessary infrastructure for Chrome to deal with
 *    headphone insertions and to work around the issue that
 *    '/etc/asound.rc' is not yet loaded before Chrome loads user
 *    settings.
 *
 *    ADHD_WM8903_INIT_COMMAND should be removed once
 *    /etc/asound.state is loaded.
 *
 *    ADHD_WM8903_HEADPHONE_INSERT and ADHD_WM8903_HEADPHONE_REMOVE
 *    should be removed once Chrome is receiving and processing
 *    headphone insert & remove messages.
 *
 *    When everything is removed, this file is a good candidate itself
 *    for removal.
 *
 *    The strings in this file are executed using 'system()'.
 */

#define ADHD_WM8903_INIT_COMMAND                                        \
    ADHD_AMIXER_COMMAND " set 'Speaker'   100%"              " && "     \
    ADHD_AMIXER_COMMAND " set 'Headphone' 100%"              " && "     \
    ADHD_AMIXER_COMMAND " set 'Digital'   100%"              " && "     \
    ADHD_AMIXER_COMMAND " set 'Left Speaker Mixer DACL'  on" " && "     \
    ADHD_AMIXER_COMMAND " set 'Right Speaker Mixer DACL' on" " && "     \
    ADHD_AMIXER_COMMAND " set 'Left Speaker Mixer DACR'  on" " && "     \
    ADHD_AMIXER_COMMAND " set 'Right Speaker Mixer DACR' on" " && "     \
    ADHD_AMIXER_COMMAND " set 'ADC Input' 'DMIC'"

#define ADHD_WM8903_HEADPHONE_INSERT                    \
    ADHD_AMIXER_COMMAND " set 'Speaker' off"  " && "    \
    ADHD_AMIXER_COMMAND " set 'Int Spk' off"  " && "    \
    ADHD_AMIXER_COMMAND " set 'Headphone' on"

#define ADHD_WM8903_HEADPHONE_REMOVE                    \
    ADHD_AMIXER_COMMAND " set 'Speaker' on"   " && "    \
    ADHD_AMIXER_COMMAND " set 'Int Spk' on"   " && "    \
    ADHD_AMIXER_COMMAND " set 'Headphone' off"

/* Between Linux Kernel 2.6.38 and 3.0, the names of the WM8903 input
 * devices changed.  Further, using kernel 3.0, the sound system on
 * Kaen does not function properly.  To facilitate faster switching to
 * the new values, and to facilitate better testing with 3.0, these
 * preprocessor symbols define the input devices.
 *
 * When we make the switch to kernel 3.0, the definitions should be
 * changed accordingly.
 *
 * This information was gathered with 'evtest' on the target devices.
 */
#if !defined(SWITCH_TO_KERNEL_3_0_COMPLETE)
#define ADHD_WM803_INPUT_NAME_HEADPHONE_JACK  "tegra-seaboard Headphone Jack"
#define ADHD_WM803_INPUT_NAME_MICROPHONE_JACK "tegra-seaboard Mic Jack"
#else
#define ADHD_WM803_INPUT_NAME_HEADPHONE_JACK  "tegra-wm8903 Headphone Jack"
#define ADHD_WM803_INPUT_NAME_MICROPHONE_JACK "tegra-wm8903 Mic Jack"
#endif

#endif
