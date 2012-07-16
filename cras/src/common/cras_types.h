/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Types commonly used in the client and server are defined here.
 */
#ifndef CRAS_TYPES_H_
#define CRAS_TYPES_H_

#include <alsa/asoundlib.h>
#include <stdint.h>
#include <stdlib.h>

/* Directions of audio streams. */
enum CRAS_STREAM_DIRECTION {
	CRAS_STREAM_OUTPUT,
	CRAS_STREAM_INPUT,
};

/* Types of audio streams. */
enum CRAS_STREAM_TYPE {
	CRAS_STREAM_TYPE_DEFAULT,
};

/* Audio format. */
struct cras_audio_format {
	snd_pcm_format_t format;
	size_t frame_rate; /* Hz */
	size_t num_channels;
};

/* Information about a client attached to the server. */
struct cras_attached_client_info {
	size_t id;
	pid_t pid;
	uid_t uid;
	gid_t gid;
};

/* Actions for card add/remove/change. */
enum cras_notify_device_action { /* Must match gavd action definitions.  */
	CRAS_DEVICE_ACTION_ADD    = 0,
	CRAS_DEVICE_ACTION_REMOVE = 1,
	CRAS_DEVICE_ACTION_CHANGE = 2,
};

/* Information about an ALSA card to be added to the system.
 *    card_type - Either internal card or a USB sound card.
 *    card_index - Index ALSA uses to refer to the card.  The X in "hw:X".
 *    priority - Base priority to give devices found on this card. Zero is the
 *      lowest priority.  Non-primary devices on the card will be given a
 *      lowered priority.
 *    usb_vendor_id - vendor ID if the device is on the USB bus.
 *    usb_product_id - product ID if the device is on the USB bus.
 */
enum CRAS_ALSA_CARD_TYPE {
	ALSA_CARD_TYPE_INTERNAL,
	ALSA_CARD_TYPE_USB,
};
struct cras_alsa_card_info {
	enum CRAS_ALSA_CARD_TYPE card_type;
	unsigned card_index;
	unsigned priority;
	unsigned usb_vendor_id;
	unsigned usb_product_id;
};

/* Create an audio format structure. */
struct cras_audio_format *cras_audio_format_create(snd_pcm_format_t format,
						   size_t frame_rate,
						   size_t num_channels);

/* Destroy an audio format struct created with cras_audio_format_crate. */
void cras_audio_format_destroy(struct cras_audio_format *fmt);

/* Returns the number of bytes per sample.
 * This is bits per smaple / 8 * num_channels.
 */
static inline size_t cras_get_format_bytes(const struct cras_audio_format *fmt)
{
	const int bytes = snd_pcm_format_physical_width(fmt->format) / 8;
	return (size_t)bytes * fmt->num_channels;
}

/* Unique identifier for each active stream.
 * The top 16 bits are the client number, lower 16 are the stream number.
 */
typedef uint32_t cras_stream_id_t;
/* Generates a stream id for client stream. */
static inline cras_stream_id_t cras_get_stream_id(uint16_t client_id,
						  uint16_t stream_id)
{
	return (cras_stream_id_t)(((client_id & 0x0000ffff) << 16) |
				  (stream_id & 0x0000ffff));
}

#endif /* CRAS_TYPES_H_ */
