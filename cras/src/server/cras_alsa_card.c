/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <alsa/use-case.h>
#include <syslog.h>

#include "cras_alsa_card.h"
#include "cras_alsa_io.h"
#include "cras_alsa_mixer.h"
#include "cras_alsa_ucm.h"
#include "cras_device_blacklist.h"
#include "cras_card_config.h"
#include "cras_config.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_types.h"
#include "cras_util.h"
#include "utlist.h"

#define MAX_ALSA_CARDS 32 /* Alsa limit on number of cards. */
#define MAX_ALSA_PCM_NAME_LENGTH 6 /* Alsa names "hw:XX" + 1 for null. */
#define MAX_INI_NAME_LENGTH 63 /* 63 chars + 1 for null where declared. */

struct iodev_list_node {
	struct cras_iodev *iodev;
	enum CRAS_STREAM_DIRECTION direction;
	struct iodev_list_node *prev, *next;
};

/* Holds information about each sound card on the system.
 * name - of the form hw:XX,YY.
 * card_index - 0 based index, value of "XX" in the name.
 * iodevs - Input and output devices for this card.
 * mixer - Controls the mixer controls for this card.
 * ucm - ALSA use case manager if available.
 * config - Config info for this card, can be NULL if none found.
 */
struct cras_alsa_card {
	char name[MAX_ALSA_PCM_NAME_LENGTH];
	size_t card_index;
	struct iodev_list_node *iodevs;
	struct cras_alsa_mixer *mixer;
	snd_use_case_mgr_t *ucm;
	struct cras_card_config *config;
};

/* Checks if there are any devices with direction already in the card.
 */
int is_first_dev(struct cras_alsa_card *alsa_card,
		 enum CRAS_STREAM_DIRECTION direction)
{
	struct iodev_list_node *node;

	DL_FOREACH(alsa_card->iodevs, node)
		if (node->direction == direction)
			return 0;
	return 1;
}

/* Creates an iodev for the given device.
 * Args:
 *    alsa_card - the alsa_card the device will be added to.
 *    info - Information about the card type and priority.
 *    card_name - The name of the card.
 *    dev_name - The name of the device.
 *    device_index - 0 based index, value of "YY" in "hw:XX,YY".
 *    direction - Input or output.
 */
void create_iodev_for_device(struct cras_alsa_card *alsa_card,
			     struct cras_alsa_card_info *info,
			     const char *card_name,
			     const char *dev_name,
			     unsigned device_index,
			     enum CRAS_STREAM_DIRECTION direction)
{
	static const unsigned DEFAULT_ALSA_CARD_PRIORITY = 50;

	struct iodev_list_node *new_dev;
	unsigned priority = DEFAULT_ALSA_CARD_PRIORITY;
	int first;

	first = is_first_dev(alsa_card, direction);

	/* Dropping the priority of non-auto routed devices ensures
	 * that the auto-route devs are still selected first after the
	 * list is re-sorted.  Without this the order of devices
	 * within a card can't be determined when the list is
	 * resorted. */
	if (!first && priority > 0)
		priority--;

	new_dev = calloc(1, sizeof(*new_dev));
	if (new_dev == NULL)
		return;

	new_dev->direction = direction;
	new_dev->iodev = alsa_iodev_create(info->card_index,
					   card_name,
					   device_index,
					   dev_name,
					   info->card_type,
					   first,
					   alsa_card->mixer,
					   alsa_card->ucm,
					   priority,
					   direction);
	if (new_dev->iodev == NULL) {
		syslog(LOG_ERR, "Couldn't create alsa_iodev for %u:%u\n",
		       info->card_index, device_index);
		free(new_dev);
		return;
	}

	syslog(LOG_DEBUG, "New %s device %u:%d",
	       direction == CRAS_STREAM_OUTPUT ? "playback" : "capture",
	       info->card_index,
	       device_index);

