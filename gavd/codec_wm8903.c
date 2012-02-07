/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "codec.h"
#include "linkerset.h"

static char const *initialize[] = {
    "set 'Speaker'   100%",
    "set 'Headphone' 100%",
    "set 'Digital'   100%",
    "set 'Left Speaker Mixer DACL'  on",
    "set 'Right Speaker Mixer DACR' on",
    "set 'ADC Input' 'DMIC'",
    "set 'ADC Companding' off",
    "set 'ADC Companding Mode' ulaw",
    "set 'ADC OSR' 'High performance'",
    "set 'Left Capture Mux' 'Right'",
    "set 'Right Capture Mux' 'Right'",
    "set 'Right Input Inverting Mux' 'IN2R'",
    "set 'Right Input Mode Mux' 'Differential Mic'",
    "set 'Right Input Mux' 'IN1R'", /* or IN3R */
    "set 'Right Input PGA' '3'",    /* or more */
    "set 'Right Input PGA' on",
    NULL
};

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

static char const *microphone_insert[] = {
    "set 'ADC Input' 'ADC'",
    NULL
};

static char const *microphone_remove[] = {
    "set 'ADC Input' 'DMIC'",
    NULL
};

#define DECLARE_BOARD(_board)                                   \
    static codec_desc_t _board##_codec_desc = {                 \
        .codec             = "wm8903",                          \
        .board             = #_board,                           \
        .initialize        = initialize,                        \
        .headphone_insert  = headphone_insert,                  \
        .headphone_remove  = headphone_remove,                  \
        .microphone_insert = microphone_insert,                 \
        .microphone_remove = microphone_remove                  \
    };                                                          \
    LINKERSET_ADD_ITEM(codec_desc, _board##_codec_desc);

DECLARE_BOARD(tegra2_aebl)
DECLARE_BOARD(tegra2_asymptote)
DECLARE_BOARD(tegra2_kaen)
DECLARE_BOARD(tegra2_seaboard)
