/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_METRICS_H_
#define CRAS_SRC_COMMON_CRAS_METRICS_H_
#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Logs the specified event.
void cras_metrics_log_event(const char* event);

// Sends histogram data.
void cras_metrics_log_histogram(const char* name,
                                int sample,
                                int min,
                                int max,
                                int nbuckets);

// Sends sparse histogram data.
void cras_metrics_log_sparse_histogram(const char* name, int sample);

void audio_peripheral_info(int vendor_id, int product_id, int type);

void audio_peripheral_close(int vendor_id,
                            int product_id,
                            int type,
                            int run_time,
                            int rate,
                            int channel,
                            int format);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_METRICS_H_
