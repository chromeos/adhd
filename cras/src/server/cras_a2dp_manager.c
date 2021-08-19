/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for ppoll */
#endif

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>

#include "cras_a2dp_pcm_iodev.h"
#include "cras_bt_log.h"
#include "cras_config.h"
#include "cras_fl_media.h"
#include "cras_iodev.h"
#include "cras_main_message.h"
#include "cras_system_state.h"
#include "cras_tm.h"

#define CRAS_A2DP_SOCKET_FILE ".a2dp"
#define CRAS_A2DP_SUSPEND_DELAY_MS (5000)
#define FLOSS_A2DP_DATA_PATH "/run/bluetooth/.a2dp_data"

/*
 * Object holding information and resources of a connected A2DP headset.
 * Members:
 *    fm - Object representing the media interface of BT adapter.
 *    iodev - The connected a2dp iodev.
 *    skt_fd - The socket fd to the a2dp device.
 *    suspend_timer - Timer to schedule suspending iodev at failures.
 *    addr - The address of connected a2dp device.
 */
struct cras_a2dp {
	struct fl_media *fm;
	struct cras_iodev *iodev;
	int skt_fd;
	struct cras_timer *suspend_timer;
	char *addr;
};

/* We assume at most one connected A2DP headset at this moment. */
static struct cras_a2dp *connected_a2dp = NULL;

enum A2DP_COMMAND {
	A2DP_CANCEL_SUSPEND,
	A2DP_SCHEDULE_SUSPEND,
};

struct a2dp_msg {
	struct cras_main_message header;
	enum A2DP_COMMAND cmd;
	struct cras_iodev *dev;
	unsigned int arg1;
};

void fill_local_a2dp_skt_addr(struct sockaddr_un *addr)
{
	memset(addr, 0, sizeof(struct sockaddr_un));
	addr->sun_family = AF_UNIX;
	snprintf(addr->sun_path, CRAS_MAX_SOCKET_PATH_SIZE, "%s/%s",
		 cras_config_get_system_socket_file_dir(),
		 CRAS_A2DP_SOCKET_FILE);
}

void fill_floss_a2dp_skt_addr(struct sockaddr_un *addr)
{
	memset(addr, 0, sizeof(struct sockaddr_un));
	addr->sun_family = AF_UNIX;
	snprintf(addr->sun_path, CRAS_MAX_SOCKET_PATH_SIZE,
		 FLOSS_A2DP_DATA_PATH);
}

static void a2dp_suspend_cb(struct cras_timer *timer, void *arg);

static void a2dp_schedule_suspend(unsigned int msec)
{
	struct cras_tm *tm;

	if (connected_a2dp->suspend_timer)
		return;

	tm = cras_system_state_get_tm();
	connected_a2dp->suspend_timer =
		cras_tm_create_timer(tm, msec, a2dp_suspend_cb, NULL);
}

static void a2dp_cancel_suspend()
{
	struct cras_tm *tm;

	if (connected_a2dp->suspend_timer == NULL)
		return;

	tm = cras_system_state_get_tm();
	cras_tm_cancel_timer(tm, connected_a2dp->suspend_timer);
	connected_a2dp->suspend_timer = NULL;
}

static void a2dp_process_msg(struct cras_main_message *msg, void *arg)
{
	struct a2dp_msg *a2dp_msg = (struct a2dp_msg *)msg;

	/* If the message was originated when another a2dp iodev was
	 * connected, simply ignore it. */
	if (a2dp_msg->dev == NULL || !connected_a2dp ||
	    connected_a2dp->iodev != a2dp_msg->dev)
		return;

	switch (a2dp_msg->cmd) {
	case A2DP_CANCEL_SUSPEND:
		a2dp_cancel_suspend();
		break;
	case A2DP_SCHEDULE_SUSPEND:
		a2dp_schedule_suspend(a2dp_msg->arg1);
		break;
	default:
		break;
	}
}

struct cras_a2dp *cras_floss_a2dp_create(struct fl_media *fm, const char *addr,
					 int sample_rate, int bits_per_sample,
					 int channel_mode)
{
	if (connected_a2dp) {
		syslog(LOG_ERR, "A2dp already connected");
		return NULL;
	}

	connected_a2dp = (struct cras_a2dp *)calloc(1, sizeof(*connected_a2dp));

	connected_a2dp->fm = fm;
	connected_a2dp->addr = strdup(addr);
	connected_a2dp->iodev = a2dp_pcm_iodev_create(
		connected_a2dp, sample_rate, bits_per_sample, channel_mode);
	connected_a2dp->skt_fd = -1;

	BTLOG(btlog, BT_A2DP_START, 0, 0);
	cras_main_message_add_handler(CRAS_MAIN_A2DP, a2dp_process_msg, NULL);

	return connected_a2dp;
}

