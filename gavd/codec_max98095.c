/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "codec.h"
#include "linkerset.h"

static char const *headphone_insert[] = {
    "set 'Speaker' off",
    "set 'Int Spk' off",
    "set 'Headphone' on",
    NULL
};

static char const *headphone_remove[] = {
    "set 'Speaker' on",
    "set 'Int Spk' on",
    "set 'Headphone' off",
    NULL
};

#define DECLARE_BOARD(_board)                                   \
    static codec_desc_t _board##_codec_desc = {                 \
        .codec             = "max98095",                        \
        .board             = #_board,                           \
        .initialize        = NULL,                              \
        .headphone_insert  = headphone_insert,                  \
        .headphone_remove  = headphone_remove,                  \
        .microphone_insert = NULL,                              \
        .microphone_remove = NULL                               \
    };                                                          \
    LINKERSET_ADD_ITEM(codec_desc, _board##_codec_desc);

DECLARE_BOARD(waluigi)
