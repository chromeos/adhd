/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Types commonly used in the client and server are defined here.
 */
#ifndef CRAS_TYPES_H_
#define CRAS_TYPES_H_

#include <stdint.h>
#include <stdlib.h>

#include "cras_audio_format.h"
#include "cras_iodev_info.h"

/* Architecture independent timespec */
struct __attribute__ ((__packed__)) cras_timespec {
	int64_t tv_sec;
	int64_t tv_nsec;
};

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

static inline int cras_stream_is_unified(enum CRAS_STREAM_DIRECTION dir)
{
	return dir == CRAS_STREAM_UNIFIED;
}

static inline int cras_stream_is_loopback(enum CRAS_STREAM_DIRECTION dir)
{
	return dir == CRAS_STREAM_POST_MIX_PRE_DSP;
}

/* Types of audio streams. */
enum CRAS_STREAM_TYPE {
	CRAS_STREAM_TYPE_DEFAULT,
};

/* Information about a client attached to the server. */
struct __attribute__ ((__packed__)) cras_attached_client_info {
	uint32_t id;
	int32_t pid;
	uint32_t uid;
	uint32_t gid;
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
#define MAX_DEBUG_STREAMS 8
#define AUDIO_THREAD_EVENT_LOG_SIZE 4096

/* There are 8 bits of space for events. */
enum AUDIO_THREAD_LOG_EVENTS {
	AUDIO_THREAD_WAKE,
	AUDIO_THREAD_SLEEP,
	AUDIO_THREAD_READ_AUDIO,
	AUDIO_THREAD_READ_AUDIO_DONE,
	AUDIO_THREAD_FILL_AUDIO,
	AUDIO_THREAD_FILL_AUDIO_DONE,
	AUDIO_THREAD_WRITE_STREAMS_WAIT,
	AUDIO_THREAD_WRITE_STREAMS_WAIT_TO,
	AUDIO_THREAD_WRITE_STREAMS_MIX,
	AUDIO_THREAD_WRITE_STREAMS_MIXED,
	AUDIO_THREAD_INPUT_SLEEP,
	AUDIO_THREAD_OUTPUT_SLEEP,
	AUDIO_THREAD_LOOP_SLEEP,
	AUDIO_THREAD_WRITE_STREAMS_STREAM,
	AUDIO_THREAD_FETCH_STREAM,
};

/* Ring buffer of log events from the audio thread. */
struct __attribute__ ((__packed__)) audio_thread_event_log {
	uint32_t write_pos;
	uint32_t log[AUDIO_THREAD_EVENT_LOG_SIZE];
};

struct __attribute__ ((__packed__)) audio_stream_debug_info {
	uint64_t stream_id;
	uint32_t direction;
	uint32_t buffer_frames;
	uint32_t cb_threshold;
	uint32_t min_cb_level;
	uint32_t flags;
	uint32_t frame_rate;
	uint32_t num_channels;
	uint32_t num_cb_timeouts;
	int8_t channel_layout[CRAS_CH_MAX];
};

/* Debug info shared from server to client. */
struct __attribute__ ((__packed__)) audio_debug_info {
	char output_dev_name[CRAS_NODE_NAME_BUFFER_SIZE];
	uint32_t output_buffer_size;
	uint32_t output_used_size;
	uint32_t output_cb_threshold;
	char input_dev_name[CRAS_NODE_NAME_BUFFER_SIZE];
	uint32_t input_buffer_size;
	uint32_t input_used_size;
	uint32_t input_cb_threshold;
	uint32_t num_streams;
	struct audio_stream_debug_info streams[MAX_DEBUG_STREAMS];
	struct audio_thread_event_log log;
};


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
 *    audio_debug_info - Debug data filled in when a client requests it. This
 *        isn't protected against concurrent updating, only one client should
 *        use it.
 */
#define CRAS_SERVER_STATE_VERSION 2
struct __attribute__ ((__packed__)) cras_server_state {
	uint32_t state_version;
	uint32_t volume;
	int32_t min_volume_dBFS;
	int32_t max_volume_dBFS;
	int32_t mute;
	int32_t user_mute;
	int32_t mute_locked;
	int32_t capture_gain;
	int32_t capture_mute;
	int32_t capture_mute_locked;
	int32_t min_capture_gain;
	int32_t max_capture_gain;
	uint32_t num_streams_attached;
	uint32_t num_output_devs;
	uint32_t num_input_devs;
	struct cras_iodev_info output_devs[CRAS_MAX_IODEVS];
	struct cras_iodev_info input_devs[CRAS_MAX_IODEVS];
	uint32_t num_output_nodes;
	uint32_t num_input_nodes;
	struct cras_ionode_info output_nodes[CRAS_MAX_IONODES];
	struct cras_ionode_info input_nodes[CRAS_MAX_IONODES];
	cras_node_id_t selected_input;
	cras_node_id_t selected_output;
	uint32_t num_attached_clients;
	struct cras_attached_client_info client_info[CRAS_MAX_ATTACHED_CLIENTS];
	uint32_t update_count;
	uint32_t num_active_streams;
	struct cras_timespec last_active_stream_time;
	struct audio_debug_info audio_debug_info;
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
struct __attribute__ ((__packed__)) cras_alsa_card_info {
	enum CRAS_ALSA_CARD_TYPE card_type;
	uint32_t card_index;
	uint32_t usb_vendor_id;
	uint32_t usb_product_id;
	uint32_t usb_desc_checksum;
};

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
