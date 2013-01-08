/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_A2DP_INFO_H_
#define CRAS_A2DP_INFO_H_

#include "a2dp-codecs.h"
#include "cras_audio_codec.h"

/*
 * Set up codec for given sbc capability.
 */
void init_a2dp(struct cras_audio_codec *codec, a2dp_sbc_t *sbc);

void destroy_a2dp(struct cras_audio_codec *codec);

#endif /* CRAS_A2DP_INFO_H_ */
