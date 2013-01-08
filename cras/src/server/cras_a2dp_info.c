/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sbc/sbc.h>
#include <syslog.h>

#include "cras_a2dp_info.h"
#include "cras_sbc_codec.h"

void init_a2dp(struct cras_audio_codec *codec, a2dp_sbc_t *sbc)
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

	codec = cras_sbc_codec_create(frequency, mode, subbands, allocation,
				      blocks, bitpool);
}

void destroy_a2dp(struct cras_audio_codec *codec)
{
	cras_sbc_codec_destroy(codec);
}
