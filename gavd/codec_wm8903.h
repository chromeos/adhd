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

#define ADHD_WM803_INPUT_NAME_HEADPHONE_JACK  "tegra-wm8903 Headphone Jack"
#define ADHD_WM803_INPUT_NAME_MICROPHONE_JACK "tegra-wm8903 Mic Jack"
#endif
