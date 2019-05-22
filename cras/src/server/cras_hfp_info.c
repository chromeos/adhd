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
#include "byte_buffer.h"
#include "cras_iodev_list.h"
#include "cras_hfp_info.h"
#include "cras_hfp_slc.h"
#include "cras_sbc_codec.h"
#include "utlist.h"

/* The max buffer size. Note that the actual used size must set to multiple
 * of SCO packet size, and the packet size does not necessarily be equal to
 * MTU. We should keep this as common multiple of possible packet sizes, for
 * example: 48, 60, 64, 128.
 */
#define MAX_HFP_BUF_SIZE_BYTES 28800

/* rate(8kHz) * sample_size(2 bytes) * channels(1) */
#define HFP_BYTE_RATE 16000

/* Per Bluetooth Core v5.0 and HFP 1.7 specification. */
#define MSBC_H2_HEADER_LEN	2
#define MSBC_FRAME_LEN		57
#define MSBC_FRAME_SIZE		59
#define MSBC_CODE_SIZE		240
#define MSBC_SYNC_WORD		0xAD

/* For one mSBC 1 compressed wideband audio channel the HCI packets will
 * be 3 octets of HCI header + 60 octets of data. */
#define MSBC_PKT_SIZE 60
#define WRITE_BUF_SIZE_BYTES	MSBC_PKT_SIZE
#define READ_BUF_SIZE_BYTES	(2 * MSBC_PKT_SIZE)

#define H2_HEADER_0		0x01

/* Second octet of H2 header is composed by 4 bits fixed 0x8 and 4 bits
 * sequence number 0000, 0011, 1100, 1111. */
static const uint8_t h2_header_frames_count[] = {
	0x08, 0x38, 0xc8, 0xf8
};

/* The pre-computed zero input bit stream of mSBC codec, per HFP 1.7 spec.
 * This mSBC frame will be decoded into all-zero input PCM. */
static const uint8_t msbc_zero_frame[] = {
	0xad, 0x00, 0x00, 0xc5, 0x00, 0x00, 0x00, 0x00, 0x77, 0x6d,
	0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d, 0xdd, 0xb6,
	0xdb, 0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb,
	0x6d, 0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d,
	0xb7, 0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6,
	0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6c
};

/* Structure to hold variables for a HFP connection. Since HFP supports
 * bi-direction audio, two iodevs should share one hfp_info if they
 * represent two directions of the same HFP headset
 * Members:
 *     fd - The file descriptor for SCO socket.
 *     started - If the hfp_info has started to read/write SCO data.
 *     mtu - The max transmit unit reported from BT adapter.
 *     packet_size - The size of SCO packet to read/write preferred by
 *         adapter, could be different than mtu.
 *     capture_buf - The buffer to hold samples read from SCO socket.
 *     playback_buf - The buffer to hold samples about to write to SCO socket.
 *     msbc_read - mSBC codec to decode input audio in wideband speech mode.
 *     msbc_write - mSBC codec to encode output audio in wideband speech mode.
 *     msbc_num_out_frames - Number of total written mSBC frames.
 *     msbc_num_in_frames - Number of total read mSBC frames.
 *     read_cb - Callback to call when SCO socket can read.
 *     write_cb - Callback to call when SCO socket can write.
 *     read_buf - Buffer to hold input audio bytes.
 *     read_buf_level - Read input audio level in bytes.
 *     idev - The input iodev using this hfp_info.
 *     odev - The output iodev using this hfp_info.
 */
struct hfp_info {
	int fd;
	int started;
	unsigned int mtu;
	unsigned int packet_size;
	struct byte_buffer *capture_buf;
	struct byte_buffer *playback_buf;
	struct cras_audio_codec *msbc_read;
	struct cras_audio_codec *msbc_write;
	unsigned int msbc_num_out_frames;
	unsigned int msbc_num_in_frames;
	int (*read_cb)(struct hfp_info *info);
	int (*write_cb)(struct hfp_info *info);
	uint8_t write_buf[WRITE_BUF_SIZE_BYTES];
	uint8_t read_buf[READ_BUF_SIZE_BYTES];
	int read_buf_level;
	struct cras_iodev *idev;
	struct cras_iodev *odev;
};