	/* Set plugged for the first USB device per card when it appears. */
	if (info->card_type == ALSA_CARD_TYPE_USB && first) {
		cras_iodev_plug_event(new_dev->iodev, 1);
		cras_iodev_move_stream_type_top_prio(CRAS_STREAM_TYPE_DEFAULT,
						     direction);
	}

	DL_APPEND(alsa_card->iodevs, new_dev);
}

/* Check if a device should be ignored for this card. Returns non-zero if the
 * device is in the blacklist and should be ignored.
 */
static int should_ignore_dev(struct cras_alsa_card_info *info,
			     struct cras_device_blacklist *blacklist,
			     size_t device_index)
{
	if (info->card_type == ALSA_CARD_TYPE_USB)
		return cras_device_blacklist_check(blacklist,
						   info->usb_vendor_id,
						   info->usb_product_id,
						   info->usb_desc_checksum,
						   device_index);
	return 0;
}

/* Filters an array of mixer control names. Keep a name if it is
 * specified in the ucm config, otherwise set it to NULL */
static void filter_mixer_names(snd_use_case_mgr_t *ucm,
			       const char *names[],
			       size_t names_len)
{
	size_t i;
	for (i = 0; i < names_len; i++) {
		char *dev = ucm_get_dev_for_mixer(ucm, names[i]);
		if (dev)
			free(dev);
		else
			names[i] = NULL;
	}
}

/*
 * Exported Interface.
 */

struct cras_alsa_card *cras_alsa_card_create(
		struct cras_alsa_card_info *info,
		struct cras_device_blacklist *blacklist)
{
	snd_ctl_t *handle = NULL;
	int rc, dev_idx;
	snd_ctl_card_info_t *card_info;
	const char *card_name;
	snd_pcm_info_t *dev_info;
	struct cras_alsa_card *alsa_card;
	const char *output_names_extra[] = {
		"IEC958",
	};
	size_t output_names_extra_size = ARRAY_SIZE(output_names_extra);

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

	/* Create a use case manager if a configuration is available. */
	alsa_card->ucm = ucm_create(card_name);

	/* Filter the extra output mixer names */
	if (alsa_card->ucm)
		filter_mixer_names(alsa_card->ucm, output_names_extra,
				   output_names_extra_size);
	else
		output_names_extra_size = 0;

	/* Create one mixer per card. */
	alsa_card->mixer = cras_alsa_mixer_create(alsa_card->name,
						  alsa_card->config,
						  output_names_extra,
						  output_names_extra_size);
	if (alsa_card->mixer == NULL) {
		syslog(LOG_ERR, "Fail opening mixer for %s.", alsa_card->name);
		goto error_bail;
	}

	dev_idx = -1;
	while (1) {
		rc = snd_ctl_pcm_next_device(handle, &dev_idx);
		if (rc < 0) {
			cras_alsa_card_destroy(alsa_card);
			snd_ctl_close(handle);
			return NULL;
		}
		if (dev_idx < 0)
			break;

		snd_pcm_info_set_device(dev_info, dev_idx);
		snd_pcm_info_set_subdevice(dev_info, 0);

		/* Check for playback devices. */
		snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_PLAYBACK);
		if (snd_ctl_pcm_info(handle, dev_info) == 0 &&
		    !should_ignore_dev(info, blacklist, dev_idx))
			create_iodev_for_device(alsa_card,
						info,
						card_name,
						snd_pcm_info_get_name(dev_info),
						dev_idx,
						CRAS_STREAM_OUTPUT);

		/* Check for capture devices. */
		snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_CAPTURE);
		if (snd_ctl_pcm_info(handle, dev_info) == 0)
			create_iodev_for_device(alsa_card,
						info,
						card_name,
						snd_pcm_info_get_name(dev_info),
						dev_idx,
						CRAS_STREAM_INPUT);
	}

	snd_ctl_close(handle);
	return alsa_card;

error_bail:
	if (handle != NULL)
		snd_ctl_close(handle);
	if (alsa_card->ucm)
		ucm_destroy(alsa_card->ucm);
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
	if (alsa_card->ucm)
		ucm_destroy(alsa_card->ucm);
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
