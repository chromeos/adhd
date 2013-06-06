/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

void hfp_info_destroy(struct hfp_info *info)
{
	if (info->capture_buf)
		destroy_buf(info->capture_buf);

	if (info->playback_buf)
		destroy_buf(info->playback_buf);

	free(info);
}
