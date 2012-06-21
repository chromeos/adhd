/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRAS_GPIO_JACK_H
#define _CRAS_GPIO_JACK_H

#include "cras_types.h"

int gpio_switch_open(const char *pathname);
int gpio_switch_read(int fd, void *buf, size_t n_bytes);

int gpio_switch_eviocgbit(int fd, unsigned long sw, void *buf);
int gpio_switch_eviocgsw(int fd, void *bits, size_t n_bytes);

unsigned gpio_get_switch_names(enum CRAS_STREAM_DIRECTION direction,
                               char **names, size_t n_names);
char *sys_input_get_device_name(const char *path);
#endif
