/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_CRAS_CONNECTION_H_)
#define _CRAS_CONNECTION_H_

void notify_cras(unsigned action,
                 unsigned card_number,
                 unsigned device_number,
                 unsigned active,
                 unsigned internal,
                 unsigned primary);
#endif