void cras_floss_a2dp_destroy(struct cras_a2dp *a2dp)
{
	/* Iodev could be NULL if there was a suspend. */
	if (a2dp->iodev)
		a2dp_pcm_iodev_destroy(a2dp->iodev);
	if (a2dp->addr)
		free(a2dp->addr);

	/* Iodev has been destroyed. This is called in main thread so it's
	 * safe to suspend timer if there's any. */
	cras_main_message_rm_handler(CRAS_MAIN_A2DP);
	a2dp_cancel_suspend();

	/* Must be the only static connected a2dp that we are destroying,
	 * so clear it. */
	free(a2dp);
	connected_a2dp = NULL;
}

int cras_floss_a2dp_fill_format(int sample_rate, int bits_per_sample,
				int channel_mode, size_t **rates,
				snd_pcm_format_t **formats,
				size_t **channel_counts)
{
	int i;
	*rates = (size_t *)calloc(FL_SAMPLE_RATES + 1, sizeof(**rates));
	if (!*rates)
		return -ENOMEM;

	i = 0;
	if (sample_rate & FL_RATE_44100)
		(*rates)[i++] = 44100;
	if (sample_rate & FL_RATE_48000)
		(*rates)[i++] = 48000;
	if (sample_rate & FL_RATE_88200)
		(*rates)[i++] = 88200;
	if (sample_rate & FL_RATE_96000)
		(*rates)[i++] = 96000;
	if (sample_rate & FL_RATE_176400)
		(*rates)[i++] = 176400;
	if (sample_rate & FL_RATE_192000)
		(*rates)[i++] = 192000;
	if (sample_rate & FL_RATE_16000)
		(*rates)[i++] = 16000;
	if (sample_rate & FL_RATE_24000)
		(*rates)[i++] = 24000;
	(*rates)[i] = 0;

	*formats = (snd_pcm_format_t *)calloc(FL_SAMPLE_SIZES + 1,
					      sizeof(**formats));
	if (!*formats) {
		free(*rates);
		return -ENOMEM;
	}
	i = 0;
	if (bits_per_sample & FL_SAMPLE_16)
		(*formats)[i++] = SND_PCM_FORMAT_S16_LE;
	if (bits_per_sample & FL_SAMPLE_24)
		(*formats)[i++] = SND_PCM_FORMAT_S24_LE;
	if (bits_per_sample & FL_SAMPLE_32)
		(*formats)[i++] = SND_PCM_FORMAT_S32_LE;
	(*formats)[i] = 0;

	*channel_counts =
		(size_t *)calloc(FL_NUM_CHANNELS + 1, sizeof(**channel_counts));
	if (!*channel_counts) {
		free(*rates);
		free(*formats);
		return -ENOMEM;
	}
	i = 0;
	if (channel_mode & FL_MODE_MONO)
		(*channel_counts)[i++] = 1;
	if (channel_mode & FL_MODE_STEREO)
		(*channel_counts)[i++] = 2;
	(*channel_counts)[i] = 0;

	return 0;
}

static void a2dp_suspend_cb(struct cras_timer *timer, void *arg)
{
	if (connected_a2dp == NULL)
		return;

	/* Here the 'suspend' means we don't want to give a2dp to user as
	 * audio option anymore because of failures. However we can't
	 * alter the state that the a2dp device is still connected, i.e
	 * the structure of fl_media and presense of cras_a2dp. The best
	 * we can do is to destroy the iodev. */
	syslog(LOG_WARNING, "Destroying iodev for A2DP device");
	a2dp_pcm_iodev_destroy(connected_a2dp->iodev);
	connected_a2dp->iodev = NULL;
}

const char *cras_floss_a2dp_get_display_name(struct cras_a2dp *a2dp)
{
	// TODO: resolve display name
	return a2dp->addr;
}

const char *cras_floss_a2dp_get_addr(struct cras_a2dp *a2dp)
{
	return a2dp->addr;
}

int cras_floss_a2dp_stop(struct cras_a2dp *a2dp)
{
	floss_media_a2dp_stop_audio_request(a2dp->fm);
	return 0;
}

static void init_a2dp_msg(struct a2dp_msg *msg, enum A2DP_COMMAND cmd,
			  struct cras_iodev *dev, unsigned int arg1)
{
	memset(msg, 0, sizeof(*msg));
	msg->header.type = CRAS_MAIN_A2DP;
	msg->header.length = sizeof(*msg);
	msg->cmd = cmd;
	msg->dev = dev;
	msg->arg1 = arg1;
}

