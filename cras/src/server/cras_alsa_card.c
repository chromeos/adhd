/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <syslog.h>

#include "cras_alsa_io.h"
#include "cras_alsa_mixer.h"
#include "cras_card_config.h"
#include "cras_config.h"
#include "cras_iodev_list.h"
#include "cras_types.h"
#include "utlist.h"

#define MAX_ALSA_CARDS 32 /* Alsa limit on number of cards. */
#define MAX_ALSA_PCM_NAME_LENGTH 6 /* Alsa names "hw:XX" + 1 for null. */
#define MAX_INI_NAME_LENGTH 63 /* 63 chars + 1 for null where declared. */

struct iodev_list_node {
	struct cras_iodev *iodev;
	struct iodev_list_node *prev, *next;
};

/* Holds information about each sound card on the system.
 * name - of the form hw:XX,YY.
 * card_index - 0 based index, value of "XX" in the name.
 * iodevs - Input and output devices for this card.
 * mixer - Controls the mixer controls for this card.
 * config - Config info for this card, can be NULL if none found.
 */
struct cras_alsa_card {
	char name[MAX_ALSA_PCM_NAME_LENGTH];
	size_t card_index;
	struct iodev_list_node *iodevs;
	struct cras_alsa_mixer *mixer;
	struct cras_card_config *config;
};

/* Creates an iodev for the given device.
 * Args:
 *    card_index - 0 based index, value of "XX" in "hw:XX,YY".
 *    card_name - The name of the card.
 *    device_index - 0 based index, value of "YY" in "hw:XX,YY".
 *    mixer - Controls the mixer controls for this card.
 *    auto_route - If true immediately switch to using this device.
 *    priority - Priority to give the device.
 *    direction - Input or output.
 */
static struct iodev_list_node *create_iodev_for_device(
		size_t card_index,
		const char *card_name,
		size_t device_index,
		struct cras_alsa_mixer *mixer,
		int auto_route,
		size_t priority,
		enum CRAS_STREAM_DIRECTION direction)
{
	struct iodev_list_node *new_dev;

	/* Dropping the priority of non-auto routed devices ensures
	 * that the auto-route devs are still selected first after the
	 * list is re-sorted.  Without this the order of devices
	 * within a card can't be determined when the list is
	 * resorted. */
	if (!auto_route && priority > 0)
		priority--;

	new_dev = calloc(1, sizeof(*new_dev));
	if (new_dev == NULL)
		return NULL;
	new_dev->iodev =
		alsa_iodev_create(card_index,
				  card_name,
				  device_index,
				  mixer,
				  auto_route,
				  priority,
				  direction);
	if (new_dev->iodev == NULL) {
		syslog(LOG_ERR, "Couldn't create alsa_iodev for %zu:%zu\n",
		       card_index, device_index);
		free(new_dev);
		return NULL;
	}

	return new_dev;
}

/*
 * Exported Interface.
 */

struct cras_alsa_card *cras_alsa_card_create(struct cras_alsa_card_info *info)
{
	snd_ctl_t *handle = NULL;
	int rc, dev_idx;
	snd_ctl_card_info_t *card_info;
	const char *card_name;
	snd_pcm_info_t *dev_info;
	struct cras_alsa_card *alsa_card;
	int first_playback = 1; /* True if it's the first playback dev. */
	int first_capture = 1; /* True if it's the first capture dev. */

	if (info->card_index >= MAX_ALSA_CARDS) {
		syslog(LOG_ERR,
		       "Invalid alsa card index %u",
		       info->card_index);
		return NULL;
	}

	snd_ctl_card_info_alloca(&card_info);
	snd_pcm_info_alloca(&dev_info);

	alsa_card = calloc(1, sizeof(*alsa_card));
	if (alsa_card == NULL)
		return NULL;
	alsa_card->card_index = info->card_index;

	snprintf(alsa_card->name,
		 MAX_ALSA_PCM_NAME_LENGTH,
		 "hw:%u",
		 info->card_index);

	rc = snd_ctl_open(&handle, alsa_card->name, 0);
	if (rc < 0) {
		syslog(LOG_ERR, "Fail opening control %s.", alsa_card->name);
		goto error_bail;
	}

	rc = snd_ctl_card_info(handle, card_info);
	if (rc < 0) {
		syslog(LOG_ERR, "Error getting card info.");
		goto error_bail;
	}

	card_name = snd_ctl_card_info_get_name(card_info);
	if (card_name == NULL) {
		syslog(LOG_ERR, "Error getting card name.");
		goto error_bail;
	}

	/* Read config file for this card if it exists. */
	alsa_card->config = cras_card_config_create(CRAS_CONFIG_FILE_DIR,
						    card_name);
	if (alsa_card->config == NULL)
		syslog(LOG_DEBUG, "No config file for %s", alsa_card->name);

	/* Create one mixer per card. */
	alsa_card->mixer = cras_alsa_mixer_create(alsa_card->name,
						  alsa_card->config);
	if (alsa_card->mixer == NULL) {
		syslog(LOG_ERR, "Fail opening mixer for %s.", alsa_card->name);
		goto error_bail;
	}

	dev_idx = -1;
	while (1) {
		snd_ctl_pcm_next_device(handle, &dev_idx);
		if (dev_idx < 0)
			break;

		snd_pcm_info_set_device(dev_info, dev_idx);
		snd_pcm_info_set_subdevice(dev_info, 0);

		/* Check for playback devices. */
		snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_PLAYBACK);
		rc = snd_ctl_pcm_info(handle, dev_info);
		if (rc == 0) {
			struct iodev_list_node *new_dev;

			new_dev = create_iodev_for_device(
					info->card_index,
					card_name,
					dev_idx,
					alsa_card->mixer,
					first_playback, /*auto-route*/
					info->priority,
					CRAS_STREAM_OUTPUT);
			if (new_dev != NULL) {
				syslog(LOG_DEBUG, "New playback device %u:%d",
				       info->card_index, dev_idx);
				DL_APPEND(alsa_card->iodevs, new_dev);
				first_playback = 0;
			}
		}

		/* Check for capture devices. */
		snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_CAPTURE);
		rc = snd_ctl_pcm_info(handle, dev_info);
		if (rc == 0) {
			struct iodev_list_node *new_dev;

			new_dev = create_iodev_for_device(
					info->card_index,
					card_name,
					dev_idx,
					alsa_card->mixer,
					first_capture,
					info->priority,
					CRAS_STREAM_INPUT);
			if (new_dev != NULL) {
				syslog(LOG_DEBUG, "New capture device %u:%d",
				       info->card_index, dev_idx);
				DL_APPEND(alsa_card->iodevs, new_dev);
				first_capture = 0;
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
	if (alsa_card->config)
		cras_card_config_destroy(alsa_card->config);
	free(alsa_card);
	return NULL;
}

void cras_alsa_card_destroy(struct cras_alsa_card *alsa_card)
{
	struct iodev_list_node *curr, *tmp;

	if (alsa_card == NULL)
		return;

	DL_FOREACH_SAFE(alsa_card->iodevs, curr, tmp) {
		alsa_iodev_destroy(curr->iodev);
		DL_DELETE(alsa_card->iodevs, curr);
		free(curr);
	}
	cras_alsa_mixer_destroy(alsa_card->mixer);
	if (alsa_card->config)
		cras_card_config_destroy(alsa_card->config);
	free(alsa_card);
}

size_t cras_alsa_card_get_index(const struct cras_alsa_card *alsa_card)
{
	assert(alsa_card);
	return alsa_card->card_index;
}
