/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_rstream.h"

#ifndef CRAS_SRC_SERVER_CRAS_RTC_H_
#define CRAS_SRC_SERVER_CRAS_RTC_H_

/*
 * Returns true if the stream is possibly a RTC stream.
 * true indicates it may be a RTC stream.
 * false indicates it's definitely not a RTC stream.
 */
bool cras_rtc_check_stream_config(const struct cras_rstream* stream);

/*
 * Adds a stream into the RTC detector. This function will detect whether
 * there are RTC streams.
 * Args:
 * 	stream - A stream added.
 * 	dev_ptr - A cras_iodev instance which the stream is attached to.
 */
void cras_rtc_add_stream(struct cras_rstream* stream, struct cras_iodev* iodev);

/*
 * Removes a stream from the RTC detector.
 * Args:
 * 	stream - A stream removed.
 * 	dev_id - A device id which the stream is attached to.
 */
void cras_rtc_remove_stream(struct cras_rstream* stream, unsigned int dev_id);

// Returns whether there are running RTC streams.
bool cras_rtc_is_running();

#endif
