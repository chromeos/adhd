/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <iniparser.h>
#include <syslog.h>

#include "cras_alsa_io.h"
#include "cras_alsa_mixer.h"
#include "cras_types.h"
#include "utlist.h"

#define MAX_ALSA_CARDS 32 /* Alsa limit on number of cards. */
#define MAX_ALSA_PCM_NAME_LENGTH 6 /* Alsa names "hw:XX" + 1 for null. */
#define CARD_CONFIG_FILE_DIR "/etc/cras"
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
 * ini - Dictionary from libiniparser containing the config file info.
 */
struct cras_alsa_card {
	char name[MAX_ALSA_PCM_NAME_LENGTH];
	size_t card_index;
	struct iodev_list_node *iodevs;
	struct cras_alsa_mixer *mixer;
	dictionary *ini;
};

/* Creates an iodev for the given device. */
static struct iodev_list_node *create_iodev_for_device(
		size_t card_index,
		size_t device_index,
		struct cras_alsa_mixer *mixer,
		enum CRAS_STREAM_DIRECTION direction)
{
	struct iodev_list_node *new_dev;

	new_dev = calloc(1, sizeof(*calloc));
	if (new_dev == NULL)
		return NULL;
	new_dev->iodev =
		alsa_iodev_create(card_index, device_index, mixer, direction);
	if (new_dev->iodev == NULL) {
		syslog(LOG_ERR, "Couldn't create alsa_iodev for %zu:%zu\n",
		       card_index, device_index);
		free(new_dev);
		return NULL;
	}

	return new_dev;
}

/* Read the config file for this card.  The config file will specify any special
 * volume curves needed for the device. */
static int read_card_config(dictionary **ini, const char *ini_dir,
			   const char *card_name)
{
	char ini_name[MAX_INI_NAME_LENGTH + 1];

	snprintf(ini_name, MAX_INI_NAME_LENGTH, "%s/%s", ini_dir, card_name);
	ini_name[MAX_INI_NAME_LENGTH] = '\0';
	*ini = iniparser_load(ini_name);
	if (*ini == NULL)
		return -EINVAL;
	return 0;
}

/*
 * Exported Interface.
 */

struct cras_alsa_card *cras_alsa_card_create(size_t card_idx)
{
	snd_ctl_t *handle = NULL;
	int rc, dev_idx;
	snd_ctl_card_info_t *card_info;
	snd_pcm_info_t *dev_info;
	struct cras_alsa_card *alsa_card;

	if (card_idx >= MAX_ALSA_CARDS) {
		syslog(LOG_ERR, "Invalid alsa card index %zu", card_idx);
		return NULL;
	}

	snd_ctl_card_info_alloca(&card_info);
	snd_pcm_info_alloca(&dev_info);

	alsa_card = calloc(1, sizeof(*alsa_card));
	if (alsa_card == NULL)
		return NULL;
	alsa_card->card_index = card_idx;

	snprintf(alsa_card->name,
		 MAX_ALSA_PCM_NAME_LENGTH,
		 "hw:%zu",
		 card_idx);

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

	/* Read config file for this card if it exists. */
	if (read_card_config(&alsa_card->ini, CARD_CONFIG_FILE_DIR,
			     snd_ctl_card_info_get_name(card_info)) < 0)
		syslog(LOG_DEBUG, "No config file for %s", alsa_card->name);

	/* Create one mixer per card. */
	alsa_card->mixer = cras_alsa_mixer_create(alsa_card->name,
						  alsa_card->ini);
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

			new_dev = create_iodev_for_device(card_idx,
							  dev_idx,
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

			new_dev = create_iodev_for_device(card_idx,
							  dev_idx,
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
	if (alsa_card->ini)
		iniparser_freedict(alsa_card->ini);
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
	if (alsa_card->ini)
		iniparser_freedict(alsa_card->ini);
	free(alsa_card);
}

size_t cras_alsa_card_get_index(const struct cras_alsa_card *alsa_card)
{
	assert(alsa_card);
	return alsa_card->card_index;
}
