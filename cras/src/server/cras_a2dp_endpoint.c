/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <syslog.h>

#include "a2dp-codecs.h"
#include "cras_a2dp_endpoint.h"
#include "cras_a2dp_iodev.h"
#include "cras_iodev.h"
#include "cras_bt_constants.h"
#include "cras_bt_endpoint.h"
#include "cras_system_state.h"

#define A2DP_SOURCE_ENDPOINT_PATH "/org/chromium/Cras/Bluetooth/A2DPSource"
#define A2DP_SINK_ENDPOINT_PATH   "/org/chromium/Cras/Bluetooth/A2DPSink"

enum A2DP_COMMAND {
	A2DP_FORCE_SUSPEND,
};

struct a2dp_msg {
	enum A2DP_COMMAND cmd;
	struct cras_iodev *dev;
};

static struct cras_iodev *iodev;

/* To send a message to main thread. */
static int to_main_fds[2];

/*
 * Force suspends a cras_iodev when unexpect error occurs.
 */
static void cras_a2dp_force_suspend(struct cras_iodev *dev)
{
	int err;
	struct a2dp_msg msg;

	msg.cmd = A2DP_FORCE_SUSPEND;
	msg.dev = dev;

	err = write(to_main_fds[1], &msg, sizeof(msg));
	if (err < 0) {
		syslog(LOG_ERR, "Failed to post message to main thread");
		return;
	}
	return;
}

static int cras_a2dp_get_capabilities(struct cras_bt_endpoint *endpoint,
				      void *capabilities, int *len)
{
	a2dp_sbc_t *sbc_caps = capabilities;

	if (*len < sizeof(*sbc_caps))
		return -ENOSPC;

	*len = sizeof(*sbc_caps);

	/* Return all capabilities. */
	sbc_caps->channel_mode = SBC_CHANNEL_MODE_MONO |
			SBC_CHANNEL_MODE_DUAL_CHANNEL |
			SBC_CHANNEL_MODE_STEREO |
			SBC_CHANNEL_MODE_JOINT_STEREO;
	sbc_caps->frequency = SBC_SAMPLING_FREQ_16000 |
			SBC_SAMPLING_FREQ_32000 |
			SBC_SAMPLING_FREQ_44100 |
			SBC_SAMPLING_FREQ_48000;
	sbc_caps->allocation_method = SBC_ALLOCATION_SNR |
			SBC_ALLOCATION_LOUDNESS;
	sbc_caps->subbands = SBC_SUBBANDS_4 | SBC_SUBBANDS_8;
	sbc_caps->block_length = SBC_BLOCK_LENGTH_4 |
			SBC_BLOCK_LENGTH_8 |
			SBC_BLOCK_LENGTH_12 |
			SBC_BLOCK_LENGTH_16;
	sbc_caps->min_bitpool = MIN_BITPOOL;
	sbc_caps->max_bitpool = MAX_BITPOOL;

	return 0;
}

