/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_CODEC_CIRRUS_COUGARPOINT_H_)
#define _CODEC_CIRRUS_COUGARPOINT_H_
#include "adhd_alsa_defs.h"

/* TODO(thutt):
 *
 *    ADHD_CIRRUS_COUGARPOINT_INIT_COMMAND should be removed once
 *    /etc/asound.state is loaded.
 *
 *    When everything is removed, this file is a good candidate itself
 *    for removal.
 *
 *    The strings in this file are executed using 'system()'.
 */

#define ADHD_CIRRUS_COUGARPOINT_INIT_COMMAND                    \
    ADHD_AMIXER_COMMAND " set 'Master'         80%"      " && " \
    ADHD_AMIXER_COMMAND " set 'Speaker Boost'  80%"      " && " \
    ADHD_AMIXER_COMMAND " set 'PCM'            90%"      " && " \
    ADHD_AMIXER_COMMAND " set 'HP/Speakers'    90%"      " && " \
    ADHD_AMIXER_COMMAND " set 'Master'         on"       " && " \
    ADHD_AMIXER_COMMAND " set 'HP/Speakers'    on"
#endif