static void send_a2dp_message(enum A2DP_COMMAND cmd, unsigned int arg1)
{
	struct a2dp_msg msg = CRAS_MAIN_MESSAGE_INIT;
	int rc;

	init_a2dp_msg(&msg, cmd, connected_a2dp->iodev, arg1);
	rc = cras_main_message_send((struct cras_main_message *)&msg);
	if (rc)
		syslog(LOG_ERR, "Failed to send a2dp message %d", cmd);
}

static void audio_format_to_floss(const struct cras_audio_format *fmt,
				  int *sample_rate, int *bits_per_sample,
				  int *channel_mode)
{
	switch (fmt->frame_rate) {
	case 44100:
		*sample_rate = FL_RATE_44100;
		break;
	case 48000:
		*sample_rate = FL_RATE_48000;
		break;
	case 88200:
		*sample_rate = FL_RATE_88200;
		break;
	case 96000:
		*sample_rate = FL_RATE_96000;
		break;
	case 176400:
		*sample_rate = FL_RATE_176400;
		break;
	case 192000:
		*sample_rate = FL_RATE_192000;
		break;
	case 16000:
		*sample_rate = FL_RATE_16000;
		break;
	case 24000:
		*sample_rate = FL_RATE_24000;
		break;
	default:
		syslog(LOG_ERR, "Unsupported rate %zu in Floss",
		       fmt->frame_rate);
		*sample_rate = FL_RATE_NONE;
	}
	switch (fmt->format) {
	case SND_PCM_FORMAT_S16_LE:
		*bits_per_sample = FL_SAMPLE_16;
		break;
	case SND_PCM_FORMAT_S24_LE:
		*bits_per_sample = FL_SAMPLE_24;
		break;
	case SND_PCM_FORMAT_S32_LE:
		*bits_per_sample = FL_SAMPLE_32;
		break;
	default:
		*bits_per_sample = FL_SAMPLE_NONE;
	}
	switch (fmt->num_channels) {
	case 1:
		*channel_mode = FL_MODE_MONO;
		break;
	case 2:
		*channel_mode = FL_MODE_STEREO;
		break;
	default:
		*channel_mode = FL_MODE_NONE;
	}
}

int cras_floss_a2dp_start(struct cras_a2dp *a2dp, struct cras_audio_format *fmt,
			  int *skt)
{
	int skt_fd = -1;
	int rc;
	struct sockaddr_un addr;
	struct timespec timeout = { 1, 0 };
	struct pollfd poll_fd;
	int sample_rate, bits_per_sample, channel_mode;

	/* Set active device, set audio config, start audio request
	 * and then finally connect the socket. */
	floss_media_a2dp_set_active_device(a2dp->fm, a2dp->addr);
	audio_format_to_floss(fmt, &sample_rate, &bits_per_sample,
			      &channel_mode);
	floss_media_a2dp_set_audio_config(a2dp->fm, sample_rate,
					  bits_per_sample, channel_mode);
	floss_media_a2dp_start_audio_request(a2dp->fm);

	skt_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (skt_fd < 0) {
		syslog(LOG_ERR, "A2DP socket failed");
		return skt_fd;
	}

	fill_floss_a2dp_skt_addr(&addr);

	rc = connect(skt_fd, (struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0) {
		syslog(LOG_ERR, "Connect to A2DP socket failed");
		goto error;
	}

	poll_fd.fd = skt_fd;
	poll_fd.events = POLLOUT;

	rc = ppoll(&poll_fd, 1, &timeout, NULL);
	if (rc <= 0) {
		syslog(LOG_ERR, "Poll for A2DP socket failed");
		goto error;
	}

	if (poll_fd.revents & (POLLERR | POLLHUP)) {
		syslog(LOG_ERR,
		       "A2DP socket error, revents: %u. Suspend in %u seconds",
		       poll_fd.revents, CRAS_A2DP_SUSPEND_DELAY_MS);
		send_a2dp_message(A2DP_SCHEDULE_SUSPEND,
				  CRAS_A2DP_SUSPEND_DELAY_MS);
		rc = -1;
		goto error;
	}

	*skt = skt_fd;
	return 0;
error:
	if (skt_fd) {
		close(skt_fd);
		unlink(addr.sun_path);
	}
	return rc;
}

void cras_a2dp_schedule_suspend(unsigned int msec)
{
	send_a2dp_message(A2DP_SCHEDULE_SUSPEND, msec);
}

void cras_a2dp_cancel_suspend()
{
	send_a2dp_message(A2DP_CANCEL_SUSPEND, 0);
}
