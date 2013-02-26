/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_A2DP_INFO_H_
#define CRAS_A2DP_INFO_H_

#include "a2dp-codecs.h"

#define A2DP_BUF_SIZE_BYTES 1024

/* Represents the codec and encoded state of a2dp iodev.
 * Members:
 *    codec - The codec used to encode PCM buffer to a2dp buffer.
 *    a2dp_buf - The buffer to hold encoded frames.
 *    codesize - Size of a SBC frame in bytes.
 *    frame_length - Size of an encoded SBC frame in bytes.
 *    frame_count - Queued SBC frame count currently in a2dp buffer.
 *    seq_num - Sequence number in rtp header.
 *    samples - Queued PCM frame count currently in a2dp buffer.
 *    nsamples - Cumulative number of encoded PCM frames.
 *    a2dp_buf_used - Used a2dp buffer counter in bytes.
 */
struct a2dp_info {
	struct cras_audio_codec *codec;
	uint8_t a2dp_buf[A2DP_BUF_SIZE_BYTES];
	int codesize;
	int frame_length;
	int frame_count;
	uint16_t seq_num;
	int samples;
	int nsamples;
	size_t a2dp_buf_used;
};

/*
 * Set up codec for given sbc capability.
 */
int init_a2dp(struct a2dp_info *a2dp, a2dp_sbc_t *sbc);

/*
 * Destroys an a2dp_info.
 */
void destroy_a2dp(struct a2dp_info *a2dp);

/*
 * Gets original size of a2dp encoded bytes.
 */
int a2dp_block_size(struct a2dp_info *a2dp, int encoded_bytes);

/*
 * Gets the number of queued frames in a2dp_info.
 */
int a2dp_queued_frames(struct a2dp_info *a2dp);

/*
 * Drains queued samples in a2dp_info.
 */
void a2dp_drain(struct a2dp_info *a2dp);

#endif /* CRAS_A2DP_INFO_H_ */
