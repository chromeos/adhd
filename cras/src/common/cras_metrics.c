/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_metrics.h"

#include <syslog.h>
#include <unistd.h>

#if HAVE_LIB_METRICS
#include <metrics/c_metrics_library.h>

#include "metrics/c_structured_metrics.h"

void cras_metrics_log_event(const char* event) {
  CMetricsLibrary handle;

  syslog(LOG_DEBUG, "UMA event: %s", event);
  handle = CMetricsLibraryNew();
  CMetricsLibrarySendCrosEventToUMA(handle, event);
  CMetricsLibraryDelete(handle);
}

void cras_metrics_log_histogram(const char* name,
                                int sample,
                                int min,
                                int max,
                                int nbuckets) {
  CMetricsLibrary handle;

  syslog(LOG_DEBUG, "UMA name: %s", name);
  handle = CMetricsLibraryNew();
  CMetricsLibrarySendToUMA(handle, name, sample, min, max, nbuckets);
  CMetricsLibraryDelete(handle);
}

void cras_metrics_log_sparse_histogram(const char* name, int sample) {
  CMetricsLibrary handle;

  syslog(LOG_DEBUG, "UMA name: %s", name);
  handle = CMetricsLibraryNew();
  CMetricsLibrarySendSparseToUMA(handle, name, sample);
  CMetricsLibraryDelete(handle);
}

void audio_peripheral_info(int vendor_id, int product_id, int type) {
  syslog(LOG_DEBUG, "AudioPeripheralInfo vid: %x, pid: %x, type: %d", vendor_id,
         product_id, type);
  AudioPeripheralInfo(vendor_id, product_id, type);
}

void audio_peripheral_close(int vendor_id,
                            int product_id,
                            int type,
                            int run_time,
                            int rate,
                            int channel,
                            int format) {
  syslog(LOG_DEBUG, "AudioPeripheralClose vid: %x, pid: %x, type: %d",
         vendor_id, product_id, type);
  AudioPeripheralClose(vendor_id, product_id, type, run_time, rate, channel,
                       format);
}

#else
void cras_metrics_log_event(const char* event) {}
void cras_metrics_log_histogram(const char* name,
                                int sample,
                                int min,
                                int max,
                                int nbuckets) {}
void cras_metrics_log_enum_histogram(const char* name, int sample, int max) {}
void cras_metrics_log_sparse_histogram(const char* name, int sample) {}

void audio_peripheral_info(int vendor_id, int product_id, int type) {}
void audio_peripheral_close(int vendor_id,
                            int product_id,
                            int type,
                            int run_time,
                            int rate,
                            int channel,
                            int format) {}

#endif
