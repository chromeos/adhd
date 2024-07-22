/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Types commonly used in the client and server are defined here.
 */
#ifndef CRAS_INCLUDE_CRAS_TYPES_H_
#define CRAS_INCLUDE_CRAS_TYPES_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "cras_audio_format.h"
#include "cras_iodev_info.h"
#include "cras_timespec.h"
#include "packet_status_logger.h"

#ifdef __cplusplus
extern "C" {
#endif

// Some special device index values.
enum CRAS_SPECIAL_DEVICE {
  NO_DEVICE,
  SILENT_RECORD_DEVICE,
  SILENT_PLAYBACK_DEVICE,
  SILENT_HOTWORD_DEVICE,
  MAX_SPECIAL_DEVICE_IDX
};

/*
 * Types of test iodevs supported.
 */
enum TEST_IODEV_TYPE {
  TEST_IODEV_HOTWORD,
};

// Commands for test iodevs.
enum CRAS_TEST_IODEV_CMD {
  TEST_IODEV_CMD_HOTWORD_TRIGGER,
};

// CRAS client connection types.
enum CRAS_CONNECTION_TYPE {
  CRAS_CONTROL,          // For legacy client.
  CRAS_PLAYBACK,         // For playback client.
  CRAS_CAPTURE,          // For capture client.
  CRAS_VMS_LEGACY,       // For legacy client in vms.
  CRAS_VMS_UNIFIED,      // For unified client in vms.
  CRAS_PLUGIN_PLAYBACK,  // For playback client in vms/plugin.
  CRAS_PLUGIN_UNIFIED,   // For unified client in vms/plugin.
  CRAS_NUM_CONN_TYPE,
};

static inline bool cras_validate_connection_type(
    enum CRAS_CONNECTION_TYPE conn_type) {
  return 0 <= conn_type && conn_type < CRAS_NUM_CONN_TYPE;
}

/* Directions of audio streams.
 * Input, Output, or loopback.
 *
 * Note that we use enum CRAS_STREAM_DIRECTION to access the elements in
 * num_active_streams in cras_server_state. For example,
 * num_active_streams[CRAS_STREAM_OUTPUT] is the number of active
 * streams with direction CRAS_STREAM_OUTPUT.
 */
enum CRAS_STREAM_DIRECTION {
  CRAS_STREAM_OUTPUT,
  CRAS_STREAM_INPUT,
  CRAS_STREAM_UNDEFINED,
  CRAS_STREAM_POST_MIX_PRE_DSP,
  CRAS_NUM_DIRECTIONS
};

// Bitmask for supporting all CRAS_STREAM_DIRECTION.
#define CRAS_STREAM_ALL_DIRECTION ((1 << CRAS_NUM_DIRECTIONS) - 1)

/* Converts CRAS_STREAM_DIRECTION to bitmask.
 * Args:
 *   dir - An enum CRAS_STREAM_DIRECTION.
 *
 * Returns:
 *   bitmask for the given direction on success, negative on failure.
 */
static inline int cras_stream_direction_mask(
    const enum CRAS_STREAM_DIRECTION dir) {
  if (0 <= dir && dir < CRAS_NUM_DIRECTIONS) {
    return (1 << dir);
  }
  return -EINVAL;
}

/*
 * Flags for stream types.
 */
enum CRAS_STREAM_FLAG {
  // This stream is OK with receiving up to a full shm of samples
  // in a single callback.
  BULK_AUDIO_OK = 0x01,
  // Don't wake up based on stream timing.  Only wake when the
  // device is ready. Input streams only.
  USE_DEV_TIMING = 0x02,
  // This stream is used only to listen for hotwords such as "OK
  // Google".  Hardware will wake the device when this phrase is heard.
  HOTWORD_STREAM = BULK_AUDIO_OK | USE_DEV_TIMING,
  // This stream only wants to receive when the data is available
  // and does not want to receive data. Used with HOTWORD_STREAM.
  TRIGGER_ONLY = 0x04,
  // This stream doesn't associate to a client. It's used mainly
  // for audio data to flow from hardware through iodev's dsp pipeline.
  SERVER_ONLY = 0x08,
  // This stream will have a pair to share the cras_audio_shm with.
  // Note that it is also a SERVER_ONLY stream.
  SIDETONE_STREAM = 0x18,
};

/*
 * Types of Loopback stream.
 */
enum CRAS_LOOPBACK_TYPE {
  LOOPBACK_POST_MIX_PRE_DSP,
  LOOPBACK_POST_DSP,
  LOOPBACK_POST_DSP_DELAYED,
  LOOPBACK_NUM_TYPES,
};

static inline int cras_stream_uses_output_hw(enum CRAS_STREAM_DIRECTION dir) {
  return dir == CRAS_STREAM_OUTPUT;
}

static inline int cras_stream_uses_input_hw(enum CRAS_STREAM_DIRECTION dir) {
  return dir == CRAS_STREAM_INPUT;
}

static inline int cras_stream_has_input(enum CRAS_STREAM_DIRECTION dir) {
  return dir != CRAS_STREAM_OUTPUT;
}

static inline int cras_stream_is_loopback(enum CRAS_STREAM_DIRECTION dir) {
  return dir == CRAS_STREAM_POST_MIX_PRE_DSP;
}

// Types of audio streams.
enum CRAS_STREAM_TYPE {
  CRAS_STREAM_TYPE_DEFAULT,
  CRAS_STREAM_TYPE_MULTIMEDIA,
  CRAS_STREAM_TYPE_VOICE_COMMUNICATION,
  CRAS_STREAM_TYPE_SPEECH_RECOGNITION,
  CRAS_STREAM_TYPE_PRO_AUDIO,
  CRAS_STREAM_TYPE_ACCESSIBILITY,
  CRAS_STREAM_NUM_TYPES,
};

// Types of audio clients.
enum CRAS_CLIENT_TYPE {
  CRAS_CLIENT_TYPE_UNKNOWN,  // Unknown client
  CRAS_CLIENT_TYPE_LEGACY,   // A client with old craslib (CRAS_PROTO_VER = 3)
  CRAS_CLIENT_TYPE_TEST,     // cras_test_client
  CRAS_CLIENT_TYPE_PCM,      // A client using CRAS via pcm, like aplay
  CRAS_CLIENT_TYPE_CHROME,   // Chrome, UI
  CRAS_CLIENT_TYPE_ARC,      // ARC++
  CRAS_CLIENT_TYPE_CROSVM,   // CROSVM
  CRAS_CLIENT_TYPE_SERVER_STREAM,    // Server stream
  CRAS_CLIENT_TYPE_LACROS,           // LaCrOS
  CRAS_CLIENT_TYPE_PLUGIN,           // PluginVM
  CRAS_CLIENT_TYPE_ARCVM,            // ARCVM
  CRAS_CLIENT_TYPE_BOREALIS,         // Borealis
  CRAS_CLIENT_TYPE_SOUND_CARD_INIT,  // sound_card_init
  CRAS_NUM_CLIENT_TYPE,              // numbers of CRAS_CLIENT_TYPE
};

static inline bool cras_validate_client_type(
    enum CRAS_CLIENT_TYPE client_type) {
  return 0 <= client_type && client_type < CRAS_NUM_CLIENT_TYPE;
}

#define ENUM_STR(x) \
  case x:           \
    return #x;

static inline const char* cras_stream_type_str(
    enum CRAS_STREAM_TYPE stream_type) {
  // clang-format off
	switch (stream_type) {
	ENUM_STR(CRAS_STREAM_TYPE_DEFAULT)
	ENUM_STR(CRAS_STREAM_TYPE_MULTIMEDIA)
	ENUM_STR(CRAS_STREAM_TYPE_VOICE_COMMUNICATION)
	ENUM_STR(CRAS_STREAM_TYPE_SPEECH_RECOGNITION)
	ENUM_STR(CRAS_STREAM_TYPE_PRO_AUDIO)
	ENUM_STR(CRAS_STREAM_TYPE_ACCESSIBILITY)
	default:
		return "INVALID_STREAM_TYPE";
	}
  // clang-format on
}

static inline const char* cras_client_type_str(
    enum CRAS_CLIENT_TYPE client_type) {
  // clang-format off
	switch (client_type) {
	ENUM_STR(CRAS_CLIENT_TYPE_UNKNOWN)
	ENUM_STR(CRAS_CLIENT_TYPE_LEGACY)
	ENUM_STR(CRAS_CLIENT_TYPE_TEST)
	ENUM_STR(CRAS_CLIENT_TYPE_PCM)
	ENUM_STR(CRAS_CLIENT_TYPE_CHROME)
	ENUM_STR(CRAS_CLIENT_TYPE_ARC)
	ENUM_STR(CRAS_CLIENT_TYPE_CROSVM)
	ENUM_STR(CRAS_CLIENT_TYPE_SERVER_STREAM)
	ENUM_STR(CRAS_CLIENT_TYPE_LACROS)
	ENUM_STR(CRAS_CLIENT_TYPE_PLUGIN)
	ENUM_STR(CRAS_CLIENT_TYPE_ARCVM)
	ENUM_STR(CRAS_CLIENT_TYPE_BOREALIS)
	ENUM_STR(CRAS_CLIENT_TYPE_SOUND_CARD_INIT)
	default:
		return "INVALID_CLIENT_TYPE";
	}
  // clang-format on
}

// Effects that can be enabled for a CRAS stream.
enum CRAS_STREAM_EFFECT {
  APM_ECHO_CANCELLATION = (1 << 0),
  APM_NOISE_SUPRESSION = (1 << 1),
  APM_GAIN_CONTROL = (1 << 2),
  APM_VOICE_DETECTION = (1 << 3),
  DSP_ECHO_CANCELLATION_ALLOWED = (1 << 4),
  DSP_NOISE_SUPPRESSION_ALLOWED = (1 << 5),
  DSP_GAIN_CONTROL_ALLOWED = (1 << 6),
  IGNORE_UI_GAINS = (1 << 7),
  // Set CLIENT_CONTROLLED_VOICE_ISOLATION indicates the client forces to
  // enable/disable the platform voice isolation based on the state of
  // VOICE_ISOLATION.
  CLIENT_CONTROLLED_VOICE_ISOLATION = (1 << 8),
  VOICE_ISOLATION = (1 << 9),
  // Indicates that CRAS is free to decide whether to actually apply the
  // APM_* effects on the stream.
  // Private field internal to CRAS. Should not be set by clients.
  PRIVATE_DONT_CARE_APM_EFFECTS = (1 << 10),
};

//
enum RTC_PROC_ON_DSP {
  RTC_PROC_AEC,
  RTC_PROC_NS,
  RTC_PROC_AGC,
};

// Information about a client attached to the server.
struct __attribute__((__packed__)) cras_attached_client_info {
  uint32_t id;
  int32_t pid;
  uint32_t uid;
  uint32_t gid;
};

/* Each ionode has a unique id. The top 32 bits are the device index, lower 32
 * are the node index. */
typedef uint64_t cras_node_id_t;

static inline cras_node_id_t cras_make_node_id(uint32_t dev_index,
                                               uint32_t node_index) {
  cras_node_id_t id = dev_index;
  return (id << 32) | node_index;
}

static inline uint32_t dev_index_of(cras_node_id_t id) {
  return (uint32_t)(id >> 32);
}

static inline uint32_t node_index_of(cras_node_id_t id) {
  return (uint32_t)id;
}

#define CRAS_MAX_IODEVS 20
#define CRAS_MAX_IONODES 20
#define CRAS_MAX_ATTACHED_CLIENTS 20
#define CRAS_MAX_AUDIO_THREAD_SNAPSHOTS 10
#define CRAS_MAX_HOTWORD_MODEL_NAME_SIZE 12
#define MAX_DEBUG_DEVS 4
#define MAX_DEBUG_STREAMS 8
#define AUDIO_THREAD_EVENT_LOG_SIZE (1024 * 6)
#define CRAS_BT_EVENT_LOG_SIZE 1024
#define MAIN_THREAD_EVENT_LOG_SIZE 1024

// There are 8 bits of space for events.
enum AUDIO_THREAD_LOG_EVENTS {
  AUDIO_THREAD_WAKE,
  AUDIO_THREAD_SLEEP,
  AUDIO_THREAD_READ_AUDIO,
  AUDIO_THREAD_READ_AUDIO_TSTAMP,
  AUDIO_THREAD_READ_AUDIO_DONE,
  AUDIO_THREAD_READ_OVERRUN,
  AUDIO_THREAD_FILL_AUDIO,
  AUDIO_THREAD_FILL_AUDIO_TSTAMP,
  AUDIO_THREAD_FILL_AUDIO_DONE,
  AUDIO_THREAD_WRITE_STREAMS_MIX,
  AUDIO_THREAD_WRITE_STREAMS_MIXED,
  AUDIO_THREAD_WRITE_STREAMS_STREAM,
  AUDIO_THREAD_FETCH_STREAM,
  AUDIO_THREAD_STREAM_ADDED,
  AUDIO_THREAD_STREAM_REMOVED,
  AUDIO_THREAD_A2DP_FLUSH,
  AUDIO_THREAD_A2DP_THROTTLE_TIME,
  AUDIO_THREAD_A2DP_WRITE,
  AUDIO_THREAD_LEA_READ,
  AUDIO_THREAD_LEA_WRITE,
  AUDIO_THREAD_DEV_STREAM_MIX,
  AUDIO_THREAD_CAPTURE_POST,
  AUDIO_THREAD_CAPTURE_WRITE,
  AUDIO_THREAD_CONV_COPY,
  AUDIO_THREAD_STREAM_FETCH_PENDING,
  AUDIO_THREAD_STREAM_RESCHEDULE,
  AUDIO_THREAD_STREAM_SLEEP_TIME,
  AUDIO_THREAD_STREAM_SLEEP_ADJUST,
  AUDIO_THREAD_STREAM_SKIP_CB,
  AUDIO_THREAD_DEV_SLEEP_TIME,
  AUDIO_THREAD_SET_DEV_WAKE,
  AUDIO_THREAD_DEV_ADDED,
  AUDIO_THREAD_DEV_REMOVED,
  AUDIO_THREAD_IODEV_CB,
  AUDIO_THREAD_PB_MSG,
  AUDIO_THREAD_ODEV_NO_STREAMS,
  AUDIO_THREAD_ODEV_START,
  AUDIO_THREAD_ODEV_LEAVE_NO_STREAMS,
  AUDIO_THREAD_ODEV_DEFAULT_NO_STREAMS,
  AUDIO_THREAD_FILL_ODEV_ZEROS,
  AUDIO_THREAD_UNDERRUN,
  AUDIO_THREAD_SEVERE_UNDERRUN,
  AUDIO_THREAD_CAPTURE_DROP_TIME,
  AUDIO_THREAD_DEV_DROP_FRAMES,
  AUDIO_THREAD_LOOPBACK_PUT,
  AUDIO_THREAD_LOOPBACK_GET,
  AUDIO_THREAD_LOOPBACK_SAMPLE_HOOK,
  AUDIO_THREAD_DEV_OVERRUN,
  AUDIO_THREAD_DEV_IO_RUN_TIME,
  AUDIO_THREAD_OFFSET_EXCEED_AVAILABLE,
  AUDIO_THREAD_WRITE_STREAM_IS_DRAINING,
  AUDIO_THREAD_UNREASONABLE_AVAILABLE_FRAMES,
  AUDIO_THREAD_WAKE_DELAY,
};

// Important events in main thread.
enum MAIN_THREAD_LOG_EVENTS {
  // iodev related
  // When an iodev closes at stream removal.
  MAIN_THREAD_DEV_CLOSE,
  // When an iodev is removed from active dev list.
  MAIN_THREAD_DEV_DISABLE,
  // When an iodev opens when stream attaches.
  MAIN_THREAD_DEV_INIT,
  // When an iodev reopens for format change.
  MAIN_THREAD_DEV_REOPEN,
  // When an iodev is set as an additional
  // active device.
  MAIN_THREAD_ADD_ACTIVE_NODE,
  // When UI selects an iodev as active.
  MAIN_THREAD_SELECT_NODE,
  // When a jack of iodev is plugged/unplugged.
  MAIN_THREAD_NODE_PLUGGED,
  // When iodev is added to list.
  MAIN_THREAD_ADD_TO_DEV_LIST,
  // When input node gain changes.
  MAIN_THREAD_INPUT_NODE_GAIN,
  // When output node volume changes.
  MAIN_THREAD_OUTPUT_NODE_VOLUME,
  // When display rotation is set.
  MAIN_THREAD_SET_DISPLAY_ROTATION,
  // When output mute state is set.
  MAIN_THREAD_SET_OUTPUT_USER_MUTE,
  // When system resumes and notifies CRAS.
  MAIN_THREAD_RESUME_DEVS,
  // When system suspends and notifies CRAS.
  MAIN_THREAD_SUSPEND_DEVS,
  // When NC-blockage related flags are toggled.
  MAIN_THREAD_NC_BLOCK_STATE,
  // When an iodev enabling/disabling DSP offload is failed/ok.
  MAIN_THREAD_DEV_DSP_OFFLOAD,
  // stream related
  // When an audio stream is added.
  MAIN_THREAD_STREAM_ADDED,
  // When an audio stream is added (always log after MAIN_THREAD_STREAM_ADDED),
  // Contains stream's format.
  MAIN_THREAD_STREAM_ADDED_INFO_FORMAT,
  // When an audio stream is removed.
  MAIN_THREAD_STREAM_REMOVED,
  // server state related
  // When Noise Cancellation is enabled/disabled.
  MAIN_THREAD_NOISE_CANCELLATION,
  // When Style Transfer is enabled/disabled.
  MAIN_THREAD_STYLE_TRANSFER,
  // When VAD target for speak on mute changed.
  MAIN_THREAD_VAD_TARGET_CHANGED,
  // When force respect UI gains is enabled/disabled.
  MAIN_THREAD_FORCE_RESPECT_UI_GAINS,
  // When sidetone is enabled/disabled.
  MAIN_THREAD_SIDETONE,
};

// There are 8 bits of space for events.
enum CRAS_BT_LOG_EVENTS {
  BT_ADAPTER_ADDED,       // BlueZ
  BT_ADAPTER_REMOVED,     // BlueZ
  BT_MANAGER_ADDED,       // Floss
  BT_MANAGER_REMOVED,     // Floss
  BT_AUDIO_GATEWAY_INIT,  // BlueZ
  BT_AUDIO_GATEWAY_START,
  BT_AVAILABLE_CODECS,  // BlueZ
  BT_A2DP_CONFIGURED,
  BT_A2DP_REQUEST_START,
  BT_A2DP_REQUEST_STOP,  // Floss
  BT_A2DP_START,         // BlueZ
  BT_A2DP_SUSPENDED,
  BT_A2DP_SET_VOLUME,
  BT_A2DP_SET_ABS_VOLUME_SUPPORT,  // Floss
  BT_A2DP_UPDATE_VOLUME,
  BT_CODEC_SELECTION,         // BlueZ
  BT_DEV_ADDED,               // Floss
  BT_DEV_REMOVED,             // Floss
  BT_DEV_CONNECTED,           // BlueZ
  BT_DEV_DISCONNECTED,        // BlueZ
  BT_DEV_CONN_WATCH_CB,       // BlueZ
  BT_DEV_SUSPEND_CB,          // BlueZ
  BT_HFP_NEW_CONNECTION,      // BlueZ
  BT_HFP_REQUEST_DISCONNECT,  // BlueZ
  BT_HFP_SUPPORTED_FEATURES,  // BlueZ
  BT_HFP_HF_INDICATOR,        // BlueZ
  BT_HFP_SET_SPEAKER_GAIN,
  BT_HFP_UPDATE_SPEAKER_GAIN,
  BT_HFP_AUDIO_DISCONNECTED,           // Floss
  BT_HSP_NEW_CONNECTION,               // BlueZ
  BT_HSP_REQUEST_DISCONNECT,           // BlueZ
  BT_LEA_AUDIO_CONF_UPDATED,           // Floss
  BT_LEA_GROUP_CONNECTED,              // Floss
  BT_LEA_GROUP_DISCONNECTED,           // Floss
  BT_LEA_GROUP_NODE_STATUS,            // Floss
  BT_LEA_GROUP_STATUS,                 // Floss
  BT_LEA_GROUP_VOLUME_CHANGED,         // Floss
  BT_LEA_SET_GROUP_VOLUME,             // Floss
  BT_LEA_SET_ABS_VOLUME_SUPPORT,       // Floss
  BT_NEW_AUDIO_PROFILE_AFTER_CONNECT,  // BlueZ
  BT_RESET,                            // BlueZ
  BT_SCO_CONNECT,
  BT_SCO_DISCONNECT,       // Floss
  BT_TRANSPORT_RELEASE,    // BlueZ
  BT_HCI_ENABLED,          // Floss
  BT_HFP_TELEPHONY_EVENT,  // Floss
};

struct __attribute__((__packed__)) audio_thread_event {
  uint32_t tag_sec;
  uint32_t nsec;
  uint32_t data1;
  uint32_t data2;
  uint32_t data3;
};

// Ring buffer of log events from the audio thread.
struct __attribute__((__packed__)) audio_thread_event_log {
  uint64_t write_pos;
  uint64_t sync_write_pos;
  uint32_t len;
  struct audio_thread_event log[AUDIO_THREAD_EVENT_LOG_SIZE];
};

struct __attribute__((__packed__)) audio_dev_debug_info {
  char dev_name[CRAS_NODE_NAME_BUFFER_SIZE];
  uint32_t buffer_size;
  uint32_t min_buffer_level;
  uint32_t min_cb_level;
  uint32_t max_cb_level;
  uint32_t frame_rate;
  uint32_t num_channels;
  double est_rate_ratio;
  double est_rate_ratio_when_underrun;
  uint8_t direction;
  uint32_t num_underruns;
  uint32_t num_underruns_during_nc;
  uint32_t num_severe_underruns;
  uint32_t num_samples_dropped;
  uint32_t highest_hw_level;
  uint32_t runtime_sec;
  uint32_t runtime_nsec;
  uint32_t longest_wake_sec;
  uint32_t longest_wake_nsec;
  double internal_gain_scaler;
  uint32_t dev_idx;
  int8_t channel_layout[CRAS_CH_MAX];
};

struct __attribute__((__packed__)) audio_stream_debug_info {
  uint64_t stream_id;
  uint32_t dev_idx;
  uint32_t direction;
  uint32_t stream_type;
  uint32_t client_type;
  uint32_t buffer_frames;
  uint32_t cb_threshold;
  // Effects requested by the stream.
  uint64_t effects;
  // Effects applied on the stream that run in the CPU
  uint64_t active_ap_effects;
  uint32_t flags;
  uint32_t frame_rate;
  uint32_t num_channels;
  uint32_t longest_fetch_sec;
  uint32_t longest_fetch_nsec;
  uint32_t num_delayed_fetches;
  uint32_t num_missed_cb;
  uint32_t num_overruns;
  uint32_t is_pinned;
  uint32_t pinned_dev_idx;
  uint32_t runtime_sec;
  uint32_t runtime_nsec;
  double stream_volume;
  int8_t channel_layout[CRAS_CH_MAX];
  uint32_t overrun_frames;
  uint32_t dropped_samples_duration_sec;
  uint32_t dropped_samples_duration_nsec;
  uint32_t underrun_duration_sec;
  uint32_t underrun_duration_nsec;
  uint64_t webrtc_apm_forward_blocks_processed;
  uint64_t webrtc_apm_reverse_blocks_processed;
};

// Debug info shared from server to client.
struct __attribute__((__packed__)) audio_debug_info {
  uint32_t num_streams;
  uint32_t num_devs;
  struct audio_dev_debug_info devs[MAX_DEBUG_DEVS];
  struct audio_stream_debug_info streams[MAX_DEBUG_STREAMS];
  struct audio_thread_event_log log;
};

struct __attribute__((__packed__)) main_thread_event {
  uint32_t tag_sec;
  uint32_t nsec;
  uint32_t data1;
  uint32_t data2;
  uint32_t data3;
};

struct __attribute__((__packed__)) main_thread_event_log {
  uint32_t write_pos;
  uint32_t len;
  struct main_thread_event log[MAIN_THREAD_EVENT_LOG_SIZE];
};

struct __attribute__((__packed__)) main_thread_debug_info {
  struct main_thread_event_log main_log;
};

struct __attribute__((__packed__)) cras_bt_event {
  uint32_t tag_sec;
  uint32_t nsec;
  uint32_t data1;
  uint32_t data2;
};

struct __attribute__((__packed__)) cras_bt_event_log {
  uint32_t write_pos;
  uint32_t len;
  struct cras_bt_event log[CRAS_BT_EVENT_LOG_SIZE];
};

struct __attribute__((__packed__)) cras_bt_debug_info {
  struct cras_bt_event_log bt_log;
  struct packet_status_logger wbs_logger;
  int32_t floss_enabled;
};

/*
 * All event enums should be less then AUDIO_THREAD_EVENT_TYPE_COUNT,
 * or they will be ignored by the handler.
 */
enum CRAS_AUDIO_THREAD_EVENT_TYPE {
  AUDIO_THREAD_EVENT_A2DP_OVERRUN,
  AUDIO_THREAD_EVENT_A2DP_THROTTLE,
  AUDIO_THREAD_EVENT_BUSYLOOP,
  AUDIO_THREAD_EVENT_DEBUG,
  AUDIO_THREAD_EVENT_SEVERE_UNDERRUN,
  AUDIO_THREAD_EVENT_UNDERRUN,
  AUDIO_THREAD_EVENT_DROP_SAMPLES,
  AUDIO_THREAD_EVENT_DEV_OVERRUN,
  AUDIO_THREAD_EVENT_OFFSET_EXCEED_AVAILABLE,
  AUDIO_THREAD_EVENT_UNREASONABLE_AVAILABLE_FRAMES,
  AUDIO_THREAD_EVENT_TYPE_COUNT,
};

/*
 * Structure of snapshot for audio thread.
 */
struct __attribute__((__packed__)) cras_audio_thread_snapshot {
  struct cras_timespec timestamp;
  enum CRAS_AUDIO_THREAD_EVENT_TYPE event_type;
  struct audio_debug_info audio_debug_info;
};

/*
 * Ring buffer for storing snapshots.
 */
struct __attribute__((__packed__)) cras_audio_thread_snapshot_buffer {
  struct cras_audio_thread_snapshot snapshots[CRAS_MAX_AUDIO_THREAD_SNAPSHOTS];
  int pos;
};

// Flexible loopback parameters
struct __attribute__((__packed__)) cras_floop_params {
  /* Bitmask of client types whose output streams
   * should be attached to the flexible loopback. */
  int64_t client_types_mask;
};

static inline bool cras_floop_params_eq(const struct cras_floop_params* a,
                                        const struct cras_floop_params* b) {
  return a->client_types_mask == b->client_types_mask;
}

/* The server state that is shared with clients. Note that any new members must
 * be appended at the tail of the struct. Otherwise, it will be incompatible
 * with the one in other environments where files can't be updated atomically,
 * like ARC++.
 */
#define CRAS_SERVER_STATE_VERSION 2
struct __attribute__((packed, aligned(4))) cras_server_state {
  // Version of this structure.
  uint32_t state_version;
  // index from 0-100.
  uint32_t volume;
  // volume in dB * 100 when volume = 1.
  int32_t min_volume_dBFS;
  // volume in dB * 100 when volume = max.
  int32_t max_volume_dBFS;
  // 0 = unmuted, 1 = muted by system (device switch, suspend, etc).
  int32_t mute;
  // 0 = unmuted, 1 = muted by user.
  int32_t user_mute;
  // 0 = unlocked, 1 = locked.
  int32_t mute_locked;
  // 1 = suspended, 0 = resumed.
  int32_t suspended;
  // Capture gain in dBFS * 100.
  int32_t capture_gain;
  // 0 = unmuted, 1 = muted.
  int32_t capture_mute;
  // 0 = unlocked, 1 = locked.
  int32_t capture_mute_locked;
  // Flag to indicate if system aec is supported.
  int32_t aec_supported;
  // Group ID for the system aec to use for separating aec
  // tunings.
  int32_t aec_group_id;
  // Total number of streams since server started.
  uint32_t num_streams_attached;
  // Number of available output devices.
  uint32_t num_output_devs;
  // Number of available input devices.
  uint32_t num_input_devs;
  // Output audio devices currently attached.
  struct cras_iodev_info output_devs[CRAS_MAX_IODEVS];
  // Input audio devices currently attached.
  struct cras_iodev_info input_devs[CRAS_MAX_IODEVS];
  // Number of available output nodes.
  uint32_t num_output_nodes;
  // Number of available input nodes.
  uint32_t num_input_nodes;
  // Output nodes currently attached.
  struct cras_ionode_info output_nodes[CRAS_MAX_IONODES];
  // Input nodes currently attached.
  struct cras_ionode_info input_nodes[CRAS_MAX_IONODES];
  // Number of clients attached to server.
  uint32_t num_attached_clients;
  // List of first 20 attached clients.
  struct cras_attached_client_info client_info[CRAS_MAX_ATTACHED_CLIENTS];
  // Incremented twice each time the struct is updated.  Odd
  // during updates.
  uint32_t update_count;
  // An array containing numbers or active
  // streams of different directions.
  uint32_t num_active_streams[CRAS_NUM_DIRECTIONS];
  // Time the last stream was removed.  Can be used
  // to determine how long audio has been idle.
  struct cras_timespec last_active_stream_time;
  // Default output buffer size in frames.
  int32_t default_output_buffer_size;
  // Whether any non-empty audio is being
  // played/captured.
  int32_t non_empty_status;
  // Whether or not bluetooth wideband speech is enabled.
  int32_t bt_wbs_enabled;
  // Whether or not enabling Bluetooth HFP
  // offload feature needs to be determined by Finch flag.
  int32_t bt_hfp_offload_finch_applied;
  // Whether Bluetooth wideband speech mic
  // should be deprioritized for selecting as default audio input.
  int32_t deprioritize_bt_wbs_mic;
  // Whether or not Noise Cancellation is enabled.
  int32_t noise_cancellation_enabled;
  // Whether or not Noise Cancellation is
  // supported by at least one input node by the DSP.
  int32_t dsp_noise_cancellation_supported;
  // Flag to bypass block/unblock Noise
  // Cancellation mechanism.
  int32_t bypass_block_noise_cancellation;
  // 1 = Pause hotword detection when the system
  // suspends. Hotword detection is resumed after system resumes.
  // 0 = Hotword detection is allowed to continue running after system
  // suspends, so a detected hotword can wake up the device.
  int32_t hotword_pause_at_suspend;
  // if system ns is supported.
  int32_t ns_supported;
  // if system agc is supported.
  int32_t agc_supported;
  // Set to true to disable using HW provided echo
  // reference in APM.
  int32_t hw_echo_ref_disabled;
  // The maximum internal mic gain users can set.
  int32_t max_internal_mic_gain;
  // if system aec on dsp is supported.
  int32_t aec_on_dsp_supported;
  // if system ns on dsp is supported.
  int32_t ns_on_dsp_supported;
  // if system agc on dsp is supported.
  int32_t agc_on_dsp_supported;
  int32_t force_respect_ui_gains;
  // Add 3 byte paddings to prevent rust bindgen structure layout
  // mismatch in cras-sys.
  char active_node_type_pair[2 * CRAS_NODE_TYPE_BUFFER_SIZE + 4];
  // The max_supported_channels of internal
  // speaker.
  int32_t max_internal_speaker_channels;
  // The max_supported_channels of headphone
  // and lineout.
  int32_t max_headphone_channels;
  // Number of streams that are not from
  // CLIENT_TYPE_CHROME or CLIENT_TYPE_LACROS
  int32_t num_non_chrome_output_streams;
  // TODO(b/272408566) remove after formal fix lands.
  // 1 - Noise Cancellation standalone mode, which implies that NC is
  // integrated without AEC on DSP. 0 - otherwise.
  int32_t nc_standalone_mode;
  // Whether or not Voice Isolation is supported by at least one input node by
  // either AP or DSP.
  int32_t voice_isolation_supported;
  // An array containing numbers of input
  // streams with permission in each client type.
  uint32_t num_input_streams_with_permission[CRAS_NUM_CLIENT_TYPE];

  // Start of debug structs which may change frequently.
  // Append new members that are accessed in other environments like ARC++
  // before this point so that the server state ABI is not broken when debug
  // structs change.

  // ring buffer for storing bluetooth event logs.
  struct cras_bt_debug_info bt_debug_info;
  // ring buffer for storing main thread event logs.
  struct main_thread_debug_info main_thread_debug_info;
  // ring buffer for storing audio thread snapshots.
  struct cras_audio_thread_snapshot_buffer snapshot_buffer;
  // Debug data filled in when a client requests it. This
  // isn't protected against concurrent updating, only one client should
  // use it.
  struct audio_debug_info audio_debug_info;
};

/* Unique identifier for each active stream.
 * The top 16 bits are the client number, lower 16 are the stream number.
 */
typedef uint32_t cras_stream_id_t;
// Generates a stream id for client stream.
static inline cras_stream_id_t cras_get_stream_id(uint16_t client_id,
                                                  uint16_t stream_id) {
  return (cras_stream_id_t)(((client_id & 0x0000ffff) << 16) |
                            (stream_id & 0x0000ffff));
}
// Verify if the stream_id fits the given client_id
static inline bool cras_valid_stream_id(cras_stream_id_t stream_id,
                                        uint16_t client_id) {
  return ((stream_id >> 16) ^ client_id) == 0;
}

enum CRAS_NODE_TYPE {
  // These value can be used for output nodes.
  CRAS_NODE_TYPE_INTERNAL_SPEAKER,
  CRAS_NODE_TYPE_HEADPHONE,
  CRAS_NODE_TYPE_HDMI,
  CRAS_NODE_TYPE_HAPTIC,
  CRAS_NODE_TYPE_LINEOUT,
  // These value can be used for input nodes.
  CRAS_NODE_TYPE_MIC,
  CRAS_NODE_TYPE_HOTWORD,
  CRAS_NODE_TYPE_POST_MIX_PRE_DSP,
  CRAS_NODE_TYPE_POST_DSP,
  CRAS_NODE_TYPE_POST_DSP_DELAYED,
  // Type for the legacy BT narrow band mic .
  CRAS_NODE_TYPE_BLUETOOTH_NB_MIC,
  // These value can be used for both output and input nodes.
  CRAS_NODE_TYPE_USB,
  CRAS_NODE_TYPE_BLUETOOTH,
  CRAS_NODE_TYPE_FALLBACK_NORMAL,
  CRAS_NODE_TYPE_FALLBACK_ABNORMAL,
  CRAS_NODE_TYPE_UNKNOWN,
  CRAS_NODE_TYPE_ECHO_REFERENCE,
  CRAS_NODE_TYPE_ALSA_LOOPBACK,
  // Flexible loopback input device
  CRAS_NODE_TYPE_FLOOP,
  // Flexible loopback output device used for routing
  CRAS_NODE_TYPE_FLOOP_INTERNAL,
};

// Position values to described where a node locates on the system.
enum CRAS_NODE_POSITION {
  // The node works only when peripheral
  // is plugged.
  NODE_POSITION_EXTERNAL,
  // The node lives on the system and doesn't
  // have specific direction.
  NODE_POSITION_INTERNAL,
  // The node locates on the side of system that
  // faces user.
  NODE_POSITION_FRONT,
  // The node locates on the opposite side of
  // the system that faces user.
  NODE_POSITION_REAR,
  // The node locates under the keyboard.
  NODE_POSITION_KEYBOARD,
};

static inline const char* cras_node_type_to_str(
    enum CRAS_NODE_TYPE type,
    enum CRAS_NODE_POSITION position) {
  switch (type) {
    case CRAS_NODE_TYPE_INTERNAL_SPEAKER:
      return "INTERNAL_SPEAKER";
    case CRAS_NODE_TYPE_HEADPHONE:
      return "HEADPHONE";
    case CRAS_NODE_TYPE_HDMI:
      return "HDMI";
    case CRAS_NODE_TYPE_HAPTIC:
      return "HAPTIC";
    case CRAS_NODE_TYPE_MIC:
      switch (position) {
        case NODE_POSITION_INTERNAL:
          return "INTERNAL_MIC";
        case NODE_POSITION_FRONT:
          return "FRONT_MIC";
        case NODE_POSITION_REAR:
          return "REAR_MIC";
        case NODE_POSITION_KEYBOARD:
          return "KEYBOARD_MIC";
        case NODE_POSITION_EXTERNAL:
        default:
          return "MIC";
      }
    case CRAS_NODE_TYPE_HOTWORD:
      return "HOTWORD";
    case CRAS_NODE_TYPE_LINEOUT:
      return "LINEOUT";
    case CRAS_NODE_TYPE_POST_MIX_PRE_DSP:
      return "POST_MIX_LOOPBACK";
    case CRAS_NODE_TYPE_POST_DSP:
      return "POST_DSP_LOOPBACK";
    case CRAS_NODE_TYPE_POST_DSP_DELAYED:
      return "POST_DSP_DELAYED_LOOPBACK";
    case CRAS_NODE_TYPE_USB:
      return "USB";
    case CRAS_NODE_TYPE_BLUETOOTH:
      return "BLUETOOTH";
    case CRAS_NODE_TYPE_BLUETOOTH_NB_MIC:
      return "BLUETOOTH_NB_MIC";
    case CRAS_NODE_TYPE_FALLBACK_NORMAL:
      return "FALLBACK_NORMAL";
    case CRAS_NODE_TYPE_FALLBACK_ABNORMAL:
      return "FALLBACK_ABNORMAL";
    case CRAS_NODE_TYPE_ECHO_REFERENCE:
      return "ECHO_REFERENCE";
    case CRAS_NODE_TYPE_ALSA_LOOPBACK:
      return "ALSA_LOOPBACK";
    case CRAS_NODE_TYPE_FLOOP:
      return "FLEXIBLE_LOOPBACK";
    case CRAS_NODE_TYPE_FLOOP_INTERNAL:
      return "FLEXIBLE_LOOPBACK_INTERNAL";
    case CRAS_NODE_TYPE_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

/* The bitmask enum of btflags.
 * Bit is toggled on for each attribute that applies.
 */
enum CRAS_BT_FLAGS {
  CRAS_BT_FLAG_NONE = 0,
  // FLOSS is the running Bluetooth stack
  CRAS_BT_FLAG_FLOSS = (1 << 0),
  // For SCO over PCM
  CRAS_BT_FLAG_SCO_OFFLOAD = (1 << 1),
  // A2DP is the current profile
  CRAS_BT_FLAG_A2DP = (1 << 2),
  // HFP is the current profile
  CRAS_BT_FLAG_HFP = (1 << 3),
  // WBS is the preferred/current codec. This is for the purpose of metrics
  // and is only attributed to the input HFP node.
  CRAS_BT_FLAG_WBS = (1 << 4),
  // SWB is the preferred/current codec. This is for the purpose of metrics
  // and is only attributed to the input HFP node.
  CRAS_BT_FLAG_SWB = (1 << 5),
  // LEA is the current profile
  CRAS_BT_FLAG_LEA = (1 << 6),
};

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_INCLUDE_CRAS_TYPES_H_