static int cras_a2dp_select_configuration(struct cras_bt_endpoint *endpoint,
					  void *capabilities, int len,
					  void *configuration)
{
	a2dp_sbc_t *sbc_caps = capabilities;
	a2dp_sbc_t *sbc_config = configuration;

	/* Pick the highest configuration. */
	if (sbc_caps->channel_mode & SBC_CHANNEL_MODE_JOINT_STEREO) {
		sbc_config->channel_mode = SBC_CHANNEL_MODE_JOINT_STEREO;
	} else if (sbc_caps->channel_mode & SBC_CHANNEL_MODE_STEREO) {
		sbc_config->channel_mode = SBC_CHANNEL_MODE_STEREO;
	} else if (sbc_caps->channel_mode & SBC_CHANNEL_MODE_DUAL_CHANNEL) {
		sbc_config->channel_mode = SBC_CHANNEL_MODE_DUAL_CHANNEL;
	} else if (sbc_caps->channel_mode & SBC_CHANNEL_MODE_MONO) {
		sbc_config->channel_mode = SBC_CHANNEL_MODE_MONO;
	} else {
		syslog(LOG_WARNING, "No supported channel modes.");
		return -ENOSYS;
	}

	if (sbc_caps->frequency & SBC_SAMPLING_FREQ_48000) {
		sbc_config->frequency = SBC_SAMPLING_FREQ_48000;
	} else if (sbc_caps->frequency & SBC_SAMPLING_FREQ_44100) {
		sbc_config->frequency = SBC_SAMPLING_FREQ_44100;
	} else if (sbc_caps->frequency & SBC_SAMPLING_FREQ_32000) {
		sbc_config->frequency = SBC_SAMPLING_FREQ_32000;
	} else if (sbc_caps->frequency & SBC_SAMPLING_FREQ_16000) {
		sbc_config->frequency = SBC_SAMPLING_FREQ_16000;
	} else {
		syslog(LOG_WARNING, "No supported sampling frequencies.");
		return -ENOSYS;
	}

	if (sbc_caps->allocation_method & SBC_ALLOCATION_LOUDNESS) {
		sbc_config->allocation_method = SBC_ALLOCATION_LOUDNESS;
	} else if (sbc_caps->allocation_method & SBC_ALLOCATION_SNR) {
		sbc_config->allocation_method = SBC_ALLOCATION_SNR;
	} else {
		syslog(LOG_WARNING, "No supported allocation method.");
		return -ENOSYS;
	}

	if (sbc_caps->subbands & SBC_SUBBANDS_8) {
		sbc_config->subbands = SBC_SUBBANDS_8;
	} else if (sbc_caps->subbands & SBC_SUBBANDS_4) {
		sbc_config->subbands = SBC_SUBBANDS_4;
	} else {
		syslog(LOG_WARNING, "No supported subbands.");
		return -ENOSYS;
	}

	if (sbc_caps->block_length & SBC_BLOCK_LENGTH_16) {
		sbc_config->block_length = SBC_BLOCK_LENGTH_16;
	} else if (sbc_caps->block_length & SBC_BLOCK_LENGTH_12) {
		sbc_config->block_length = SBC_BLOCK_LENGTH_12;
	} else if (sbc_caps->block_length & SBC_BLOCK_LENGTH_8) {
		sbc_config->block_length = SBC_BLOCK_LENGTH_8;
	} else if (sbc_caps->block_length & SBC_BLOCK_LENGTH_4) {
		sbc_config->block_length = SBC_BLOCK_LENGTH_4;
	} else {
		syslog(LOG_WARNING, "No supported block length.");
		return -ENOSYS;
	}

	sbc_config->min_bitpool = (sbc_caps->min_bitpool > MIN_BITPOOL
				   ? sbc_caps->min_bitpool : MIN_BITPOOL);
	sbc_config->max_bitpool = (sbc_caps->max_bitpool < MAX_BITPOOL
				   ? sbc_caps->max_bitpool : MAX_BITPOOL);

	return 0;
}

static void cras_a2dp_start(struct cras_bt_endpoint *endpoint,
			    struct cras_bt_transport *transport)
{
	syslog(LOG_INFO, "Creating iodev for A2DP device");

	if (iodev) {
		syslog(LOG_WARNING,
		       "Replacing existing endpoint configuration");
		a2dp_iodev_destroy(iodev);
	}

	iodev = a2dp_iodev_create(transport,
				  cras_a2dp_force_suspend);
	if (!iodev)
		syslog(LOG_WARNING, "Failed to create a2dp iodev");
}

static void cras_a2dp_suspend(struct cras_bt_endpoint *endpoint,
			      struct cras_bt_transport *transport)
{
	if (iodev) {
		syslog(LOG_INFO, "Destroying iodev for A2DP device");
		a2dp_iodev_destroy(iodev);
		iodev = NULL;
	}
}

/* Handles a2dp messages in main thread.
 */
static void a2dp_handle_message(void *arg)
{
	int rc;
	struct a2dp_msg msg;

	rc = read(to_main_fds[0], &msg, sizeof(msg));
	if (rc < 0)
		return;

	switch (msg.cmd) {
	case A2DP_FORCE_SUSPEND:
		/* If the iodev to force suspend no longer active,
		 * ignore the message. */
		if (iodev != msg.dev)
			break;
		a2dp_iodev_destroy(iodev);
		iodev = NULL;
		break;
	default:
		syslog(LOG_ERR, "Unhandled a2dp command");
		break;
	}
	return;
}

static struct cras_bt_endpoint cras_a2dp_endpoint = {
	/* BlueZ connects the device A2DP Sink to our A2DP Source endpoint,
	 * and the device A2DP Source to our A2DP Sink. It's best if you don't
	 * think about it too hard.
	 */
	.object_path = A2DP_SOURCE_ENDPOINT_PATH,
	.uuid = A2DP_SOURCE_UUID,
	.codec = A2DP_CODEC_SBC,

	.get_capabilities = cras_a2dp_get_capabilities,
	.select_configuration = cras_a2dp_select_configuration,
	.start = cras_a2dp_start,
	.suspend = cras_a2dp_suspend
};

int cras_a2dp_endpoint_create(DBusConnection *conn)
{
	int err;
	err = pipe(to_main_fds);
	if (err < 0) {
		syslog(LOG_ERR, "Failed to create pipe for a2dp endpoint");
		return err;
	}
	cras_system_add_select_fd(to_main_fds[0],
				  a2dp_handle_message,
				  &cras_a2dp_endpoint);
	return cras_bt_endpoint_add(conn, &cras_a2dp_endpoint);
}
