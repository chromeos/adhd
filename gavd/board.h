/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_BOARD_GENERIC_H_)
#define _BOARD_GENERIC_H_

#define ADHD_TARGET_MACHINE BOARD
#include ADHD_BOARD_INCLUDE

#if !defined(ADHD_GPIO_HEADPHONE)
#define adhd_gpio_headphone 0
#undef ADHD_GPIO_HEADPHONE_GPIO_NUMBER
#else
#define adhd_gpio_headphone 1
#if !defined(ADHD_GPIO_HEADPHONE_GPIO_NUMBER)
#error "ADHD_GPIO_HEADPHONE_GPIO_NUMBER must be defined."
#endif
#endif
#endif
