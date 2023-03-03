/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_FL_MEDIA_H_
#define CRAS_SRC_SERVER_CRAS_FL_MEDIA_H_

#include <dbus/dbus.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define FL_NULL_ADDRESS "00:00:00:00:00:00"

#define FL_SAMPLE_RATES 8
#define FL_SAMPLE_SIZES 3
#define FL_NUM_CHANNELS 2

#define FL_RATE_NONE 0x00
#define FL_RATE_44100 0x01
#define FL_RATE_48000 0x02
#define FL_RATE_88200 0x04
#define FL_RATE_96000 0x08
#define FL_RATE_176400 0x10
#define FL_RATE_192000 0x20
#define FL_RATE_16000 0x40
#define FL_RATE_24000 0x80

#define FL_SAMPLE_NONE 0x00
#define FL_SAMPLE_16 0x01
#define FL_SAMPLE_24 0x02
#define FL_SAMPLE_32 0x04

#define FL_MODE_NONE 0x00
#define FL_MODE_MONO 0x01
#define FL_MODE_STEREO 0x02

#define FL_A2DP_CODEC_SRC_SBC 0
#define FL_A2DP_CODEC_SRC_AAC 1
#define FL_A2DP_CODEC_SRC_APTX 2
#define FL_A2DP_CODEC_SRC_APTXHD 3
#define FL_A2DP_CODEC_SRC_LDAC 4
#define FL_A2DP_CODEC_SINK_SBC 5
#define FL_A2DP_CODEC_SINK_AAC 6
#define FL_A2DP_CODEC_SINK_LDAC 7
#define FL_A2DP_CODEC_MAX 8

// Bitmask form of enum defined on floss to expose HF's HFP codec capability.
#define FL_CODEC_NONE 0x00
#define FL_CODEC_CVSD 0x01
#define FL_CODEC_MSBC 0x02

struct fl_media;

struct fl_media* floss_media_get_active_fm();

int fl_media_init(int hci);

int floss_media_start(DBusConnection* conn, unsigned int hci);

int floss_media_stop(DBusConnection* conn);

// Calls SetActiveDevice method to Floss media interface.
int floss_media_hfp_set_active_device(struct fl_media* fm, const char* addr);

// Calls StartScoCall to Floss media interface.
// Returns codec inuse (CVSD=1, mSBC=2) on success.
int floss_media_hfp_start_sco_call(struct fl_media* fm,
                                   const char* addr,
                                   bool enable_offload,
                                   bool force_cvsd);

// Calls StopScoCall method to Floss media interface.
int floss_media_hfp_stop_sco_call(struct fl_media* fm, const char* addr);

// Calls SetVolume method to Floss media interface.
int floss_media_hfp_set_volume(struct fl_media* fm,
                               unsigned int volume,
                               const char* addr);

// Unlink hfp with bt_io_manager and destroy related resources.
int floss_media_hfp_suspend(struct fl_media* fm);

// Calls SetActiveDevice method to Floss media interface.
int floss_media_a2dp_set_active_device(struct fl_media* fm, const char* addr);

// Calls SetAudioConfig method to Floss media interface.
int floss_media_a2dp_set_audio_config(struct fl_media* fm,
                                      unsigned int rate,
                                      unsigned int bps,
                                      unsigned int channels);

// Calls GetPresentationPosition method to Floss media interface.
int floss_media_a2dp_get_presentation_position(
    struct fl_media* fm,
    uint64_t* remote_delay_report_ns,
    uint64_t* total_bytes_read,
    struct timespec* data_position_ts);

// Calls SetVolume method to Floss media interface.
int floss_media_a2dp_set_volume(struct fl_media* fm, unsigned int volume);

// Calls StartAudioRequest method to Floss media interface.
int floss_media_a2dp_start_audio_request(struct fl_media* fm, const char* addr);

// Calls StopAudioRequest method to Floss media interface.
int floss_media_a2dp_stop_audio_request(struct fl_media* fm);

// Unlink a2dp with bt_io_manager and destroy related resources.
int floss_media_a2dp_suspend(struct fl_media* fm);

#endif  // CRAS_SRC_SERVER_CRAS_FL_MEDIA_H_
