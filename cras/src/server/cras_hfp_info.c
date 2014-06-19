/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>

#include "audio_thread.h"
#include "cras_hfp_info.h"

/* Make buffer size of multiple MTUs (= 48 bytes * 21) */
#define HFP_BUF_SIZE_BYTES 1008

/* rate(8kHz) * sample_size(2 bytes) * channels(1) */
#define HFP_BYTE_RATE 16000
#define HFP_MTU_BYTES 48

/* Ring buffer storing samples for transmission. */
struct pcm_buf {
	uint8_t *buf;
	size_t read_idx;
	size_t write_idx;
	size_t used_size;
};

static struct pcm_buf *create_buf()
{
	struct pcm_buf *pb;
	pb = (struct pcm_buf *)calloc(1, sizeof(*pb));
	if (!pb)
		return NULL;

	pb->buf = (uint8_t *)calloc(HFP_BUF_SIZE_BYTES,
				    sizeof(*pb->buf));
	if (!pb->buf)
		return NULL;

	return pb;
}

static void destroy_buf(struct pcm_buf *pb)
{
	free(pb->buf);
	free(pb);
}

static void init_buf(struct pcm_buf *pb)
{
	pb->read_idx = 0;
	pb->write_idx = 0;
	pb->used_size = 0;
}

static int queued_bytes(struct pcm_buf *pb)
{
	return pb->used_size;
}

static void get_read_buf_bytes(struct pcm_buf *pb, uint8_t **b, unsigned *count)
{
	int avail;

	*b = pb->buf + pb->read_idx;
	if (pb->used_size == HFP_BUF_SIZE_BYTES
			|| pb->read_idx > pb->write_idx)
		avail = HFP_BUF_SIZE_BYTES - pb->read_idx;
	else
		avail = pb->write_idx - pb->read_idx;

	if ((int)*count > avail)
		*count = avail;
}

static void put_read_buf_bytes(struct pcm_buf *pb, unsigned nread)
{
	pb->read_idx += nread;
	if (pb->read_idx == HFP_BUF_SIZE_BYTES)
		pb->read_idx = 0;
	pb->used_size -= nread;
}

static void get_write_buf_bytes(struct pcm_buf *pb, uint8_t **b,
				unsigned *count)
{
	size_t avail;

	*b = pb->buf + pb->write_idx;
	if (pb->used_size == HFP_BUF_SIZE_BYTES
			|| pb->read_idx > pb->write_idx)
		avail = pb->read_idx - pb->write_idx;
	else
		avail = HFP_BUF_SIZE_BYTES - pb->write_idx;

	if (*count > avail)
		*count = avail;
}

static void put_write_buf_bytes(struct pcm_buf *pb, unsigned nwrite)
{
	pb->write_idx += nwrite;
	if (pb->write_idx == HFP_BUF_SIZE_BYTES)
		pb->write_idx = 0;
	pb->used_size += nwrite;
}

/* Structure to hold variables for a HFP connection. Since HFP supports
 * bi-direction audio, two iodevs should share one hfp_info if they
 * represent two directions of the same HFP headset
 */
struct hfp_info {
	int fd;
	int started;

	struct pcm_buf *capture_buf;
	struct pcm_buf *playback_buf;

	struct cras_iodev *idev;
	struct cras_iodev *odev;
};

int hfp_info_add_iodev(struct hfp_info *info, struct cras_iodev *dev)
{
	if (dev->direction == CRAS_STREAM_OUTPUT) {
		if (info->odev)
			goto invalid;
		info->odev = dev;

		init_buf(info->playback_buf);
	} else if (dev->direction == CRAS_STREAM_INPUT) {
		if (info->idev)
			goto invalid;
		info->idev = dev;

		init_buf(info->capture_buf);
	}

	return 0;

invalid:
	return -EINVAL;
}

int hfp_info_rm_iodev(struct hfp_info *info, struct cras_iodev *dev)
{
	if (dev->direction == CRAS_STREAM_OUTPUT && info->odev == dev) {
		info->odev = NULL;
	} else if (dev->direction == CRAS_STREAM_INPUT && info->idev == dev){
		info->idev = NULL;
	} else
		return -EINVAL;

	return 0;
}

int hfp_info_has_iodev(struct hfp_info *info)
{
	return info->odev || info->idev;
}

void hfp_buf_acquire(struct hfp_info *info, struct cras_iodev *dev,
		     uint8_t **buf, unsigned *count)
{
	size_t format_bytes;
	format_bytes = cras_get_format_bytes(dev->format);

	*count *= format_bytes;

	if (dev->direction == CRAS_STREAM_OUTPUT) {
		get_write_buf_bytes(info->playback_buf, buf, count);
	} else {
		get_read_buf_bytes(info->capture_buf, buf, count);
	}

	*count /= format_bytes;
}

int hfp_buf_size(struct hfp_info *info, struct cras_iodev *dev)
{
	return HFP_BUF_SIZE_BYTES / cras_get_format_bytes(dev->format);
}

