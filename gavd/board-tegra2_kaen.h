/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_BOARD_TEGRA2_KAEN_H_)
#define _BOARD_TEGRA2_KAEN_H_

/* To determine the value of 'ADHD_INPUT_NAME_HEADPHONE_JACK', and
 * 'ADHD_INPUT_NAME_MICROPHONE_JACK', run 'evtest' on the test machine
 * (you may need to use 'gmerge evtest').
 *
 * The value is the text which matches the desired input device.  In
 * the case of 'tegra2_kaen', the names are derived from the following
 * lines produced by 'evtest':
 *
 *       /dev/input/event3:      tegra-seaboard Headphone Jack
 *       /dev/input/event2:      tegra-seaboard Mic Jack
 */
#define ADHD_GPIO_HEADPHONE
#define ADHD_GPIO_HEADPHONE_GPIO_NUMBER 185
#define ADHD_INPUT_NAME_HEADPHONE_JACK  ADHD_WM803_INPUT_NAME_HEADPHONE_JACK

#define ADHD_GPIO_MICROPHONE
#define ADHD_GPIO_MICROPHONE_GPIO_NUMBER 185
#define ADHD_INPUT_NAME_MICROPHONE_JACK ADHD_WM803_INPUT_NAME_MICROPHONE_JACK

#define ADHD_INITIALIZE_SOUND_COMMAND      ADHD_WM8903_INIT_COMMAND
#define ADHD_GPIO_HEADPHONE_INSERT_COMMAND ADHD_WM8903_HEADPHONE_INSERT
#define ADHD_GPIO_HEADPHONE_REMOVE_COMMAND ADHD_WM8903_HEADPHONE_REMOVE
#define ADHD_GPIO_MIRCOPHONE_INSERT_COMMAND ADHD_WM8903_MICROPHONE_INSERT
#define ADHD_GPIO_MIRCOPHONE_REMOVE_COMMAND ADHD_WM8903_MICROPHONE_REMOVE
#endif
