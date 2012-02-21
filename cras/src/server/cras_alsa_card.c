/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <syslog.h>

#include "cras_alsa_io.h"
#include "cras_alsa_mixer.h"
#include "cras_types.h"
#include "utlist.h"

#define MAX_ALSA_CARDS 32 /* Alsa limit on number of cards. */
#define MAX_ALSA_PCM_NAME_LENGTH 6 /* Alsa names "hw:XX" + 1 for null. */
#define MAX_ALSA_DEV_NAME_LENGTH 9 /* Alsa names "hw:XX,YY" + 1 for null. */

struct iodev_list_node {
	struct cras_iodev *iodev;
	struct iodev_list_node *prev, *next;
};

struct cras_alsa_card {
	char name[MAX_ALSA_PCM_NAME_LENGTH];
	size_t card_index;
	struct iodev_list_node *iodevs;
	struct cras_alsa_mixer *mixer;
};

/* Creates an iodev for the given device. */
static struct iodev_list_node *create_iodev_for_device(
		const char *dev_name,
		struct cras_alsa_mixer *mixer,
		enum CRAS_STREAM_DIRECTION direction)
{
	struct iodev_list_node *new_dev;

	new_dev = calloc(1, sizeof(*calloc));
	if (new_dev == NULL)
		return NULL;
	new_dev->iodev = alsa_iodev_create(dev_name, mixer, direction);
	if (new_dev->iodev == NULL) {
		free(new_dev);
		return NULL;
	}

	return new_dev;
}

/*
 * Exported Interface.
 */

struct cras_alsa_card *cras_alsa_card_create(size_t card_idx)
{
	snd_ctl_t *handle = NULL;
	int rc, dev_idx;
	snd_pcm_info_t *dev_info;
	char dev_name[MAX_ALSA_DEV_NAME_LENGTH + 1];
	struct cras_alsa_card *alsa_card;

	if (card_idx >= MAX_ALSA_CARDS) {
		syslog(LOG_ERR, "Invalid alsa card index %zu", card_idx);
		return NULL;
	}

	snd_pcm_info_alloca(&dev_info);

	alsa_card = calloc(1, sizeof(*alsa_card));
	if (alsa_card == NULL)
		return NULL;

	snprintf(alsa_card->name,
		 MAX_ALSA_PCM_NAME_LENGTH,
		 "hw:%zu",
		 card_idx);

	/* Create one mixer per card. */
	alsa_card->mixer = cras_alsa_mixer_create(alsa_card->name);
	if (alsa_card->mixer == NULL) {
		syslog(LOG_ERR, "Fail opening mixer for %s.", alsa_card->name);
		goto error_bail;
	}

	rc = snd_ctl_open(&handle, alsa_card->name, 0);
	if (rc < 0) {
		syslog(LOG_ERR, "Fail opening control %s.", alsa_card->name);
		goto error_bail;
	}

	dev_idx = -1;
	while (1) {
		snd_ctl_pcm_next_device(handle, &dev_idx);
		if (dev_idx < 0)
			break;
		snprintf(dev_name,
			 MAX_ALSA_DEV_NAME_LENGTH,
			 "hw:%zu,%d",
			 card_idx,
			 dev_idx);

		snd_pcm_info_set_device(dev_info, dev_idx);
		snd_pcm_info_set_subdevice(dev_info, 0);

		/* Check for playback devices. */
		snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_PLAYBACK);
		rc = snd_ctl_pcm_info(handle, dev_info);
		if (rc == 0) {
			struct iodev_list_node *new_dev;

			new_dev = create_iodev_for_device(dev_name,
							  alsa_card->mixer,
							  CRAS_STREAM_OUTPUT);
			if (new_dev != NULL) {
				syslog(LOG_DEBUG, "New playback device %zu:%d",
						card_idx, dev_idx);
				DL_APPEND(alsa_card->iodevs, new_dev);
			}
		}

		/* Check for capture devices. */
		snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_CAPTURE);
		rc = snd_ctl_pcm_info(handle, dev_info);
		if (rc == 0) {
			struct iodev_list_node *new_dev;

			new_dev = create_iodev_for_device(dev_name,
							  alsa_card->mixer,
							  CRAS_STREAM_INPUT);
			if (new_dev != NULL) {
				syslog(LOG_DEBUG, "New capture device %zu:%d",
						card_idx, dev_idx);
				DL_APPEND(alsa_card->iodevs, new_dev);
			}
		}
	}

	snd_ctl_close(handle);
	return alsa_card;

error_bail:
	if (handle != NULL)
		snd_ctl_close(handle);
	if (alsa_card->mixer)
		cras_alsa_mixer_destroy(alsa_card->mixer);
	free(alsa_card);
	return NULL;
}

void cras_alsa_card_destroy(struct cras_alsa_card *alsa_card)
{
	struct iodev_list_node *curr, *tmp;

	if (alsa_card == NULL)
		return;

	cras_alsa_mixer_destroy(alsa_card->mixer);

	DL_FOREACH_SAFE(alsa_card->iodevs, curr, tmp) {
		alsa_iodev_destroy(curr->iodev);
		DL_DELETE(alsa_card->iodevs, curr);
		free(curr);
	}
	free(alsa_card);
}