void hfp_buf_release(struct hfp_info *info, struct cras_iodev *dev,
		     unsigned written_frames)
{
	size_t format_bytes;
	format_bytes = cras_get_format_bytes(dev->format);

	written_frames *= format_bytes;

	if (dev->direction == CRAS_STREAM_OUTPUT)
		put_write_buf_bytes(info->playback_buf, written_frames);
	else
		put_read_buf_bytes(info->capture_buf, written_frames);
}

int hfp_buf_queued(struct hfp_info *info, const struct cras_iodev *dev)
{
	size_t format_bytes;
	format_bytes = cras_get_format_bytes(dev->format);

	if (dev->direction == CRAS_STREAM_OUTPUT)
		return queued_bytes(info->playback_buf) / format_bytes;
	else
		return queued_bytes(info->capture_buf) / format_bytes;
}

int hfp_write(struct hfp_info *info)
{
	int err = 0;
	unsigned to_send;
	uint8_t *samples;

	/* Write something */
	to_send = HFP_MTU_BYTES;
	get_read_buf_bytes(info->playback_buf, &samples, &to_send);
	if (to_send != HFP_MTU_BYTES) {
		syslog(LOG_ERR, "Buffer not enough for write.");
		return 0;
	}

send_sample:
	err = send(info->fd, samples, to_send, 0);
	if (err < 0) {
		if (errno == EINTR)
			goto send_sample;

		return err;
	}

	if (err != HFP_MTU_BYTES) {
		syslog(LOG_ERR, "Partially write %d bytes", err);
		return -1;
	}

	put_read_buf_bytes(info->playback_buf, to_send);

	return err;
}

int hfp_read(struct hfp_info *info)
{
	int err = 0;
	unsigned to_read;
	uint8_t *capture_buf;

	to_read = HFP_MTU_BYTES;
	get_write_buf_bytes(info->capture_buf, &capture_buf, &to_read);
	if (to_read != HFP_MTU_BYTES) {
		syslog(LOG_ERR, "Buffer not enough for read.");
		return 0;
	}

recv_sample:
	err = recv(info->fd, capture_buf, to_read, 0);
	if (err < 0) {
		syslog(LOG_ERR, "Read error %s", strerror(errno));
		if (errno == EINTR)
			goto recv_sample;

		return err;
	}

	if (err != HFP_MTU_BYTES) {
		syslog(LOG_ERR, "Partially read %d bytes", err);
		return -1;
	}

	put_write_buf_bytes(info->capture_buf, err);

	return err;
}

/* Callback function to handle sample read and write.
 * Note that we poll the SCO socket for read sample, since it reflects
 * there is actual some sample to read while the socket always reports
 * writable even when device buffer is full.
 * The strategy is to synchronize read & write operations:
 * 1. Read one chunk of MTU bytes of data.
 * 2. When input device not attached, ignore the data just read.
 * 3. When output device attached, write one chunk of MTU bytes of data.
 */
static int hfp_info_callback(void *arg)
{
	struct hfp_info *info = (struct hfp_info *)arg;
	int err;

	err = hfp_read(info);
	if (err < 0) {
		syslog(LOG_ERR, "Read error");
		goto read_write_error;
	}

	/* Ignore the MTU bytes just read if input dev not in present */
	if (!info->idev)
		put_read_buf_bytes(info->capture_buf, HFP_MTU_BYTES);

	if (info->odev) {
		err = hfp_write(info);
		if (err < 0) {
			syslog(LOG_ERR, "Write error");
			goto read_write_error;
		}
	}

	return 0;

read_write_error:
	hfp_info_stop(info);

	return 0;
}

struct hfp_info *hfp_info_create()
{
	struct hfp_info *info;
	info = (struct hfp_info *)calloc(1, sizeof(*info));
	if (!info)
		goto error;

	info->capture_buf = create_buf();
	if (!info->capture_buf)
		goto error;

	info->playback_buf = create_buf();
	if (!info->playback_buf)
		goto error;

	init_buf(info->playback_buf);
	init_buf(info->capture_buf);

	return info;

error:
	if (info) {
		if (info->capture_buf)
			destroy_buf(info->capture_buf);
		if (info->playback_buf)
			destroy_buf(info->playback_buf);
		free(info);
	}
	return NULL;
}

int hfp_info_running(struct hfp_info *info)
{
	return info->started;
}

int hfp_info_start(int fd, struct hfp_info *info)
{
	info->fd = fd;
	init_buf(info->playback_buf);
	init_buf(info->capture_buf);

	audio_thread_add_callback(info->fd, hfp_info_callback, info);

	info->started = 1;

	return 0;
}

int hfp_info_stop(struct hfp_info *info)
{
	audio_thread_rm_callback(info->fd);

	close(info->fd);
	info->fd = 0;
	info->started = 0;

	return 0;
}

void hfp_info_destroy(struct hfp_info *info)
{
	if (info->capture_buf)
		destroy_buf(info->capture_buf);

	if (info->playback_buf)
		destroy_buf(info->playback_buf);

	free(info);
}