int hfp_info_add_iodev(struct hfp_info *info, struct cras_iodev *dev)
{
	if (dev->direction == CRAS_STREAM_OUTPUT) {
		if (info->odev)
			goto invalid;
		info->odev = dev;

		buf_reset(info->playback_buf);
	} else if (dev->direction == CRAS_STREAM_INPUT) {
		if (info->idev)
			goto invalid;
		info->idev = dev;

		buf_reset(info->capture_buf);
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
	unsigned int buf_avail;
	format_bytes = cras_get_format_bytes(dev->format);

	*count *= format_bytes;

	if (dev->direction == CRAS_STREAM_OUTPUT)
		*buf = buf_write_pointer_size(info->playback_buf, &buf_avail);
	else
		*buf = buf_read_pointer_size(info->capture_buf, &buf_avail);

	if (*count > buf_avail)
		*count = buf_avail;
	*count /= format_bytes;
}

int hfp_buf_size(struct hfp_info *info, struct cras_iodev *dev)
{
	return info->playback_buf->used_size / cras_get_format_bytes(dev->format);
}

void hfp_buf_release(struct hfp_info *info, struct cras_iodev *dev,
		     unsigned written_frames)
{
	size_t format_bytes;
	format_bytes = cras_get_format_bytes(dev->format);

	written_frames *= format_bytes;

	if (dev->direction == CRAS_STREAM_OUTPUT)
		buf_increment_write(info->playback_buf, written_frames);
	else
		buf_increment_read(info->capture_buf, written_frames);
}

int hfp_buf_queued(struct hfp_info *info, const struct cras_iodev *dev)
{
	size_t format_bytes;
	format_bytes = cras_get_format_bytes(dev->format);

	if (dev->direction == CRAS_STREAM_OUTPUT)
		return buf_queued(info->playback_buf) / format_bytes;
	else
		return buf_queued(info->capture_buf) / format_bytes;
}

int hfp_fill_output_with_zeros(struct hfp_info *info,
			       struct cras_iodev *dev,
			       unsigned int nframes)
{
	unsigned int buf_avail;
	unsigned int format_bytes;
	unsigned int nbytes;
	uint8_t *buf;
	int i;
	int ret = 0;

	format_bytes = cras_get_format_bytes(dev->format);
	nbytes = nframes * format_bytes;
	/* Loop twice to make sure ring buffer is filled. */
	for (i = 0; i < 2; i++) {
		buf = buf_write_pointer_size(info->playback_buf, &buf_avail);
		if (buf_avail == 0)
			break;
		buf_avail = MIN(nbytes, buf_avail);
		memset(buf, 0, buf_avail);
		buf_increment_write(info->playback_buf, buf_avail);
		nbytes -= buf_avail;
		ret += buf_avail / format_bytes;
	}
	return ret;
}

int hfp_force_output_level(struct hfp_info *info,
			   struct cras_iodev *dev,
			   unsigned int level)
{
	level *= cras_get_format_bytes(dev->format);
	level = MIN(level, MAX_HFP_BUF_SIZE_BYTES);
	buf_adjust_readable(info->playback_buf, level);
	return 0;
}

int hfp_write_msbc(struct hfp_info *info)
{
	int to_write = 0;
	size_t encoded;
	int err;
	int pcm_encoded;
	unsigned int pcm_avail;
	uint8_t *samples;
	uint8_t *wp;

	samples = buf_read_pointer_size(info->playback_buf, &pcm_avail);
	wp = info->write_buf;
	if (pcm_avail >= MSBC_CODE_SIZE) {
		/* Encode more */
		wp[0] = H2_HEADER_0;
		wp[1] = h2_header_frames_count[info->msbc_num_out_frames % 4];
		pcm_encoded = info->msbc_write->encode(
				info->msbc_write, samples, pcm_avail,
				wp + MSBC_H2_HEADER_LEN,
				WRITE_BUF_SIZE_BYTES - MSBC_H2_HEADER_LEN,
				&encoded);
		buf_increment_read(info->playback_buf, pcm_encoded);
		pcm_avail -= pcm_encoded;
	} else {
		memset(wp, 0, WRITE_BUF_SIZE_BYTES);
	}
	to_write = info->packet_size;

msbc_send_again:
	err = send(info->fd, info->write_buf, to_write, 0);
	if (err < 0) {
		if (errno == EINTR)
			goto msbc_send_again;
		return err;
	}
	if (err != (int)info->packet_size) {
		syslog(LOG_ERR, "Partially write %d bytes for mSBC", err);
		return -1;
	}
	info->msbc_num_out_frames++;

	return err;
}

int hfp_write(struct hfp_info *info)
{
	int err = 0;
	unsigned to_send;
	uint8_t *samples;

	/* Write something */
	samples = buf_read_pointer_size(info->playback_buf, &to_send);
	if (to_send < info->packet_size)
		return 0;
	to_send = info->packet_size;

send_sample:
	err = send(info->fd, samples, to_send, 0);
	if (err < 0) {
		if (errno == EINTR)
			goto send_sample;

		return err;
	}

	if (err != (int)info->packet_size) {
		syslog(LOG_ERR,
		       "Partially write %d bytes for SCO packet size %u",
		       err, info->packet_size);
		return -1;
	}

	buf_increment_read(info->playback_buf, to_send);

	return err;
}

static int h2_header_get_seq(const uint8_t *p) {
	int i;
	for (i = 0; i < 4; i++) {
		if (*p == h2_header_frames_count[i])
			return i;
	}
	return -1;
}

/*
 * Extract mSBC frame from SCO socket input bytes, given that the mSBC frame
 * could be lost or corrupted.
 * Args:
 *    input - Pointer to input bytes read from SCO socket.
 *    len - Length of input bytes.
 *    seq - To be filled by the sequence number of mSBC packet.
 *    frame_head - To be filled by the starting position of mSBC frame if found.
 * Returns:
 *    The position of input bytes that has been read during the extraction.
 */
static int extract_msbc_frame(const uint8_t *input, int len,
			      int *seq, const uint8_t **frame_head)
{
	int rp = 0;
	while (len - rp >= MSBC_FRAME_SIZE) {
		if ((input[rp] != H2_HEADER_0) ||
		    (input[rp + 2] != MSBC_SYNC_WORD)) {
			rp++;
			continue;
		}
		*seq = h2_header_get_seq(input + rp + 1);
		if (*seq < 0) {
			rp++;
			continue;
		}
		*frame_head = input + rp;
		break;
	}
	return rp;
}

/*
 * Handle the case when mSBC frame is considered lost.
 * Args:
 *    msbc - The mSBC codec handles audio input.
 *    capture_buf - The buf to store decoded PCM audio.
 *    pcm_avail - Available room of capture_buf in bytes.
 */
static int handle_packet_loss(struct cras_audio_codec *msbc,
			      uint8_t *capture_buf, unsigned int pcm_avail)
{
	size_t pcm_decoded = 0;
	int decoded;

	/*
	 * Handle packet loss by feeding one zero input bit stream of mSBC.
	 * TODO(enshuo): add PLC.
	 */
	decoded = msbc->decode(
			msbc,
			msbc_zero_frame,
			MSBC_FRAME_LEN,
			capture_buf,
			pcm_avail,
			&pcm_decoded);
	if (decoded < 0)
		return decoded;

	return pcm_decoded;
}

int hfp_read_msbc(struct hfp_info *info)
{
	int err = 0;
	unsigned int pcm_avail = 0;
	int decoded;
	int rp = 0;
	size_t pcm_decoded = 0;
	uint8_t *capture_buf;
	const uint8_t *frame_head = NULL;
	int seq;

	/* Check if there's room for more PCM. */
	capture_buf = buf_write_pointer_size(info->capture_buf, &pcm_avail);
	if (pcm_avail < MSBC_CODE_SIZE)
		return 0;

recv_msbc_bytes:
	err = recv(info->fd, info->read_buf + info->read_buf_level,
		   info->packet_size, 0);
	if (err < 0) {
		syslog(LOG_ERR, "mSBC read err %s", strerror(errno));
		if (errno == EINTR)
			goto recv_msbc_bytes;
		return err;
	}
	/*
	 * Treat return code 0 (socket shutdown) as error here. BT stack
	 * shall send signal to main thread for device disconnection.
	 */
	if (err != (int)info->packet_size) {
		syslog(LOG_ERR, "Partially read %d bytes for mSBC packet", err);
		return -1;
	}
	info->read_buf_level += err;

	rp = extract_msbc_frame(info->read_buf, info->read_buf_level,
				&seq, &frame_head);
	if (!frame_head) {
		info->read_buf_level -= rp;
		memmove(info->read_buf, info->read_buf + rp,
			info->read_buf_level);
		return err;
	}

	/*
	 * Consider packet loss when found discontinuity in sequence number.
	 * TODO(hychao): Integrate with erroneous data reporting feature from
	 * controller that sets packet statue flag on HCI SCO packet.
	 */
	while (seq != (info->msbc_num_in_frames % 4)) {
		err = handle_packet_loss(info->msbc_read,
					 capture_buf,
					 pcm_avail);
		if (err < 0)
			return err;
		buf_increment_write(info->capture_buf, err);
		info->msbc_num_in_frames++;
	}

	decoded = info->msbc_read->decode(
			info->msbc_read,
			frame_head + MSBC_H2_HEADER_LEN,
			MSBC_FRAME_LEN,
			capture_buf,
			pcm_avail,
			&pcm_decoded);
	if (decoded < 0) {
		/*
		 * If mSBC frame cannot be decoded, consider this packet is
		 * corrupted and lost.
		 */
		err = handle_packet_loss(info->msbc_read,
					 capture_buf,
					 pcm_avail);
		if (err < 0)
			return err;
		pcm_decoded = err;
	}

	rp += MSBC_FRAME_SIZE;
	buf_increment_write(info->capture_buf, pcm_decoded);
	info->msbc_num_in_frames++;

	info->read_buf_level -= rp;
	memmove(info->read_buf, info->read_buf + rp, info->read_buf_level);

	return 0;
}

int hfp_read(struct hfp_info *info)
{
	int err = 0;
	unsigned to_read;
	uint8_t *capture_buf;

	capture_buf = buf_write_pointer_size(info->capture_buf, &to_read);

	if (to_read < info->packet_size)
		return 0;
	to_read = info->packet_size;

recv_sample:
	err = recv(info->fd, capture_buf, to_read, 0);
	if (err < 0) {
		syslog(LOG_ERR, "Read error %s", strerror(errno));
		if (errno == EINTR)
			goto recv_sample;

		return err;
	}

	if (err != (int)info->packet_size) {
		/* Allow the SCO packet size be modified from the default MTU
		 * value to the size of SCO data we first read. This is for
		 * some adapters who prefers a different value than MTU for
		 * transmitting SCO packet.
		 */
		if (err && (info->packet_size == info->mtu)) {
			info->packet_size = err;
		} else {
			syslog(LOG_ERR, "Partially read %d bytes for %u size SCO packet",
			       err, info->packet_size);
			return -1;
		}
	}

	buf_increment_write(info->capture_buf, err);

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

	if (!info->started)
		return 0;

	err = info->read_cb(info);
	if (err < 0) {
		syslog(LOG_ERR, "Read error");
		goto read_write_error;
	}

	/* Ignore the MTU bytes just read if input dev not in present */
	if (!info->idev)
		buf_increment_read(info->capture_buf, info->packet_size);

	if (info->odev) {
		err = info->write_cb(info);
		if (err < 0) {
			syslog(LOG_ERR, "Write error");
			goto read_write_error;
		}
	}

	return 0;

read_write_error:
	/*
	 * This callback is executing in audio thread, so it's safe to
	 * unregister itself by audio_thread_rm_callback().
	 */
	audio_thread_rm_callback(info->fd);
	close(info->fd);
	info->fd = 0;
	info->started = 0;

	return 0;
}

struct hfp_info *hfp_info_create(int codec)
{
	struct hfp_info *info;
	info = (struct hfp_info *)calloc(1, sizeof(*info));
	if (!info)
		goto error;

	info->capture_buf = byte_buffer_create(MAX_HFP_BUF_SIZE_BYTES);
	if (!info->capture_buf)
		goto error;

	info->playback_buf = byte_buffer_create(MAX_HFP_BUF_SIZE_BYTES);
	if (!info->playback_buf)
		goto error;

	if (codec == HFP_CODEC_ID_MSBC) {
		info->write_cb = hfp_write_msbc;
		info->read_cb = hfp_read_msbc;
		info->msbc_read = cras_msbc_codec_create();
		info->msbc_write = cras_msbc_codec_create();
	} else {
		info->write_cb = hfp_write;
		info->read_cb = hfp_read;
	}

	return info;

error:
	if (info) {
		if (info->capture_buf)
			byte_buffer_destroy(&info->capture_buf);
		if (info->playback_buf)
			byte_buffer_destroy(&info->playback_buf);
		free(info);
	}
	return NULL;
}

int hfp_info_running(struct hfp_info *info)
{
	return info->started;
}

int hfp_info_start(int fd, unsigned int mtu, struct hfp_info *info)
{
	info->fd = fd;
	info->mtu = mtu;

	/* Initialize to MTU, it may change when actually read the socket. */
	info->packet_size = mtu;
	buf_reset(info->playback_buf);
	buf_reset(info->capture_buf);

	audio_thread_add_callback(info->fd, hfp_info_callback, info);

	info->read_buf_level = 0;
	info->started = 1;
	info->msbc_num_out_frames = 0;
	info->msbc_num_in_frames = 0;

	return 0;
}

int hfp_info_stop(struct hfp_info *info)
{
	if (!info->started)
		return 0;

	audio_thread_rm_callback_sync(
		cras_iodev_list_get_audio_thread(),
		info->fd);

	close(info->fd);
	info->fd = 0;
	info->started = 0;

	return 0;
}

void hfp_info_destroy(struct hfp_info *info)
{
	if (info->capture_buf)
		byte_buffer_destroy(&info->capture_buf);

	if (info->playback_buf)
		byte_buffer_destroy(&info->playback_buf);

	if (info->msbc_read)
		cras_sbc_codec_destroy(info->msbc_read);
	if (info->msbc_write)
		cras_sbc_codec_destroy(info->msbc_write);

	free(info);
}
