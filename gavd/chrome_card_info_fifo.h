/* Copyright (c) 2011, 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_CHROME_CARD_INFO_FIFO_H_)
#define _CHROME_CARD_INFO_FIFO_H_

#include "fifo.h"

FIFO_DECLARE(chrome_card_info_fifo);

void chrome_card_added(const char *udev_sysname,
                       unsigned    card_number,
                       unsigned    device_number);
void chrome_card_removed(const char *udev_sysname,
                         unsigned    card_number,
                         unsigned    device_number);
void chrome_card_changed(const char *udev_sysname,
                         unsigned    card_number,
                         unsigned    device_number,
                         unsigned    active,
                         unsigned    internal,
                         unsigned    primary);
#endif
