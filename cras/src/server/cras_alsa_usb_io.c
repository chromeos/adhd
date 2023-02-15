/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_alsa_usb_io.h"

struct cras_iodev *cras_alsa_usb_iodev_create(
	size_t card_index, const char *card_name, size_t device_index,
	const char *pcm_name, const char *dev_name, const char *dev_id,
	enum CRAS_ALSA_CARD_TYPE card_type, int is_first,
	struct cras_alsa_mixer *mixer, const struct cras_card_config *config,
	struct cras_use_case_mgr *ucm, snd_hctl_t *hctl,
	enum CRAS_STREAM_DIRECTION direction, size_t usb_vid, size_t usb_pid,
	char *usb_serial_number)
{
	return NULL;
}

int cras_alsa_usb_iodev_legacy_complete_init(struct cras_iodev *iodev)
{
	return 0;
}

int cras_alsa_usb_iodev_ucm_add_nodes_and_jacks(struct cras_iodev *iodev,
						struct ucm_section *section)
{
	return 0;
}

void cras_alsa_usb_iodev_ucm_complete_init(struct cras_iodev *iodev)
{
	;
}

void cras_alsa_usb_iodev_destroy(struct cras_iodev *iodev)
{
	;
}

unsigned cras_alsa_usb_iodev_index(struct cras_iodev *iodev)
{
	return 0;
}

int cras_alsa_usb_iodev_has_hctl_jacks(struct cras_iodev *iodev)
{
	return 0;
}