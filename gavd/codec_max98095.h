/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_CODEC_MAX98095_H_)
#define _CODEC_MAX98095_H_
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
 *    ADHD_MAX98095_INIT_COMMAND should be removed once
 *    /etc/asound.state is loaded.
 *
 *    ADHD_MAX98095_HEADPHONE_INSERT and ADHD_MAX98095_HEADPHONE_REMOVE
 *    should be removed once Chrome is receiving and processing
 *    headphone insert & remove messages.
 *
 *    When everything is removed, this file is a good candidate itself
 *    for removal.
 *
 *    The strings in this file are executed using 'system()'.
 */

#define ADHD_MAX98095_INIT_COMMAND ""

#define ADHD_MAX98095_HEADPHONE_INSERT                    \
    ADHD_AMIXER_COMMAND " set 'Speaker' off"  " && "    \
    ADHD_AMIXER_COMMAND " set 'Int Spk' off"  " && "    \
    ADHD_AMIXER_COMMAND " set 'Headphone' on"

#define ADHD_MAX98095_HEADPHONE_REMOVE                    \
    ADHD_AMIXER_COMMAND " set 'Speaker' on"   " && "    \
    ADHD_AMIXER_COMMAND " set 'Int Spk' on"   " && "    \
    ADHD_AMIXER_COMMAND " set 'Headphone' off"

#define ADHD_MAX98095_MICROPHONE_INSERT ""
#define ADHD_MAX98095_MICROPHONE_REMOVE ""
#endif
