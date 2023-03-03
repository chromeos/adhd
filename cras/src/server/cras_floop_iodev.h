/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_FLOOP_IODEV_H_
#define CRAS_SRC_SERVER_CRAS_FLOOP_IODEV_H_

#include "cras/src/server/cras_iodev.h"
#include "cras_types.h"

/*
 * A pair of felxible loopback iodevs that can dynamically route
 * playback streams to capture streams.
 *
 * Audio samples sent to the output iodev (CRAS_STREAM_OUTPUT)
 * will become available in the input iodev (CRAS_STREAM_INPUT):
 * playback stream -> odev -> buffer -> idev -> capture stream
 *                    ^^^^^^^^^^^^^^^^^^^^^^
 *                    this is the loopback device
 */
struct cras_floop_pair {
  struct cras_iodev input;
  struct cras_iodev output;

  // for ulist.h
  struct cras_floop_pair *prev, *next;
};

/*
 * Create a pair of flexible loopback devices.
 * Samples written to the output iodev can be read from the input iodev.
 *
 * Called when a client requests it.
 */
struct cras_floop_pair* cras_floop_pair_create(
    const struct cras_floop_params* params);

/*
 * Remove it from cras_iodev_list and frees the cras_floop_pair.
 *
 * TODO(b/214165288): Call it when a floop device is unused for a while.
 */
void cras_floop_pair_destroy(struct cras_floop_pair* loopdev);

/*
 * Tells whether the given stream should be attached to the floop pair
 *
 * Called when:
 * 1. An output stream is added to check if it should be attached to the floop
 * 2. The floop is activated (the first input stream for the floop starts),
 *    to attach existing output streams to the floop.
 */
bool cras_floop_pair_match_output_stream(const struct cras_floop_pair* pair,
                                         const struct cras_rstream* stream);

// Tells whether the floop pair matches the params
bool cras_floop_pair_match_params(const struct cras_floop_pair* pair,
                                  const struct cras_floop_params* params);

#endif  // CRAS_SRC_SERVER_CRAS_FLOOP_IODEV_H_
