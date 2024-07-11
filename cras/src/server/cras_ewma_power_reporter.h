// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// cras_ewma_power_report reports ewma power of the heuristically
// selected input stream. The configuration is stored as a static variable in
// the cras_ewma_power_report.c file. The process is as follows:

// 1. Audio thread captures the data.
// 2. If the stream matches the criteria, measures the ewma and aggregate the
//    value temporarily.
// 3. After some times, audio thread sends the aggregated value to
//    the main thread.
// 4. Main thread receives the message, then sends a D-Bus signal.
// 5. The signal will be captured by chrome to be shown in the vc panel.

#ifndef CRAS_SRC_SERVER_CRAS_EWMA_POWER_REPORTER_H_
#define CRAS_SRC_SERVER_CRAS_EWMA_POWER_REPORTER_H_

#include <sys/time.h>

#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/ewma_power.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the power reporter.
// Must be called from the main thread.
void cras_ewma_power_reporter_init();

// Enable or disable the power reporter.
// Should be called by the main thread.
void cras_ewma_power_reporter_set_enabled(bool enabled);

// Update the target of the input stream that will be measured.
// Should be called by the main thread.
void cras_ewma_power_reporter_set_target(uint32_t stream_id);

// Callback to update the target of the input stream that will be measured.
// Should be called by the main thread.
void cras_ewma_power_reporter_streams_changed(struct cras_rstream* all_streams);

// Check whether the stream matches the criteria.
// Should be called by the audio thread.
bool cras_ewma_power_reporter_should_calculate(const uint32_t stream_id);

// Aggregate the ewma power temporarily, and sends it to the main thread
// after some times.
// Should be called by the audio thread.
int cras_ewma_power_reporter_report(const uint32_t stream_id,
                                    const struct ewma_power* ewma);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_EWMA_POWER_REPORTER_H_
