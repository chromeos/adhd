/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file contains definitions specific to Alsa.
 */
#if !defined(_ADHD_ALSA_DEFS_H)
#define _ADHD_ALSA_DEFS_H

/* These macros are used with C string concatentation.  Always ensure
 * that string literal containing the command line parameters has a
 * space at the beginning.
 */
#define ADHD_AMIXER_COMMAND                "/usr/bin/amixer"
#define ADHD_ALSACTL_COMMAND               "/usr/sbin/alsactl"

#endif
