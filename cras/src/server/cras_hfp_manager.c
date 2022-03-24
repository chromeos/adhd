/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for ppoll */
#endif

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "audio_thread.h"
#include "cras_audio_format.h"
#include "cras_config.h"
#include "cras_hfp_manager.h"
#include "cras_iodev_list.h"
#include "cras_fl_media.h"
#include "cras_fl_pcm_iodev.h"

#define CRAS_HFP_SOCKET_FILE ".hfp"
#define FLOSS_HFP_DATA_PATH "/run/bluetooth/audio/.sco_data"

/*
 * Object holding information and resources of a connected HFP headset.
 * Members:
 *    fm - Object representing the media interface of BT adapter.
 *    idev - The input iodev for HFP.
 *    odev - The output iodev for HFP.
 *    addr - The address of connected HFP device.
 *    fd - The file descriptor for SCO socket.
 *    idev_started - If an input device started. This is used to determine if
 *    	a sco start or stop is required.
 *    odev_started - If an output device started. This is used to determine if
 *    	a sco start or stop is required.
 */
struct cras_hfp {
	struct fl_media *fm;
	struct cras_iodev *idev;
	struct cras_iodev *odev;
	char *addr;
	int fd;
	int idev_started;
	int odev_started;
};

static struct cras_hfp *connected_hfp = NULL;

void fill_floss_hfp_skt_addr(struct sockaddr_un *addr)
{
	memset(addr, 0, sizeof(struct sockaddr_un));
	addr->sun_family = AF_UNIX;
	snprintf(addr->sun_path, CRAS_MAX_SOCKET_PATH_SIZE,
		 FLOSS_HFP_DATA_PATH);
}

void set_dev_started(struct cras_hfp *hfp, enum CRAS_STREAM_DIRECTION dir,
		     int started)
{
	if (dir == CRAS_STREAM_INPUT)
		hfp->idev_started = started;
	else if (dir == CRAS_STREAM_OUTPUT)
		hfp->odev_started = started;
}

/* Creates cras_hfp object representing a connected hfp device. */
struct cras_hfp *cras_floss_hfp_create(struct fl_media *fm, const char *addr)
{
	if (connected_hfp) {
		syslog(LOG_ERR, "Hfp already connected");
		return NULL;
	}
	connected_hfp = (struct cras_hfp *)calloc(1, sizeof(*connected_hfp));

	if (!connected_hfp)
		return NULL;

	connected_hfp->fm = fm;
	connected_hfp->addr = strdup(addr);
	connected_hfp->idev =
		hfp_pcm_iodev_create(connected_hfp, CRAS_STREAM_INPUT);
	connected_hfp->odev =
		hfp_pcm_iodev_create(connected_hfp, CRAS_STREAM_OUTPUT);

	connected_hfp->fd = -1;
	return connected_hfp;
}

int cras_floss_hfp_start(struct cras_hfp *hfp, thread_callback cb,
			 enum CRAS_STREAM_DIRECTION dir)
{
	int skt_fd;
	int rc;
	struct sockaddr_un addr;
	struct timespec timeout = { 10, 0 };
	struct pollfd poll_fd;

	if ((dir == CRAS_STREAM_INPUT && hfp->idev_started) ||
	    (dir == CRAS_STREAM_OUTPUT && hfp->odev_started))
		return -EINVAL;

	/* Check if the sco and socket connection has started by another
	 * direction's iodev. We can skip the data channel setup if so. */
	if (hfp->fd >= 0)
		goto start_dev;

	rc = floss_media_hfp_start_sco_call(hfp->fm, hfp->addr);
	if (rc < 0)
		return rc;

	/* TODO(b/213408429): Hook to a data path ready callback instead. */
	sleep(1);
	skt_fd = socket(PF_UNIX, SOCK_STREAM | O_NONBLOCK, 0);
	if (skt_fd < 0) {
		syslog(LOG_ERR, "Create HFP socket failed with error %d",
		       errno);
		rc = skt_fd;
		goto error;
	}

