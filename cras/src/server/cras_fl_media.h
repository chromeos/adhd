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

#ifdef __cplusplus
extern "C" {
#endif

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

#define FL_LEA_GROUP_NONE -1

// Bitmask form of enum defined on floss to expose HF's available codec IDs.
enum FL_HFP_CODEC_BIT_ID {
  FL_HFP_CODEC_BIT_ID_NONE = 0,
  FL_HFP_CODEC_BIT_ID_CVSD = (1 << 0),
  FL_HFP_CODEC_BIT_ID_MSBC = (1 << 1),
  FL_HFP_CODEC_BIT_ID_LC3 = (1 << 2),
  FL_HFP_CODEC_BIT_ID_UNKNOWN = (1 << 3),
};

// Bitmask form of enum defined on floss to expose available codec and formats.
enum FL_HFP_CODEC_FORMAT {
  FL_HFP_CODEC_FORMAT_NONE = 0,
  FL_HFP_CODEC_FORMAT_CVSD = (1 << 0),
  FL_HFP_CODEC_FORMAT_MSBC_TRANSPARENT = (1 << 1),
  FL_HFP_CODEC_FORMAT_MSBC = (1 << 2),
  FL_HFP_CODEC_FORMAT_LC3_TRANSPARENT = (1 << 3),
  FL_HFP_CODEC_FORMAT_UNKNOWN = (1 << 4),
};

enum FL_LEA_AUDIO_CONTEXT_TYPE {
  FL_LEA_AUDIO_CONTEXT_UNINITIALIZED = 0x0000,
  FL_LEA_AUDIO_CONTEXT_UNSPECIFIED = 0x0001,
  FL_LEA_AUDIO_CONTEXT_CONVERSATIONAL = 0x0002,
  FL_LEA_AUDIO_CONTEXT_MEDIA = 0x0004,
  FL_LEA_AUDIO_CONTEXT_GAME = 0x0008,
  FL_LEA_AUDIO_CONTEXT_INSTRUCTIONAL = 0x0010,
  FL_LEA_AUDIO_CONTEXT_VOICEASSISTANTS = 0x0020,
  FL_LELEAUDIO_CONTEXT_LIVE = 0x0040,
  FL_LEA_AUDIO_CONTEXT_SOUNDEFFECTS = 0x0080,
  FL_LEA_AUDIO_CONTEXT_NOTIFICATIONS = 0x0100,
  FL_LEA_AUDIO_CONTEXT_RINGTONE = 0x0200,
  FL_LEA_AUDIO_CONTEXT_ALERTS = 0x0400,
  FL_LEA_AUDIO_CONTEXT_EMERGENCYALARM = 0x0800,
  FL_LEA_AUDIO_CONTEXT_RFU = 0x1000,
};

enum FL_LEA_AUDIO_DIRECTION {
  FL_LEA_AUDIO_DIRECTION_NONE = 0,
  FL_LEA_AUDIO_DIRECTION_OUTPUT = (1 << 0),
  FL_LEA_AUDIO_DIRECTION_INPUT = (1 << 1),
};

enum FL_LEA_GROUP_STATUS {
  FL_LEA_GROUP_INACTIVE,
  FL_LEA_GROUP_ACTIVE,
  FL_LEA_GROUP_TURNED_IDLE_DURING_CALL,
};

enum FL_LEA_GROUP_NODE_STATUS {
  FL_LEA_GROUP_NODE_ADDED = 1,
  FL_LEA_GROUP_NODE_REMOVED,
};

enum FL_LEA_GROUP_STREAM_STATUS {
  FL_LEA_GROUP_STREAM_STATUS_IDLE = 0,
  FL_LEA_GROUP_STREAM_STATUS_STREAMING,
  FL_LEA_GROUP_STREAM_STATUS_RELEASING,
  FL_LEA_GROUP_STREAM_STATUS_SUSPENDING,
  FL_LEA_GROUP_STREAM_STATUS_SUSPENDED,
  FL_LEA_GROUP_STREAM_STATUS_CONFIGURED_AUTONOMOUS,
  FL_LEA_GROUP_STREAM_STATUS_CONFIGURED_BY_USER,
  FL_LEA_GROUP_STREAM_STATUS_DESTROYED,
};

enum FL_LEA_AUDIO_USAGE {
  FL_LEA_AUDIO_USAGE_UNKNOWN = 0,
  FL_LEA_AUDIO_USAGE_MEDIA = 1,
  FL_LEA_AUDIO_USAGE_VOICE_COMMUNICATION = 2,
  FL_LEA_AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING = 3,
  FL_LEA_AUDIO_USAGE_ALARM = 4,
  FL_LEA_AUDIO_USAGE_NOTIFICATION = 5,
  FL_LEA_AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE = 6,
  FL_LEA_AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST = 7,
  FL_LEA_AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT = 8,
  FL_LEA_AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED = 9,
  FL_LEA_AUDIO_USAGE_NOTIFICATION_EVENT = 10,
  FL_LEA_AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY = 11,
  FL_LEA_AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE = 12,
  FL_LEA_AUDIO_USAGE_ASSISTANCE_SONIFICATION = 13,
  FL_LEA_AUDIO_USAGE_GAME = 14,
  FL_LEA_AUDIO_USAGE_VIRTUAL_SOURCE = 15,
  FL_LEA_AUDIO_USAGE_ASSISTANT = 16,
  FL_LEA_AUDIO_USAGE_CALL_ASSISTANT = 17,
  FL_LEA_AUDIO_USAGE_EMERGENCY = 1000,
  FL_LEA_AUDIO_USAGE_SAFETY = 1001,
  FL_LEA_AUDIO_USAGE_VEHICLE_STATUS = 1002,
  FL_LEA_AUDIO_USAGE_ANNOUNCEMENT = 1003,
};

enum FL_LEA_AUDIO_CONTENT_TYPE {
  FL_LEA_AUDIO_CONTENT_TYPE_UNKNOWN = 0u,
  FL_LEA_AUDIO_CONTENT_TYPE_SPEECH = 1u,
  FL_LEA_AUDIO_CONTENT_TYPE_MUSIC = 2u,
  FL_LEA_AUDIO_CONTENT_TYPE_MOVIE = 3u,
  FL_LEA_AUDIO_CONTENT_TYPE_SONIFICATION = 4u,
};

enum FL_LEA_AUDIO_SOURCE {
  FL_LEA_AUDIO_SOURCE_DEFAULT = 0,
  FL_LEA_AUDIO_SOURCE_MIC = 1,
  FL_LEA_AUDIO_SOURCE_VOICE_UPLINK = 2,
  FL_LEA_AUDIO_SOURCE_VOICE_DOWNLINK = 3,
  FL_LEA_AUDIO_SOURCE_VOICE_CALL = 4,
  FL_LEA_AUDIO_SOURCE_CAMCORDER = 5,
  FL_LEA_AUDIO_SOURCE_VOICE_RECOGNITION = 6,
  FL_LEA_AUDIO_SOURCE_VOICE_COMMUNICATION = 7,
  FL_LEA_AUDIO_SOURCE_REMOTE_SUBMIX = 8,
  FL_LEA_AUDIO_SOURCE_UNPROCESSED = 9,
  FL_LEA_AUDIO_SOURCE_VOICE_PERFORMANCE = 10,
  FL_LEA_AUDIO_SOURCE_ECHO_REFERENCE = 1997,
  FL_LEA_AUDIO_SOURCE_FM_TUNER = 1998,
  FL_LEA_AUDIO_SOURCE_HOTWORD = 1999,
  FL_LEA_AUDIO_SOURCE_INVALID = -1,
};

enum FL_LEA_STREAM_STARTED_STATUS {
  FL_LEA_STREAM_STARTED_STATUS_CANCELED = -1,
  FL_LEA_STREAM_STARTED_STATUS_IDLE = 0,
  FL_LEA_STREAM_STARTED_STATUS_STARTED = 1,
};

struct fl_media;

struct fl_media* floss_media_get_active_fm();

unsigned int floss_media_get_active_hci();

int fl_media_init(int hci);

int floss_media_start(DBusConnection* conn, unsigned int hci);

int floss_media_stop(DBusConnection* conn, unsigned int hci);

// Calls SetHfpActiveDevice method to Floss media interface.
int floss_media_hfp_set_active_device(struct fl_media* fm, const char* addr);

// Calls StartScoCall to Floss media interface.
// Returns codec (enum FL_HFP_CODEC_BIT_ID) inuse on success,
// or negative errno value on error.
int floss_media_hfp_start_sco_call(struct fl_media* fm,
                                   const char* addr,
                                   bool enable_offload,
                                   int disabled_codecs);

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

// Calls ResetActiveDevice method to Floss media interface.
int floss_media_a2dp_reset_active_device(struct fl_media* fm);

// Calls SetAudioConfig method to Floss media interface.
int floss_media_a2dp_set_audio_config(struct fl_media* fm,
                                      const char* addr,
                                      int codec_type,
                                      int sample_rate,
                                      int bits_per_sample,
                                      int channel_mode);

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
int floss_media_a2dp_stop_audio_request(struct fl_media* fm, const char* addr);

// Unlink a2dp with bt_io_manager and destroy related resources.
int floss_media_a2dp_suspend(struct fl_media* fm);

// Disconnects the device.
int floss_media_disconnect_device(struct fl_media* fm, const char* addr);

// Provides the metadata to provide output stream context to Floss.
// |gain| is the normalized linear volume. 0=silence, 1=0dbfs...
int floss_media_lea_source_metadata_changed(
    struct fl_media* fm,
    enum FL_LEA_AUDIO_USAGE usage,
    enum FL_LEA_AUDIO_CONTENT_TYPE content_type,
    double gain);

// Provides the metadata to provide input stream context to Floss.
// |gain| is the normalized linear volume. 0=silence, 1=0dbfs...
int floss_media_lea_sink_metadata_changed(struct fl_media* fm,
                                          enum FL_LEA_AUDIO_SOURCE source,
                                          double gain);

// Request to start the output stream.
int floss_media_lea_host_start_audio_request(struct fl_media* fm,
                                             uint32_t* data_interval_us,
                                             uint32_t* sample_rate,
                                             uint8_t* bits_per_sample,
                                             uint8_t* channels_count);

// Request to start the input stream.
// TODO(b/317682584): verify that this can be invoked alone.
int floss_media_lea_peer_start_audio_request(struct fl_media* fm,
                                             uint32_t* data_interval_us,
                                             uint32_t* sample_rate,
                                             uint8_t* bits_per_sample,
                                             uint8_t* channels_count);

// Request to stop the output stream.
int floss_media_lea_host_stop_audio_request(struct fl_media* fm);

// Request to stop the input stream.
int floss_media_lea_peer_stop_audio_request(struct fl_media* fm);

// Activates the specified group (a non-negative integer).
// Deactivates the active group (if any) when |group_id| is |-1|.
// Ensure this is activated before streaming, and deactivate when
// reconfiguration is needed (which can take seconds).
int floss_media_lea_set_active_group(struct fl_media* fm, int group_id);

// Sets the volume of the specified group, range of volume is [0, 255].
int floss_media_lea_set_group_volume(struct fl_media* fm,
                                     int group_id,
                                     uint8_t volume);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_FL_MEDIA_H_
