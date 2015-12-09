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
#include "cras_hfp_ag_profile.h"
#include "cras_main_message.h"
#include "cras_system_state.h"
#include "cras_tm.h"
#include "cras_util.h"

#define A2DP_SOURCE_ENDPOINT_PATH "/org/chromium/Cras/Bluetooth/A2DPSource"
#define A2DP_SINK_ENDPOINT_PATH   "/org/chromium/Cras/Bluetooth/A2DPSink"

enum A2DP_COMMAND {
	A2DP_SCHEDULE_SUSPEND,
	A2DP_CANCEL_SUSPEND,
};

struct a2dp_msg {
	struct cras_main_message header;
	enum A2DP_COMMAND cmd;
	struct cras_iodev *dev;
	unsigned int arg;
};

/* Pointers for the only connected a2dp device. */
static struct a2dp {
	struct cras_iodev *iodev;
	struct cras_bt_device *device;
	struct cras_timer *suspend_timer;
} connected_a2dp;

int cras_a2dp_has_suspend_timer() {
	return !!connected_a2dp.suspend_timer;
}

void cras_a2dp_cancel_suspend_timer(struct cras_iodev *dev)
{
	int err;
	struct a2dp_msg msg;

	msg.header.type = CRAS_MAIN_A2DP;
	msg.header.length = sizeof(msg);
	msg.cmd = A2DP_CANCEL_SUSPEND;
	msg.dev = dev;

	err = cras_main_message_send((struct cras_main_message *)&msg);
	if (err < 0)
		syslog(LOG_ERR, "Failed to post a2dp cancel message");
}

/*
 * Force suspends a cras_iodev when unexpect error occurs.
 */
void cras_a2dp_schedule_suspend_timer(struct cras_iodev *dev,
				      unsigned int msec)
{
	int err;
	struct a2dp_msg msg;

	msg.header.type = CRAS_MAIN_A2DP;
	msg.header.length = sizeof(msg);
	msg.cmd = A2DP_SCHEDULE_SUSPEND;
	msg.dev = dev;
	msg.arg = msec;

	err = cras_main_message_send((struct cras_main_message *)&msg);
	if (err < 0) {
		syslog(LOG_ERR, "Failed to post a2dp schedule message");
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

	if (connected_a2dp.iodev) {
		syslog(LOG_WARNING,
		       "Replacing existing endpoint configuration");
		a2dp_iodev_destroy(connected_a2dp.iodev);
	}

	/* When A2DP-only device connected, suspend all HFP/HSP audio
	 * gateways. */
	if (!cras_bt_device_supports_profile(
			cras_bt_transport_device(transport),
			CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE |
			CRAS_BT_DEVICE_PROFILE_HSP_HEADSET))
		cras_hfp_ag_suspend();


	connected_a2dp.iodev = a2dp_iodev_create(transport);
	connected_a2dp.device = cras_bt_transport_device(transport);

	if (!connected_a2dp.iodev)
		syslog(LOG_WARNING, "Failed to create a2dp iodev");
}

static void cras_a2dp_suspend(struct cras_bt_endpoint *endpoint,
			      struct cras_bt_transport *transport)
{
	cras_a2dp_suspend_connected_device();
}

static void a2dp_suspend_timer_cb(struct cras_timer *timer, void *arg)
{
	struct cras_iodev *iodev = (struct cras_iodev *)arg;

	connected_a2dp.suspend_timer = NULL;
	if (connected_a2dp.iodev != iodev)
		return;

	cras_a2dp_suspend_connected_device();
}

/* Handles a2dp messages in main thread.
 */
static void a2dp_handle_message(struct cras_main_message *msg, void *arg)
{
	struct a2dp_msg *a2dp_msg = (struct a2dp_msg *)msg;
	struct cras_tm *tm = cras_system_state_get_tm();

	switch (a2dp_msg->cmd) {
	case A2DP_SCHEDULE_SUSPEND:
		/* If the iodev to force suspend no longer active,
		 * ignore the message. */
		if ((connected_a2dp.iodev != a2dp_msg->dev) ||
		    connected_a2dp.suspend_timer)
			break;
		connected_a2dp.suspend_timer = cras_tm_create_timer(
				tm, a2dp_msg->arg,
				a2dp_suspend_timer_cb, a2dp_msg->dev);
		break;
	case A2DP_CANCEL_SUSPEND:
		if (connected_a2dp.suspend_timer) {
			cras_tm_cancel_timer(tm, connected_a2dp.suspend_timer);
			connected_a2dp.suspend_timer = NULL;
		}
		break;
	default:
		syslog(LOG_ERR, "Unhandled a2dp command");
		break;
	}
	return;
}

static void a2dp_transport_state_changed(struct cras_bt_endpoint *endpoint,
					 struct cras_bt_transport *transport)
{
	if (connected_a2dp.iodev && transport) {
		/* When pending message is received in bluez, try to aquire
		 * the transport. */
		if (cras_bt_transport_fd(transport) != -1 &&
		    cras_bt_transport_state(transport) ==
				CRAS_BT_TRANSPORT_STATE_PENDING)
			cras_bt_transport_try_acquire(transport);
	}
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
	.suspend = cras_a2dp_suspend,
	.transport_state_changed = a2dp_transport_state_changed
};

int cras_a2dp_endpoint_create(DBusConnection *conn)
{
	cras_main_message_add_handler(CRAS_MAIN_A2DP,
				      a2dp_handle_message, NULL);
	return cras_bt_endpoint_add(conn, &cras_a2dp_endpoint);
}

struct cras_bt_device *cras_a2dp_connected_device()
{
	return connected_a2dp.device;
}

void cras_a2dp_suspend_connected_device()
{
	struct cras_tm *tm = cras_system_state_get_tm();

	if (connected_a2dp.iodev) {
		syslog(LOG_INFO, "Destroying iodev for A2DP device");
		a2dp_iodev_destroy(connected_a2dp.iodev);
		if (connected_a2dp.suspend_timer) {
			cras_tm_cancel_timer(tm, connected_a2dp.suspend_timer);
			connected_a2dp.suspend_timer = NULL;
		}

		connected_a2dp.iodev = NULL;
		connected_a2dp.device = NULL;
	}
}
