/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sbc/sbc.h>
#include <syslog.h>

#include "cras_a2dp_info.h"
#include "cras_sbc_codec.h"
#include "rtp.h"

int init_a2dp(struct a2dp_info *a2dp, a2dp_sbc_t *sbc)
{
	uint8_t frequency = 0, mode = 0, subbands = 0, allocation, blocks = 0,
		bitpool;

	if (sbc->frequency & SBC_SAMPLING_FREQ_48000)
		frequency = SBC_FREQ_48000;
	else if (sbc->frequency & SBC_SAMPLING_FREQ_44100)
		frequency = SBC_FREQ_44100;
	else if (sbc->frequency & SBC_SAMPLING_FREQ_32000)
		frequency = SBC_FREQ_32000;
	else if (sbc->frequency & SBC_SAMPLING_FREQ_16000)
		frequency = SBC_FREQ_16000;

	if (sbc->channel_mode & SBC_CHANNEL_MODE_JOINT_STEREO)
		mode = SBC_MODE_JOINT_STEREO;
	else if (sbc->channel_mode & SBC_CHANNEL_MODE_STEREO)
		mode = SBC_MODE_STEREO;
	else if (sbc->channel_mode & SBC_CHANNEL_MODE_DUAL_CHANNEL)
		mode = SBC_MODE_DUAL_CHANNEL;
	else if (sbc->channel_mode & SBC_CHANNEL_MODE_MONO)
		mode = SBC_MODE_MONO;

	if (sbc->allocation_method & SBC_ALLOCATION_LOUDNESS)
		allocation = SBC_AM_LOUDNESS;
	else
		allocation = SBC_AM_SNR;

	switch (sbc->subbands) {
	case SBC_SUBBANDS_4:
		subbands = SBC_SB_4;
		break;
	case SBC_SUBBANDS_8:
		subbands = SBC_SB_8;
		break;
	}

	switch (sbc->block_length) {
	case SBC_BLOCK_LENGTH_4:
		blocks = SBC_BLK_4;
		break;
	case SBC_BLOCK_LENGTH_8:
		blocks = SBC_BLK_8;
		break;
	case SBC_BLOCK_LENGTH_12:
		blocks = SBC_BLK_12;
		break;
	case SBC_BLOCK_LENGTH_16:
		blocks = SBC_BLK_16;
		break;
	}

	bitpool = sbc->max_bitpool;

	a2dp->codec = cras_sbc_codec_create(frequency, mode, subbands,
					    allocation, blocks, bitpool);
	if (!a2dp->codec)
		return -1;

	a2dp->a2dp_buf_used = sizeof(struct rtp_header)
			+ sizeof(struct rtp_payload);
	a2dp->frame_count = 0;
	a2dp->seq_num = 0;
	a2dp->samples = 0;

	return 0;
}

void destroy_a2dp(struct a2dp_info *a2dp)
{
	cras_sbc_codec_destroy(a2dp->codec);
}

int a2dp_queued_frames(struct a2dp_info *a2dp)
{
	return a2dp->samples;
}

void a2dp_drain(struct a2dp_info *a2dp)
{
	a2dp->a2dp_buf_used = sizeof(struct rtp_header)
			+ sizeof(struct rtp_payload);
	a2dp->samples = 0;
	a2dp->seq_num = 0;
	a2dp->frame_count = 0;
}
