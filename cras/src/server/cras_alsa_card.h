/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRAS_ALSA_CARD_H
#define _CRAS_ALSA_CARD_H

#include "cras_types.h"

/* cras_alsa_card represents an alsa sound card.  It adds all the devices for
 * this card to the system when it is created, and removes them when it is
 * destroyed.  It will create an alsa_mixer object that can control the volume
 * and mute settings for the card.
 */

struct cras_alsa_card;

/* Creates a cras_alsa_card instance for the given alsa device.  Enumerates the
 * devices for the card and adds them to the system as possible playback or
 * capture endpoints.
 * Args:
 *    card_info - Contains the card index, type, and priority.
 * Returns:
 *    A pointer to the newly created cras_alsa_card which must later be freed
 *    by calling cras_alsa_card_destroy or NULL on error.
 */
struct cras_alsa_card *cras_alsa_card_create(struct cras_alsa_card_info *info);

/* Destroys a cras_alsa_card that was returned from cras_alsa_card_create.
 * Args:
 *    alsa_card - The cras_alsa_card pointer returned from
 *        cras_alsa_card_create.
 */
void cras_alsa_card_destroy(struct cras_alsa_card *alsa_card);

/* Returns the alsa card index for the given card.
 * Args:
 *    alsa_card - The cras_alsa_card pointer returned from
 *        cras_alsa_card_create.
 */
size_t cras_alsa_card_get_index(const struct cras_alsa_card *alsa_card);

#endif /* _CRAS_ALSA_CARD_H */
