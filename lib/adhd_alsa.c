/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>

#include "adhd_alsa.h"
#include "verbose.h"

#if 0
void adhd_alsa_dump_card(unsigned card)
{
    card = card;
}

void adhd_alsa_dump_all_cards(void)
{

}
#endif

static unsigned adhd_alsa_get_cards_work(adhd_alsa_card_t *cards)
{
    int      card_number = -1;
    unsigned result      = 0;

    while (snd_card_next(&card_number) >= 0 && card_number != -1) {
        if (cards != NULL) {
            cards[result].number = (unsigned)card_number;
        }
        ++result;
    }
    return result;
}

static unsigned adhd_alsa_get_card_count(void)
{
    return adhd_alsa_get_cards_work(NULL);
}

static void adhd_alsa_get_cards(adhd_alsa_card_t *cards)
{
    adhd_alsa_get_cards_work(cards);
}

void adhd_alsa_card_to_hw_name(unsigned  card,
                               char     *hw_name,
                               size_t    hw_name_len)
{
    /* ALSA supports no more than 32 cards.  See snd_card_next().
     *
     * We know that no more than two bytes will be needed card number
     * in the output string.
     */
    assert(card < 32 &&         /* See snd_card_next() for '32'. */
           hw_name_len >= strlen("hw:31") + 1);
    snprintf(hw_name, hw_name_len, "hw:%u", card);
    hw_name[hw_name_len - 1] = '\0';
}

static void adhd_alsa_gather_cards(adhd_alsa_info_t *info)
{
    unsigned i;

    info->n_cards = adhd_alsa_get_card_count();
    if (info->n_cards > 0) {
        info->cards = malloc(sizeof(adhd_alsa_info_t) * info->n_cards);
        assert(info->cards != NULL);
        adhd_alsa_get_cards(info->cards);
    }
    for (i = 0; i < info->n_cards; ++i) {
        adhd_alsa_card_t *c = &info->cards[i];
        snd_card_get_name((int)c->number, &c->name);
        snd_card_get_longname((int)c->number, &c->long_name);
    }
}

static void adhd_alsa_get_card_info(adhd_alsa_card_t *c)
{
    char       hw_name[16];
    int        result;
    snd_ctl_t *ctl;

    adhd_alsa_card_to_hw_name(c->number, hw_name, ARRAYLEN(hw_name));
    result = snd_ctl_open(&ctl, hw_name, SND_CTL_NONBLOCK);
    assert(result == 0);
    snd_ctl_close(ctl);
}

void adhd_alsa_get_all_card_info(adhd_alsa_info_t *info)
{
    unsigned i;
    adhd_alsa_gather_cards(info);

    for (i = 0; i < info->n_cards; ++i) {
        adhd_alsa_card_t *c = &info->cards[i];
        adhd_alsa_get_card_info(c);
    }
}


void adhd_alsa_release_card_info(adhd_alsa_info_t *info)
{
    if (1) {
        unsigned i;
        for (i = 0; i < info->n_cards; ++i) {
            adhd_alsa_card_t *c = &info->cards[i];
            fprintf(stderr, "%u %u '%s' '%s'\n", i, c->number, c->name, c->long_name);
            free(c->name);
            free(c->long_name);
        }
        fprintf(stderr, "done\n");
        //        free(info->cards);
    }
}
