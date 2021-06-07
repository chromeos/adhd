/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras_rstream.h"
#include "cras_iodev.h"

/*
 * Add a stream into the RTC detector. This function will detect whether
 * there are RTC streams.
 * Args:
 * 	stream - A stream added.
 * 	dev_ptr - A cras_iodev instance which the stream is attached to.
 */
void cras_rtc_add_stream(struct cras_rstream *stream, struct cras_iodev *iodev);

/*
 * Remove a stream from the RTC detector.
 * Args:
 * 	stream - A stream removed.
 * 	dev_id - A device id which the stream is attached to.
 */
void cras_rtc_remove_stream(struct cras_rstream *stream, unsigned int dev_id);
