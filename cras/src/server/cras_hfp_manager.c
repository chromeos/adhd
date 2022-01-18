/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for ppoll */
#endif

#include <poll.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "cras_config.h"
#include "cras_fl_media.h"
#include "cras_fl_pcm_iodev.h"
#include "cras_types.h"

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
 */
struct cras_hfp {
	struct fl_media *fm;
	struct cras_iodev *idev;
	struct cras_iodev *odev;
	char *addr;
	int fd;
};

static struct cras_hfp *connected_hfp = NULL;

void fill_floss_hfp_skt_addr(struct sockaddr_un *addr)
{
	memset(addr, 0, sizeof(struct sockaddr_un));
	addr->sun_family = AF_UNIX;
	snprintf(addr->sun_path, CRAS_MAX_SOCKET_PATH_SIZE,
		 FLOSS_HFP_DATA_PATH);
}

/* Creates cras_hfp object representing a connected hfp device. */
struct cras_hfp *cras_floss_hfp_create(struct fl_media *fm, const char *addr)
{
	if (connected_hfp) {
		syslog(LOG_ERR, "Hfp already connected");
		return NULL;
	}
	connected_hfp = (struct cras_hfp *)calloc(1, sizeof(*connected_hfp));

	connected_hfp->fm = fm;
	connected_hfp->addr = strdup(addr);
	connected_hfp->idev =
		hfp_pcm_iodev_create(connected_hfp, CRAS_STREAM_INPUT);
	connected_hfp->odev =
		hfp_pcm_iodev_create(connected_hfp, CRAS_STREAM_OUTPUT);
	connected_hfp->fd = -1;
	return connected_hfp;
}

int cras_floss_hfp_started(struct cras_hfp *hfp)
{
	return hfp->fd >= 0;
}

int cras_floss_hfp_get_fd(struct cras_hfp *hfp)
{
	return hfp->fd;
}

int cras_floss_hfp_start(struct cras_hfp *hfp)
{
	int skt_fd = -1;
	int rc;
	struct sockaddr_un addr;
	struct timespec timeout = { 1, 0 };
	struct pollfd poll_fd;

	if (hfp->fd >= 0)
		return -EINVAL;

	rc = floss_media_hfp_start_sco_call(hfp->fm, hfp->addr);
	if (rc < 0)
		return rc;

	skt_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (skt_fd < 0) {
		syslog(LOG_ERR, "Create HFP socket failed");
		return skt_fd;
	}

	fill_floss_hfp_skt_addr(&addr);

	rc = connect(skt_fd, (struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0) {
		syslog(LOG_ERR, "Connect to HFP socket failed");
		goto error;
	}

	poll_fd.fd = skt_fd;
	poll_fd.events = POLLOUT | POLLIN;

	rc = ppoll(&poll_fd, 1, &timeout, NULL);
	if (rc <= 0) {
		syslog(LOG_ERR, "Poll for HFP socket failed");
		goto error;
	}

	if (poll_fd.revents & (POLLERR | POLLHUP)) {
		syslog(LOG_ERR, "HFP socket error, revents: %u.",
		       poll_fd.revents);
		rc = -1;
		goto error;
	}

	hfp->fd = skt_fd;
	return 0;
error:
	if (skt_fd) {
		close(skt_fd);
		unlink(addr.sun_path);
	}
	return rc;
}

int cras_floss_hfp_stop(struct cras_hfp *hfp)
{
	int rc;

	if (hfp->fd < 0)
		return 0;

	close(hfp->fd);
	hfp->fd = -1;

	rc = floss_media_hfp_stop_sco_call(hfp->fm, hfp->addr);
	return rc;
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