	fill_floss_hfp_skt_addr(&addr);

	syslog(LOG_DEBUG, "Connect to HFP socket at %s ", addr.sun_path);
	rc = connect(skt_fd, (struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0) {
		syslog(LOG_ERR, "Connect to HFP socket failed with error %d",
		       errno);
		goto error;
	}

	poll_fd.fd = skt_fd;
	poll_fd.events = POLLIN | POLLOUT;

	rc = ppoll(&poll_fd, 1, &timeout, NULL);
	if (rc <= 0) {
		syslog(LOG_ERR, "Poll for HFP socket failed with error %d",
		       errno);
		goto error;
	}

	if (poll_fd.revents & (POLLERR | POLLHUP)) {
		syslog(LOG_ERR, "HFP socket error, revents: %u.",
		       poll_fd.revents);
		rc = -1;
		goto error;
	}

	hfp->fd = skt_fd;
	audio_thread_add_events_callback(hfp->fd, cb, hfp,
					 POLLIN | POLLERR | POLLHUP);

start_dev:
	set_dev_started(hfp, dir, 1);

	return 0;
error:
	floss_media_hfp_stop_sco_call(hfp->fm, hfp->addr);
	if (skt_fd >= 0) {
		close(skt_fd);
		unlink(addr.sun_path);
	}
	return rc;
}

int cras_floss_hfp_stop(struct cras_hfp *hfp, enum CRAS_STREAM_DIRECTION dir)
{
	if (hfp->fd < 0)
		return 0;

	set_dev_started(hfp, dir, 0);

	if (hfp->idev_started || hfp->odev_started)
		return 0;

	audio_thread_rm_callback_sync(cras_iodev_list_get_audio_thread(),
				      hfp->fd);
	close(hfp->fd);
	hfp->fd = -1;

	return floss_media_hfp_stop_sco_call(hfp->fm, hfp->addr);
}

int cras_floss_hfp_get_fd(struct cras_hfp *hfp)
{
	return hfp->fd;
}

void cras_floss_hfp_get_iodevs(struct cras_hfp *hfp, struct cras_iodev **idev,
			       struct cras_iodev **odev)
{
	*idev = hfp->idev;
	*odev = hfp->odev;
}

const char *cras_floss_hfp_get_display_name(struct cras_hfp *hfp)
{
	/* TODO: resolve display name. */
	return hfp->addr;
}

const char *cras_floss_hfp_get_addr(struct cras_hfp *hfp)
{
	return hfp->addr;
}

int cras_floss_hfp_fill_format(struct cras_hfp *hfp, size_t **rates,
			       snd_pcm_format_t **formats,
			       size_t **channel_counts)
{
	/* TODO(b/214148074): Support WBS. hfp is passed in advance for it*/
	*rates = (size_t *)malloc(2 * sizeof(size_t));
	if (!*rates)
		return -ENOMEM;
	(*rates)[0] = 8000;
	(*rates)[1] = 0;

	*formats = (snd_pcm_format_t *)malloc(2 * sizeof(snd_pcm_format_t));
	if (!*formats)
		return -ENOMEM;
	(*formats)[0] = SND_PCM_FORMAT_S16_LE;
	(*formats)[1] = (snd_pcm_format_t)0;

	*channel_counts = (size_t *)malloc(2 * sizeof(size_t));
	if (!*channel_counts)
		return -ENOMEM;
	(*channel_counts)[0] = 1;
	(*channel_counts)[1] = 0;
	return 0;
}

/* Destroys given cras_hfp object. */
void cras_floss_hfp_destroy(struct cras_hfp *hfp)
{
	if (hfp->idev)
		hfp_pcm_iodev_destroy(hfp->idev);
	if (hfp->odev)
		hfp_pcm_iodev_destroy(hfp->odev);
	if (hfp->addr)
		free(hfp->addr);
	if (hfp->fd >= 0)
		close(hfp->fd);

	/* Must be the only static connected hfp that we are destroying,
	 * so clear it. */
	free(hfp);
	connected_hfp = NULL;
}
