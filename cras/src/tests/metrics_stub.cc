/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_server_metrics.h"

extern "C" {

int cras_server_metrics_ap_nc_start_status(bool success) {
  return 0;
}

int cras_server_metrics_ap_nc_runtime(unsigned runtime_second) {
  return 0;
}

int cras_server_metrics_ast_start_status(bool success) {
  return 0;
}

int cras_server_metrics_ast_runtime(unsigned runtime_second) {
  return 0;
}

int cras_server_metrics_device_runtime(struct cras_iodev* iodev) {
  return 0;
}

int cras_server_metrics_device_volume(struct cras_iodev* iodev) {
  return 0;
}

int cras_server_metrics_device_sample_format(struct cras_iodev* iodev) {
  return 0;
}

int cras_server_metrics_device_sample_rate(struct cras_iodev* iodev) {
  return 0;
}

int cras_server_metrics_device_samples_dropped(struct cras_iodev* iodev) {
  return 0;
}

int cras_server_metrics_device_configure_time(struct cras_iodev* iodev,
                                              struct timespec* beg,
                                              struct timespec* end) {
  return 0;
}

int cras_server_metrics_highest_device_delay(
    unsigned int hw_level,
    unsigned int largest_cb_level,
    enum CRAS_STREAM_DIRECTION direction) {
  return 0;
}

int cras_server_metrics_highest_hw_level(unsigned hw_level,
                                         enum CRAS_STREAM_DIRECTION direction) {
  return 0;
}

int cras_server_metrics_longest_fetch_delay(unsigned delay_msec) {
  return 0;
}

int cras_server_metrics_missed_cb_event(struct cras_rstream* stream) {
  return 0;
}

int cras_server_metrics_num_underruns(struct cras_iodev* iodev) {
  return 0;
}

int cras_server_metrics_num_underruns_during_apnc(struct cras_iodev* iodev) {
  return 0;
}

int cras_server_metrics_hfp_battery_indicator(int battery_indicator_support) {
  return 0;
}

int cras_server_metrics_hfp_battery_report(int battery_report) {
  return 0;
}

int cras_server_metrics_hfp_wideband_support(bool supported) {
  return 0;
}

int cras_server_metrics_hfp_packet_loss(float packet_loss_ratio) {
  return 0;
}

int cras_server_metrics_hfp_sco_connection_error(
    enum CRAS_METRICS_BT_SCO_ERROR_TYPE type) {
  return 0;
}

int cras_server_metrics_busyloop(struct timespec* ts, unsigned count) {
  return 0;
}

int cras_server_metrics_busyloop_length(unsigned count) {
  return 0;
}

int cras_server_metrics_device_open_status(struct cras_iodev* iodev,
                                           enum CRAS_DEVICE_OPEN_STATUS code,
                                           bool group_active) {
  return 0;
}

int cras_server_metrics_device_dsp_offload_status(
    const struct cras_iodev* iodev,
    enum CRAS_DEVICE_DSP_OFFLOAD_STATUS code) {
  return 0;
}

int cras_server_metrics_wake_delay(const struct timespec* ts) {
  return 0;
}

int cras_server_metrics_wake_delay_per_10k_wakes(unsigned count) {
  return 0;
}

int cras_server_metrics_peer_supported_a2dp_codecs(unsigned codec_mask) {
  return 0;
}

int cras_server_metrics_ucm_create_status(enum CRAS_ALSA_CARD_TYPE card_type,
                                          bool success) {
  return 0;
}

}  // extern "C"
