/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_metrics.h"

#include <syslog.h>
#include <unistd.h>

struct usb_device {
  int vendor_id;
  int product_id;
};

struct usb_device popular_usb_devices[] = {
    {.vendor_id = 0x582, .product_id = 0x159},
    {.vendor_id = 0xc053, .product_id = 0x047f},
    {.vendor_id = 0xc056, .product_id = 0x047f},
    {.vendor_id = 0x0a8f, .product_id = 0x046d},
    {.vendor_id = 0x0300, .product_id = 0x0b0e},
    {.vendor_id = 0x0014, .product_id = 0x0d8c},
    {.vendor_id = 0x2008, .product_id = 0x1b3f},
    {.vendor_id = 0xc053, .product_id = 0x047f},
    {.vendor_id = 0x2319, .product_id = 0x0b0e},
    {.vendor_id = 0x0a38, .product_id = 0x046d},
    {.vendor_id = 0x0306, .product_id = 0x0b0e},
    {.vendor_id = 0x0305, .product_id = 0x0b0e},
    {.vendor_id = 0xc056, .product_id = 0x047f},
    {.vendor_id = 0x0300, .product_id = 0x0b0e},
    {.vendor_id = 0x5033, .product_id = 0x18d1},
    {.vendor_id = 0x0a8f, .product_id = 0x046d},
    {.vendor_id = 0xc058, .product_id = 0x047f},
    {.vendor_id = 0x2008, .product_id = 0x1b3f},
    {.vendor_id = 0x0a38, .product_id = 0x046d},
    {.vendor_id = 0x2319, .product_id = 0x0b0e},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x0014, .product_id = 0x0d8c},
    {.vendor_id = 0xc055, .product_id = 0x047f},
    {.vendor_id = 0xa051, .product_id = 0x04e8},
    {.vendor_id = 0x402e, .product_id = 0x0bda},
    {.vendor_id = 0x0a6b, .product_id = 0x046d},
    {.vendor_id = 0x161f, .product_id = 0x0c76},
    {.vendor_id = 0x0305, .product_id = 0x0b0e},
    {.vendor_id = 0x0269, .product_id = 0x03f0},
    {.vendor_id = 0x245d, .product_id = 0x0b0e},
    {.vendor_id = 0x0e40, .product_id = 0x0b0e},
    {.vendor_id = 0x02ee, .product_id = 0x047f},
    {.vendor_id = 0x0e41, .product_id = 0x0b0e},
    {.vendor_id = 0x030c, .product_id = 0x0b0e},
    {.vendor_id = 0xc058, .product_id = 0x047f},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x5033, .product_id = 0x18d1},
    {.vendor_id = 0x0420, .product_id = 0x0b0e},
    {.vendor_id = 0x0a44, .product_id = 0x046d},
    {.vendor_id = 0x4014, .product_id = 0x0bda},
    {.vendor_id = 0x030c, .product_id = 0x0b0e},
    {.vendor_id = 0x056b, .product_id = 0x03f0},
    {.vendor_id = 0x2008, .product_id = 0x1b3f},
    {.vendor_id = 0x0422, .product_id = 0x0b0e},
    {.vendor_id = 0x013c, .product_id = 0x0d8c},
    {.vendor_id = 0x0a37, .product_id = 0x046d},
    {.vendor_id = 0x0033, .product_id = 0x1395},
    {.vendor_id = 0x245e, .product_id = 0x0b0e},
    {.vendor_id = 0x0127, .product_id = 0x047f},
    {.vendor_id = 0xa051, .product_id = 0x04e8},
    {.vendor_id = 0x0306, .product_id = 0x0b0e},
    {.vendor_id = 0x0ab7, .product_id = 0x046d},
    {.vendor_id = 0x0a6b, .product_id = 0x046d},
    {.vendor_id = 0x0412, .product_id = 0x0b0e},
    {.vendor_id = 0xa503, .product_id = 0x413c},
    {.vendor_id = 0xc055, .product_id = 0x047f},
    {.vendor_id = 0x8001, .product_id = 0x18d1},
    {.vendor_id = 0x40fe, .product_id = 0x05a7},
    {.vendor_id = 0x0300, .product_id = 0x0b0e},
    {.vendor_id = 0x0025, .product_id = 0x1395},
    {.vendor_id = 0xc054, .product_id = 0x047f},
    {.vendor_id = 0x0422, .product_id = 0x0b0e},
    {.vendor_id = 0x9e84, .product_id = 0xb58e},
    {.vendor_id = 0x245d, .product_id = 0x0b0e},
    {.vendor_id = 0xa396, .product_id = 0x17ef},
    {.vendor_id = 0x0e30, .product_id = 0x0b0e},
    {.vendor_id = 0x0005, .product_id = 0x0d8c},
    {.vendor_id = 0x161e, .product_id = 0x0c76},
    {.vendor_id = 0x02ee, .product_id = 0x047f},
    {.vendor_id = 0x0410, .product_id = 0x0b0e},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x24c8, .product_id = 0x0b0e},
    {.vendor_id = 0x16a4, .product_id = 0x951},
    {.vendor_id = 0x0269, .product_id = 0x03f0},
    {.vendor_id = 0x0a44, .product_id = 0x046d},
    {.vendor_id = 0x153f, .product_id = 0x0c76},
    {.vendor_id = 0x0e41, .product_id = 0x0b0e},
    {.vendor_id = 0x4837, .product_id = 0x0bda},
    {.vendor_id = 0x0420, .product_id = 0x0b0e},
    {.vendor_id = 0x24c7, .product_id = 0x0b0e},
    {.vendor_id = 0x013c, .product_id = 0x0d8c},
    {.vendor_id = 0x0e40, .product_id = 0x0b0e},
    {.vendor_id = 0x030c, .product_id = 0x0b0e},
    {.vendor_id = 0x2475, .product_id = 0x0b0e},
    {.vendor_id = 0x3063, .product_id = 0x17ef},
    {.vendor_id = 0x48f0, .product_id = 0x0bda},
    {.vendor_id = 0x0005, .product_id = 0xb58e},
    {.vendor_id = 0x2912, .product_id = 0x08bb},
    {.vendor_id = 0x0a37, .product_id = 0x046d},
    {.vendor_id = 0xac01, .product_id = 0x047f},
    {.vendor_id = 0x3083, .product_id = 0x17ef},
    {.vendor_id = 0x8001, .product_id = 0x18d1},
    {.vendor_id = 0x0300, .product_id = 0x0b0e},
    {.vendor_id = 0x245e, .product_id = 0x0b0e},
    {.vendor_id = 0x0033, .product_id = 0x1395},
    {.vendor_id = 0x0412, .product_id = 0x0b0e},
    {.vendor_id = 0xa503, .product_id = 0x413c},
    {.vendor_id = 0x2476, .product_id = 0x0b0e},
    {.vendor_id = 0xc056, .product_id = 0x047f},
    {.vendor_id = 0x0ab7, .product_id = 0x046d},
    {.vendor_id = 0x0aba, .product_id = 0x046d},
    {.vendor_id = 0x02e6, .product_id = 0x047f},
    {.vendor_id = 0x30b0, .product_id = 0x17ef},
    {.vendor_id = 0x40fe, .product_id = 0x05a7},
    {.vendor_id = 0x0127, .product_id = 0x047f},
    {.vendor_id = 0x0025, .product_id = 0x1395},
    {.vendor_id = 0x30bb, .product_id = 0x17ef},
    {.vendor_id = 0x005f, .product_id = 0x909},
    {.vendor_id = 0x9e84, .product_id = 0xb58e},
    {.vendor_id = 0x016c, .product_id = 0x0d8c},
    {.vendor_id = 0x0005, .product_id = 0x0d8c},
    {.vendor_id = 0x0011, .product_id = 0x31b2},
    {.vendor_id = 0x2008, .product_id = 0x1b3f},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0xc054, .product_id = 0x047f},
    {.vendor_id = 0x24c9, .product_id = 0x0b0e},
    {.vendor_id = 0x161f, .product_id = 0x0c76},
    {.vendor_id = 0x24b1, .product_id = 0x0b0e},
    {.vendor_id = 0x612e, .product_id = 0x17ef},
    {.vendor_id = 0x0e30, .product_id = 0x0b0e},
    {.vendor_id = 0x2453, .product_id = 0x0b0e},
    {.vendor_id = 0x4007, .product_id = 0x0a12},
    {.vendor_id = 0x0021, .product_id = 0x0d8c},
    {.vendor_id = 0x0aaf, .product_id = 0x046d},
    {.vendor_id = 0x0a4f, .product_id = 0x1b1c},
    {.vendor_id = 0xc035, .product_id = 0x047f},
    {.vendor_id = 0x4bb7, .product_id = 0x0bda},
    {.vendor_id = 0x48f0, .product_id = 0x0bda},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x002d, .product_id = 0x1395},
    {.vendor_id = 0x161e, .product_id = 0x0c76},
    {.vendor_id = 0x879d, .product_id = 0x03f0},
    {.vendor_id = 0x0567, .product_id = 0x03f0},
    {.vendor_id = 0x0410, .product_id = 0x0b0e},
    {.vendor_id = 0x1012, .product_id = 0x14ed},
    {.vendor_id = 0x402e, .product_id = 0x0bda},
    {.vendor_id = 0x2008, .product_id = 0x1b3f},
    {.vendor_id = 0x4938, .product_id = 0x0bda},
    {.vendor_id = 0x16a4, .product_id = 0x951},
    {.vendor_id = 0x005f, .product_id = 0x909},
    {.vendor_id = 0x0a66, .product_id = 0x046d},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x0005, .product_id = 0xb58e},
    {.vendor_id = 0x2475, .product_id = 0x0b0e},
    {.vendor_id = 0x153f, .product_id = 0x0c76},
    {.vendor_id = 0xc056, .product_id = 0x047f},
    {.vendor_id = 0xac01, .product_id = 0x047f},
    {.vendor_id = 0x2008, .product_id = 0x1b3f},
    {.vendor_id = 0x030c, .product_id = 0x0b0e},
    {.vendor_id = 0x2912, .product_id = 0x08bb},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x49c6, .product_id = 0x0bda},
    {.vendor_id = 0x49fa, .product_id = 0x0bda},
    {.vendor_id = 0x0011, .product_id = 0x31b2},
    {.vendor_id = 0xa310, .product_id = 0x05a7},
    {.vendor_id = 0x0aba, .product_id = 0x046d},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x4014, .product_id = 0x0bda},
    {.vendor_id = 0x0ab1, .product_id = 0x046d},
    {.vendor_id = 0x2476, .product_id = 0x0b0e},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x0aaf, .product_id = 0x046d},
    {.vendor_id = 0x005f, .product_id = 0x909},
    {.vendor_id = 0x0011, .product_id = 0x31b2},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x2453, .product_id = 0x0b0e},
    {.vendor_id = 0x056b, .product_id = 0x03f0},
    {.vendor_id = 0x2008, .product_id = 0x1b3f},
    {.vendor_id = 0x85dd, .product_id = 0x152a},
    {.vendor_id = 0x340b, .product_id = 0x040d},
    {.vendor_id = 0x0011, .product_id = 0x31b2},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x0567, .product_id = 0x03f0},
    {.vendor_id = 0x4042, .product_id = 0x2188},
    {.vendor_id = 0x24c7, .product_id = 0x0b0e},
    {.vendor_id = 0x4837, .product_id = 0x0bda},
    {.vendor_id = 0x0011, .product_id = 0x31b2},
    {.vendor_id = 0x016c, .product_id = 0x0d8c},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x2008, .product_id = 0x1b3f},
    {.vendor_id = 0x0a66, .product_id = 0x046d},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x0a4f, .product_id = 0x1b1c},
    {.vendor_id = 0x2008, .product_id = 0x1b3f},
    {.vendor_id = 0xa396, .product_id = 0x17ef},
    {.vendor_id = 0x24b1, .product_id = 0x0b0e},
    {.vendor_id = 0x4938, .product_id = 0x0bda},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x1012, .product_id = 0x14ed},
    {.vendor_id = 0x612e, .product_id = 0x17ef},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x005f, .product_id = 0x909},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x3083, .product_id = 0x17ef},
    {.vendor_id = 0x016c, .product_id = 0x0d8c},
    {.vendor_id = 0x0ab1, .product_id = 0x046d},
    {.vendor_id = 0x4938, .product_id = 0x0bda},
    {.vendor_id = 0x879d, .product_id = 0x03f0},
    {.vendor_id = 0x513b, .product_id = 0x07ca},
    {.vendor_id = 0x0012, .product_id = 0x0d8c},
    {.vendor_id = 0x4042, .product_id = 0x2188},
    {.vendor_id = 0x161f, .product_id = 0x0c76},
    {.vendor_id = 0x48f0, .product_id = 0x0bda},
    {.vendor_id = 0xc035, .product_id = 0x047f},
    {.vendor_id = 0xc056, .product_id = 0x047f},
    {.vendor_id = -1, .product_id = 0},
};

bool in_popular_usb_devices(int vid, int pid) {
  int i = 0;
  for (i = 0; popular_usb_devices[i].vendor_id != -1; i++) {
    if (popular_usb_devices[i].vendor_id == vid &&
        popular_usb_devices[i].product_id == pid) {
      return true;
    }
  }
  return false;
}

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
  if (in_popular_usb_devices(vendor_id, product_id)) {
    syslog(LOG_DEBUG,
           "AudioPeripheralClose vid: %x, pid: %x, type: %d, run_time: %d, "
           "rate: %d, channel: %d, format:%d",
           vendor_id, product_id, type, run_time, rate, channel, format);
    AudioPeripheralClose(vendor_id, product_id, type, run_time, rate, channel,
                         format);
  }
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
