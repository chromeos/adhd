/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "codec.h"
#include "linkerset.h"

#define DECLARE_BOARD(_board)                                   \
    static codec_desc_t _board##_codec_desc = {                 \
        .codec             = "cirrus-cougarpoint",              \
        .board             = #_board,                           \
        .headphone_insert  = NULL,                              \
        .headphone_remove  = NULL,                              \
        .microphone_insert = NULL,                              \
        .microphone_remove = NULL                               \
    };                                                          \
    LINKERSET_ADD_ITEM(codec_desc, _board##_codec_desc);

DECLARE_BOARD(lumpy)
DECLARE_BOARD(stumpy)
DECLARE_BOARD(stumpy64)
