/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_GAVD_ALSA_H_)
#define _GAVD_ALSA_H_
#include <stdlib.h>
#include <alsa/asoundlib.h>

#define ARRAYLEN(_a) (sizeof(_a) / sizeof(_a[0]))

typedef struct adhd_alsa_card_t {
    unsigned  number;
    char     *name;
    char     *long_name;
} adhd_alsa_card_t;

typedef struct adhd_alsa_info_t {
    unsigned          n_cards;
    adhd_alsa_card_t *cards;    /* [0 .. n_cards) */
} adhd_alsa_info_t;

//void adhd_alsa_dump_card(unsigned card);
//void adhd_alsa_dump_all_cards(void);

void adhd_alsa_card_to_hw_name(unsigned  card,
                              char     *hw_name,
                              size_t    hw_name_len);
void adhd_alsa_get_all_card_info(adhd_alsa_info_t *info);
void adhd_alsa_release_card_info(adhd_alsa_info_t *info);
#endif
