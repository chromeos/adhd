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

#include "cras_iodev_info.h"

/* Directions of audio streams.
 * Input, Output, or Unified (Both input and output synchronously).
 */
enum CRAS_STREAM_DIRECTION {
	CRAS_STREAM_OUTPUT,
	CRAS_STREAM_INPUT,
	CRAS_STREAM_UNIFIED,
	CRAS_STREAM_POST_MIX_PRE_DSP,
};

static inline int cras_stream_uses_output_hw(enum CRAS_STREAM_DIRECTION dir)
{
	return dir == CRAS_STREAM_OUTPUT || dir == CRAS_STREAM_UNIFIED;
}

static inline int cras_stream_uses_input_hw(enum CRAS_STREAM_DIRECTION dir)
{
	return dir == CRAS_STREAM_INPUT || dir == CRAS_STREAM_UNIFIED;
}

static inline int cras_stream_has_input(enum CRAS_STREAM_DIRECTION dir)
{
	return dir != CRAS_STREAM_OUTPUT;
}

static inline int cras_stream_is_loopback(enum CRAS_STREAM_DIRECTION dir)
{
	return dir == CRAS_STREAM_POST_MIX_PRE_DSP;
}

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

/* Each ionode has a unique id. The top 32 bits are the device index, lower 32
 * are the node index. */
typedef uint64_t cras_node_id_t;

static inline cras_node_id_t cras_make_node_id(uint32_t dev_index,
					       uint32_t node_index)
{
	cras_node_id_t id = dev_index;
	return (id << 32) | node_index;
}

static inline uint32_t dev_index_of(cras_node_id_t id)
{
	return (uint32_t) (id >> 32);
}

static inline uint32_t node_index_of(cras_node_id_t id)
{
	return (uint32_t) id;
}

#define CRAS_MAX_IODEVS 20
#define CRAS_MAX_IONODES 20
#define CRAS_MAX_ATTACHED_CLIENTS 20

/* The server state that is shared with clients.
 *    state_version - Version of this structure.
 *    volume - index from 0-100.
 *    min_volume_dBFS - volume in dB * 100 when volume = 1.
 *    max_volume_dBFS - volume in dB * 100 when volume = max.
 *    mute - 0 = unmuted, 1 = muted by system (device switch, suspend, etc).
 *    user_mute - 0 = unmuted, 1 = muted by user.
 *    mute_locked - 0 = unlocked, 1 = locked.
 *    capture_gain - Capture gain in dBFS * 100.
 *    capture_mute - 0 = unmuted, 1 = muted.
 *    capture_mute_locked - 0 = unlocked, 1 = locked.
 *    min_capture_gain - Min allowed capture gain in dBFS * 100.
 *    max_capture_gain - Max allowed capture gain in dBFS * 100.
 *    num_streams_attached - Total number of streams since server started.
 *    num_output_devs - Number of available output devices.
 *    num_input_devs - Number of available input devices.
 *    output_devs - Output audio devices currently attached.
 *    input_devs - Input audio devices currently attached.
 *    num_output_nodes - Number of available output nodes.
 *    num_input_nodes - Number of available input nodes.
 *    output_nodes - Output nodes currently attached.
 *    input_nodes - Input nodes currently attached.
 *    selected_input - The input node currently selected. 0 if none selected.
 *    selected_output - The output node currently selected. 0 if none selected.
 *    num_attached_clients - Number of clients attached to server.
 *    client_info - List of first 20 attached clients.
 *    update_count - Incremented twice each time the struct is updated.  Odd
 *        during updates.
 *    num_active_streams - Number of streams currently playing or recording
 *        audio.
 *    last_active_stream_time - Time the last stream was removed.  Can be used
 *        to determine how long audio has been idle.
 */
#define CRAS_SERVER_STATE_VERSION 1
struct cras_server_state {
	unsigned state_version;
	size_t volume;
	long min_volume_dBFS;
	long max_volume_dBFS;
	int mute;
	int user_mute;
	int mute_locked;
	long capture_gain;
	int capture_mute;
	int capture_mute_locked;
	long min_capture_gain;
	long max_capture_gain;
	unsigned num_streams_attached;
	unsigned num_output_devs;
	unsigned num_input_devs;
	struct cras_iodev_info output_devs[CRAS_MAX_IODEVS];
	struct cras_iodev_info input_devs[CRAS_MAX_IODEVS];
	unsigned num_output_nodes;
	unsigned num_input_nodes;
	struct cras_ionode_info output_nodes[CRAS_MAX_IONODES];
	struct cras_ionode_info input_nodes[CRAS_MAX_IONODES];
	cras_node_id_t selected_input;
	cras_node_id_t selected_output;
	unsigned num_attached_clients;
	struct cras_attached_client_info client_info[CRAS_MAX_ATTACHED_CLIENTS];
	unsigned update_count;
	unsigned num_active_streams;
	struct timespec last_active_stream_time;
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
 *    usb_desc_checksum - the checksum of the USB descriptors if the device
 *      is on the USB bus.
 */
enum CRAS_ALSA_CARD_TYPE {
	ALSA_CARD_TYPE_INTERNAL,
	ALSA_CARD_TYPE_USB,
};
struct cras_alsa_card_info {
	enum CRAS_ALSA_CARD_TYPE card_type;
	unsigned card_index;
	unsigned usb_vendor_id;
	unsigned usb_product_id;
	unsigned usb_desc_checksum;
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

enum CRAS_NODE_TYPE {
	/* These value can be used for output nodes. */
	CRAS_NODE_TYPE_INTERNAL_SPEAKER,
	CRAS_NODE_TYPE_HEADPHONE,
	CRAS_NODE_TYPE_HDMI,
	/* These value can be used for input nodes. */
	CRAS_NODE_TYPE_INTERNAL_MIC,
	CRAS_NODE_TYPE_MIC,
	/* These value can be used for both output and input nodes. */
	CRAS_NODE_TYPE_USB,
	CRAS_NODE_TYPE_BLUETOOTH,
	CRAS_NODE_TYPE_UNKNOWN,
};

#endif /* CRAS_TYPES_H_ */
