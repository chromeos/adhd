/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_A2DP_INFO_H_
#define CRAS_SRC_SERVER_CRAS_A2DP_INFO_H_

#include <stddef.h>
#include <stdint.h>

#include "third_party/bluez/a2dp-codecs.h"

#define A2DP_BUF_SIZE_BYTES 2048

// Represents the codec and encoded state of a2dp iodev.
struct a2dp_info {
  // The codec used to encode PCM buffer to a2dp buffer.
  struct cras_audio_codec* codec;
  // The buffer to hold encoded frames.
  uint8_t a2dp_buf[A2DP_BUF_SIZE_BYTES];
  // Size of a SBC frame in bytes.
  int codesize;
  // Size of an encoded SBC frame in bytes.
  int frame_length;
  // Queued SBC frame count currently in a2dp buffer.
  int frame_count;
  // Sequence number in rtp header.
  uint16_t seq_num;
  // Queued PCM frame count currently in a2dp buffer.
  int samples;
  // Cumulative number of encoded PCM frames.
  int nsamples;
  // Used a2dp buffer counter in bytes.
  size_t a2dp_buf_used;
};

/*
 * Set up codec for given sbc capability.
 */
int init_a2dp(struct a2dp_info* a2dp, a2dp_sbc_t* sbc);

/*
 * Destroys an a2dp_info.
 */
void destroy_a2dp(struct a2dp_info* a2dp);

/*
 * Gets the codesize of the SBC codec.
 */
int a2dp_codesize(struct a2dp_info* a2dp);

/*
 * Gets original size of a2dp encoded bytes.
 */
int a2dp_block_size(struct a2dp_info* a2dp, int encoded_bytes);

/*
 * Gets the number of queued frames in a2dp_info.
 */
int a2dp_queued_frames(const struct a2dp_info* a2dp);

/*
 * Empty all queued samples in a2dp_info.
 */
void a2dp_reset(struct a2dp_info* a2dp);

/*
 * Encodes samples using the codec for this a2dp instance, returns the number of
 * pcm bytes processed.
 * Args:
 *    a2dp: The a2dp info object.
 *    pcm_buf: The buffer of pcm samples.
 *    pcm_buf_size: Size of the pcm buffer.
 *    format_bytes: Number of bytes per sample.
 *    link_mtu: The maximum transmit unit.
 */
int a2dp_encode(struct a2dp_info* a2dp,
                const void* pcm_buf,
                int pcm_buf_size,
                int format_bytes,
                size_t link_mtu);

/*
 * Writes samples using a2dp, returns number of frames written.
 * Args:
 *    a2dp: The a2dp info object.
 *    stream_fd: The file descriptor to send stream to.
 *    link_mtu: The maximum transmit unit.
 */
int a2dp_write(struct a2dp_info* a2dp, int stream_fd, size_t link_mtu);

#endif  // CRAS_SRC_SERVER_CRAS_A2DP_INFO_H_
