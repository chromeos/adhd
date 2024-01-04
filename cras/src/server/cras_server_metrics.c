/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_server_metrics.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include "cras/server/main_message.h"
#include "cras/server/platform/dlc/dlc.h"
#include "cras/src/common/cras_metrics.h"
#include "cras/src/common/cras_types_internal.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_rstream_config.h"
#include "cras/src/server/cras_system_state.h"
#include "cras_shm.h"
#include "cras_types.h"
#include "cras_util.h"

#define METRICS_NAME_BUFFER_SIZE 100

const char kA2dpExitCode[] = "Cras.A2dpExitCode";
const char kA2dp20msFailureOverStream[] = "Cras.A2dp20msFailureOverStream";
const char kA2dp100msFailureOverStream[] = "Cras.A2dp100msFailureOverStream";
const char kApNcRuntime[] = "Cras.ApNcRuntime";
const char kApNcStartStatus[] = "Cras.ApNcStartStatus";
const char kBusyloop[] = "Cras.Busyloop";
const char kBusyloopLength[] = "Cras.BusyloopLength";
const char kDeviceOpenStatus[] = "Cras.DeviceOpenStatus";
const char kDeviceTypeInput[] = "Cras.DeviceTypeInput";
const char kDeviceTypeOutput[] = "Cras.DeviceTypeOutput";
const char kDeviceGain[] = "Cras.DeviceGain";
const char kDeviceVolume[] = "Cras.DeviceVolume";
const char kDeviceNoiseCancellationStatus[] =
    "Cras.DeviceNoiseCancellationStatus";
const char kDeviceSampleRate[] = "Cras.DeviceSampleRate";
const char kDeviceDspOffloadStatus[] = "Cras.DeviceDspOffloadStatus";
const char kFetchDelayMilliSeconds[] = "Cras.FetchDelayMilliSeconds";
const char kHighestDeviceDelayInput[] = "Cras.HighestDeviceDelayInput";
const char kHighestDeviceDelayOutput[] = "Cras.HighestDeviceDelayOutput";
const char kHighestInputHardwareLevel[] = "Cras.HighestInputHardwareLevel";
const char kHighestOutputHardwareLevel[] = "Cras.HighestOutputHardwareLevel";
const char kInternalSoundcardStatus5s[] = "Cras.InternalSoundcardStatus";
const char kInternalSoundcardStatus10s[] = "Cras.InternalSoundcardStatus.10s";
const char kMissedCallbackFirstTimeInput[] =
    "Cras.MissedCallbackFirstTimeInput";
const char kMissedCallbackFirstTimeOutput[] =
    "Cras.MissedCallbackFirstTimeOutput";
const char kMissedCallbackFrequencyInput[] =
    "Cras.MissedCallbackFrequencyInput";
const char kMissedCallbackFrequencyOutput[] =
    "Cras.MissedCallbackFrequencyOutput";
const char kMissedCallbackFrequencyAfterReschedulingInput[] =
    "Cras.MissedCallbackFrequencyAfterReschedulingInput";
const char kMissedCallbackFrequencyAfterReschedulingOutput[] =
    "Cras.MissedCallbackFrequencyAfterReschedulingOutput";
const char kMissedCallbackSecondTimeInput[] =
    "Cras.MissedCallbackSecondTimeInput";
const char kMissedCallbackSecondTimeOutput[] =
    "Cras.MissedCallbackSecondTimeOutput";
const char kRtcDevicePair[] = "Cras.RtcDevicePair";
const char kSetAecRefDeviceType[] = "Cras.SetAecRefDeviceType";
const char kStreamCallbackThreshold[] = "Cras.StreamCallbackThreshold";
const char kStreamClientTypeInput[] = "Cras.StreamClientTypeInput";
const char kStreamClientTypeOutput[] = "Cras.StreamClientTypeOutput";
const char kStreamAddError[] = "Cras.StreamAddError";
const char kStreamConnectStatus[] = "Cras.StreamConnectStatus";
const char kStreamCreateError[] = "Cras.StreamCreateError";
const char kStreamFlags[] = "Cras.StreamFlags";
const char kStreamEffects[] = "Cras.StreamEffects";
const char kStreamRuntime[] = "Cras.StreamRuntime";
const char kStreamRuntimeWithMinimum10s[] = "Cras.StreamRuntimeWithMinimum.10s";
const char kStreamSamplingFormat[] = "Cras.StreamSamplingFormat";
const char kStreamSamplingRate[] = "Cras.StreamSamplingRate";
const char kStreamChannelCount[] = "Cras.StreamChannelCount";
const char kUnderrunsPerDevice[] = "Cras.UnderrunsPerDevice";
const char kHfpScoConnectionError[] = "Cras.HfpScoConnectionError";
const char kHfpScoReconnectionOnBusy[] = "Cras.HfpScoReconnectionOnBusy";
const char kHfpBatteryIndicatorSupported[] =
    "Cras.HfpBatteryIndicatorSupported";
const char kHfpBatteryReport[] = "Cras.HfpBatteryReport";
const char kHfpWidebandSpeechSupported[] = "Cras.HfpWidebandSpeechSupported";
const char kHfpWidebandSpeechPacketLoss[] = "Cras.HfpWidebandSpeechPacketLoss";
const char kHfpWidebandSpeechSelectedCodec[] =
    "Cras.kHfpWidebandSpeechSelectedCodec";
const char kHfpMicSuperResolutionStatus[] = "Cras.HfpMicSuperResolutionStatus";
const char kCrasDlcManagerStatus[] = "Cras.DlcManagerStatus";

/*
 * Records missed callback frequency only when the runtime of stream is larger
 * than the threshold.
 */
const double MISSED_CB_FREQUENCY_SECONDS_MIN = 10.0;

const time_t CRAS_METRICS_SHORT_PERIOD_THRESHOLD_SECONDS = 600;
const time_t CRAS_METRICS_LONG_PERIOD_THRESHOLD_SECONDS = 3600;

static const char* get_timespec_period_str(struct timespec ts) {
  if (ts.tv_sec < CRAS_METRICS_SHORT_PERIOD_THRESHOLD_SECONDS) {
    return "ShortPeriod";
  }
  if (ts.tv_sec < CRAS_METRICS_LONG_PERIOD_THRESHOLD_SECONDS) {
    return "MediumPeriod";
  }
  return "LongPeriod";
}

// Type of metrics to log.
enum CRAS_SERVER_METRICS_TYPE {
  A2DP_EXIT_CODE,
  A2DP_20MS_FAILURE_OVER_STREAM,
  A2DP_100MS_FAILURE_OVER_STREAM,
  AP_NC_START_STATUS,
  AP_NC_RUNTIME,
  BT_BATTERY_INDICATOR_SUPPORTED,
  BT_BATTERY_REPORT,
  BT_SCO_CONNECTION_ERROR,
  BT_SCO_RECONNECTION_ON_BUSY,
  BT_WIDEBAND_PACKET_LOSS,
  BT_WIDEBAND_SUPPORTED,
  BT_WIDEBAND_SELECTED_CODEC,
  BT_MIC_SUPER_RESOLUTION_STATUS,
  BUSYLOOP,
  BUSYLOOP_LENGTH,
  DEVICE_CONFIGURE_TIME,
  DEVICE_GAIN,
  DEVICE_OPEN_STATUS,
  DEVICE_RUNTIME,
  DEVICE_VOLUME,
  DEVICE_NOISE_CANCELLATION_STATUS,
  DEVICE_SAMPLE_RATE,
  DEVICE_DSP_OFFLOAD_STATUS,
  DLC_MANAGER_STATUS,
  HIGHEST_DEVICE_DELAY_INPUT,
  HIGHEST_DEVICE_DELAY_OUTPUT,
  HIGHEST_INPUT_HW_LEVEL,
  HIGHEST_OUTPUT_HW_LEVEL,
  INTERNAL_SOUNDCARD_STATUS_5S,
  INTERNAL_SOUNDCARD_STATUS_10S,
  LONGEST_FETCH_DELAY,
  MISSED_CB_FIRST_TIME_INPUT,
  MISSED_CB_FIRST_TIME_OUTPUT,
  MISSED_CB_FREQUENCY_INPUT,
  MISSED_CB_FREQUENCY_OUTPUT,
  MISSED_CB_FREQUENCY_AFTER_RESCHEDULING_INPUT,
  MISSED_CB_FREQUENCY_AFTER_RESCHEDULING_OUTPUT,
  MISSED_CB_SECOND_TIME_INPUT,
  MISSED_CB_SECOND_TIME_OUTPUT,
  NUM_UNDERRUNS,
  RTC_RUNTIME,
  SET_AEC_REF_DEVICE_TYPE,
  STREAM_ADD_ERROR,
  STREAM_CONFIG,
  STREAM_CONNECT_STATUS,
  STREAM_CREATE_ERROR,
  STREAM_RUNTIME
};

/*
 * Please do not change the order of this enum. It will affect the result of
 * metrics.
 */
enum CRAS_METRICS_DEVICE_TYPE {
  // Output devices.
  CRAS_METRICS_DEVICE_INTERNAL_SPEAKER,
  CRAS_METRICS_DEVICE_HEADPHONE,
  CRAS_METRICS_DEVICE_HDMI,
  CRAS_METRICS_DEVICE_HAPTIC,
  CRAS_METRICS_DEVICE_LINEOUT,
  // Input devices.
  CRAS_METRICS_DEVICE_INTERNAL_MIC,
  CRAS_METRICS_DEVICE_FRONT_MIC,
  CRAS_METRICS_DEVICE_REAR_MIC,
  CRAS_METRICS_DEVICE_KEYBOARD_MIC,
  CRAS_METRICS_DEVICE_MIC,
  CRAS_METRICS_DEVICE_HOTWORD,
  CRAS_METRICS_DEVICE_POST_MIX_LOOPBACK,
  CRAS_METRICS_DEVICE_POST_DSP_LOOPBACK,
  // Devices supporting input and output function.
  CRAS_METRICS_DEVICE_USB,
  CRAS_METRICS_DEVICE_A2DP,
  CRAS_METRICS_DEVICE_HFP,
  CRAS_METRICS_DEVICE_HSP,  // Deprecated
  CRAS_METRICS_DEVICE_BLUETOOTH,
  CRAS_METRICS_DEVICE_BLUETOOTH_NB_MIC,
  CRAS_METRICS_DEVICE_NO_DEVICE,
  CRAS_METRICS_DEVICE_NORMAL_FALLBACK,
  CRAS_METRICS_DEVICE_ABNORMAL_FALLBACK,
  CRAS_METRICS_DEVICE_SILENT_HOTWORD,
  CRAS_METRICS_DEVICE_UNKNOWN,
  CRAS_METRICS_DEVICE_BLUETOOTH_WB_MIC,
  CRAS_METRICS_DEVICE_ALSA_LOOPBACK,
  CRAS_METRICS_DEVICE_BLUETOOTH_SWB_MIC,
};

struct cras_server_metrics_stream_config {
  enum CRAS_STREAM_DIRECTION direction;
  unsigned cb_threshold;
  unsigned flags;
  unsigned effects;
  int format;
  unsigned rate;
  unsigned num_channels;
  enum CRAS_CLIENT_TYPE client_type;
};

struct cras_server_metrics_device_data {
  enum CRAS_METRICS_DEVICE_TYPE type;
  enum CRAS_STREAM_DIRECTION direction;
  struct timespec runtime;
  unsigned value;
  int sample_rate;
  enum CRAS_USE_CASE use_case;
  // For DEVICE_OPEN_STATUS:
  //    false - This is the first iodev opened in its iodev group.
  //    true - There are already other open iodev(s) in the group.
  bool has_open_dev;
};

struct cras_server_metrics_stream_data {
  enum CRAS_CLIENT_TYPE client_type;
  enum CRAS_STREAM_TYPE stream_type;
  enum CRAS_STREAM_DIRECTION direction;
  struct timespec runtime;
};

struct cras_server_metrics_timespec_data {
  struct timespec runtime;
  unsigned count;
};

struct cras_server_metrics_rtc_data {
  enum CRAS_METRICS_DEVICE_TYPE in_type;
  enum CRAS_METRICS_DEVICE_TYPE out_type;
  struct timespec runtime;
};

struct cras_server_metrics_dlc_manager_data {
  enum CrasDlcId dlc_id;
  int num_retry_times;
};

union cras_server_metrics_data {
  unsigned value;
  struct cras_server_metrics_stream_config stream_config;
  struct cras_server_metrics_device_data device_data;
  struct cras_server_metrics_stream_data stream_data;
  struct cras_server_metrics_timespec_data timespec_data;
  struct cras_server_metrics_rtc_data rtc_data;
  struct cras_server_metrics_dlc_manager_data dlc_manager_data;
};

/*
 * Make sure the size of message in the acceptable range. Otherwise, it may
 * be split into multiple packets while sending.
 */
static_assert(sizeof(union cras_server_metrics_data) <= 256,
              "The size is too large.");

struct cras_server_metrics_message {
  struct cras_main_message header;
  enum CRAS_SERVER_METRICS_TYPE metrics_type;
  union cras_server_metrics_data data;
};

static void init_server_metrics_msg(struct cras_server_metrics_message* msg,
                                    enum CRAS_SERVER_METRICS_TYPE type,
                                    union cras_server_metrics_data data) {
  memset(msg, 0, sizeof(*msg));
  msg->header.type = CRAS_MAIN_METRICS;
  msg->header.length = sizeof(*msg);
  msg->metrics_type = type;
  msg->data = data;
}

static int send_unsigned_metrics(enum CRAS_SERVER_METRICS_TYPE type,
                                 unsigned num);
static void handle_metrics_message(struct cras_main_message* msg, void* arg);

// The wrapper function of cras_main_message_send.
static int cras_server_metrics_message_send(struct cras_main_message* msg) {
  // If current function is in the main thread, call handler directly.
  if (cras_system_state_in_main_thread()) {
    handle_metrics_message(msg, NULL);
    return 0;
  }
  return cras_main_message_send(msg);
}

static inline const char* metrics_device_type_str(
    enum CRAS_METRICS_DEVICE_TYPE device_type) {
  switch (device_type) {
    case CRAS_METRICS_DEVICE_INTERNAL_SPEAKER:
      return "InternalSpeaker";
    case CRAS_METRICS_DEVICE_HEADPHONE:
      return "Headphone";
    case CRAS_METRICS_DEVICE_HDMI:
      return "HDMI";
    case CRAS_METRICS_DEVICE_HAPTIC:
      return "Haptic";
    case CRAS_METRICS_DEVICE_LINEOUT:
      return "Lineout";
    // Input devices.
    case CRAS_METRICS_DEVICE_INTERNAL_MIC:
      return "InternalMic";
    case CRAS_METRICS_DEVICE_FRONT_MIC:
      return "FrontMic";
    case CRAS_METRICS_DEVICE_REAR_MIC:
      return "RearMic";
    case CRAS_METRICS_DEVICE_KEYBOARD_MIC:
      return "KeyboardMic";
    case CRAS_METRICS_DEVICE_MIC:
      return "Mic";
    case CRAS_METRICS_DEVICE_HOTWORD:
      return "Hotword";
    case CRAS_METRICS_DEVICE_POST_MIX_LOOPBACK:
      return "PostMixLoopback";
    case CRAS_METRICS_DEVICE_POST_DSP_LOOPBACK:
      return "PostDspLoopback";
    // Devices supporting input and output function.
    case CRAS_METRICS_DEVICE_USB:
      return "USB";
    case CRAS_METRICS_DEVICE_A2DP:
      return "A2DP";
    case CRAS_METRICS_DEVICE_HFP:
      return "HFP";
    case CRAS_METRICS_DEVICE_BLUETOOTH:
      return "Bluetooth";
    case CRAS_METRICS_DEVICE_BLUETOOTH_NB_MIC:
      return "BluetoothNarrowBandMic";
    case CRAS_METRICS_DEVICE_BLUETOOTH_WB_MIC:
      return "BluetoothWideBandMic";
    case CRAS_METRICS_DEVICE_BLUETOOTH_SWB_MIC:
      return "BluetoothSuperWideBandMic";
    case CRAS_METRICS_DEVICE_NO_DEVICE:
      return "NoDevice";
    case CRAS_METRICS_DEVICE_ALSA_LOOPBACK:
      return "AlsaLoopback";
    // Other fallback devices.
    case CRAS_METRICS_DEVICE_NORMAL_FALLBACK:
      return "NormalFallback";
    case CRAS_METRICS_DEVICE_ABNORMAL_FALLBACK:
      return "AbnormalFallback";
    case CRAS_METRICS_DEVICE_SILENT_HOTWORD:
      return "SilentHotword";
    case CRAS_METRICS_DEVICE_UNKNOWN:
      return "Unknown";
    default:
      return "InvalidType";
  }
}

static inline const char* metrics_client_type_str(
    enum CRAS_CLIENT_TYPE client_type) {
  switch (client_type) {
    case CRAS_CLIENT_TYPE_UNKNOWN:
      return "Unknown";
    case CRAS_CLIENT_TYPE_LEGACY:
      return "Legacy";
    case CRAS_CLIENT_TYPE_TEST:
      return "Test";
    case CRAS_CLIENT_TYPE_PCM:
      return "PCM";
    case CRAS_CLIENT_TYPE_CHROME:
      return "Chrome";
    case CRAS_CLIENT_TYPE_ARC:
      return "ARC";
    case CRAS_CLIENT_TYPE_CROSVM:
      return "CrOSVM";
    case CRAS_CLIENT_TYPE_SERVER_STREAM:
      return "ServerStream";
    case CRAS_CLIENT_TYPE_LACROS:
      return "LaCrOS";
    case CRAS_CLIENT_TYPE_PLUGIN:
      return "PluginVM";
    case CRAS_CLIENT_TYPE_ARCVM:
      return "ARCVM";
    case CRAS_CLIENT_TYPE_BOREALIS:
      return "BOREALIS";
    case CRAS_CLIENT_TYPE_SOUND_CARD_INIT:
      return "SOUND_CARD_INIT";
    default:
      return "InvalidType";
  }
}

static inline const char* metrics_dlc_id_str(enum CrasDlcId dlc_id) {
  switch (dlc_id) {
    case CrasDlcSrBt:
      return "SrBt";
    case CrasDlcNcAp:
      return "NcAp";
    default:
      return "InvalidDlcId";
  }
}

static inline const char* metrics_stream_type_str(
    enum CRAS_STREAM_TYPE stream_type) {
  switch (stream_type) {
    case CRAS_STREAM_TYPE_DEFAULT:
      return "Default";
    case CRAS_STREAM_TYPE_MULTIMEDIA:
      return "Multimedia";
    case CRAS_STREAM_TYPE_VOICE_COMMUNICATION:
      return "VoiceCommunication";
    case CRAS_STREAM_TYPE_SPEECH_RECOGNITION:
      return "SpeechRecognition";
    case CRAS_STREAM_TYPE_PRO_AUDIO:
      return "ProAudio";
    case CRAS_STREAM_TYPE_ACCESSIBILITY:
      return "Accessibility";
    default:
      return "InvalidType";
  }
}

/*
 * Gets the device type from node type and skip the checking of special devices.
 * This is useful because checking of special devices relies on iodev->info.idx.
 * info.idx of some iodevs remains 0 while the true info.idx is recorded in its
 * parent iodev. For example, hfp_iodev has info.idx equal to 0 and the true idx
 * is in its related bt_io_manager->bt_iodevs.
 *
 * Args:
 *    iodev: the iodev instance.
 *
 * Returns:
 *    The corresponding device type inferred from iodev->active_node->type.
 */
static enum CRAS_METRICS_DEVICE_TYPE
get_metrics_device_type_from_active_node_type(const struct cras_iodev* iodev) {
  switch (iodev->active_node->type) {
    case CRAS_NODE_TYPE_INTERNAL_SPEAKER:
      return CRAS_METRICS_DEVICE_INTERNAL_SPEAKER;
    case CRAS_NODE_TYPE_HEADPHONE:
      return CRAS_METRICS_DEVICE_HEADPHONE;
    case CRAS_NODE_TYPE_HDMI:
      return CRAS_METRICS_DEVICE_HDMI;
    case CRAS_NODE_TYPE_HAPTIC:
      return CRAS_METRICS_DEVICE_HAPTIC;
    case CRAS_NODE_TYPE_LINEOUT:
      return CRAS_METRICS_DEVICE_LINEOUT;
    case CRAS_NODE_TYPE_MIC:
      switch (iodev->active_node->position) {
        case NODE_POSITION_INTERNAL:
          return CRAS_METRICS_DEVICE_INTERNAL_MIC;
        case NODE_POSITION_FRONT:
          return CRAS_METRICS_DEVICE_FRONT_MIC;
        case NODE_POSITION_REAR:
          return CRAS_METRICS_DEVICE_REAR_MIC;
        case NODE_POSITION_KEYBOARD:
          return CRAS_METRICS_DEVICE_KEYBOARD_MIC;
        case NODE_POSITION_EXTERNAL:
        default:
          return CRAS_METRICS_DEVICE_MIC;
      }
    case CRAS_NODE_TYPE_HOTWORD:
      return CRAS_METRICS_DEVICE_HOTWORD;
    case CRAS_NODE_TYPE_POST_MIX_PRE_DSP:
      return CRAS_METRICS_DEVICE_POST_MIX_LOOPBACK;
    case CRAS_NODE_TYPE_POST_DSP:
      return CRAS_METRICS_DEVICE_POST_DSP_LOOPBACK;
    case CRAS_NODE_TYPE_USB:
      return CRAS_METRICS_DEVICE_USB;
    case CRAS_NODE_TYPE_BLUETOOTH: {
      switch (iodev->active_node->btflags &
              (CRAS_BT_FLAG_A2DP | CRAS_BT_FLAG_HFP)) {
        case CRAS_BT_FLAG_A2DP:
          return CRAS_METRICS_DEVICE_A2DP;
        case CRAS_BT_FLAG_HFP:
          /* HFP narrow band has its own node type so we know
           * this is wideband mic for sure. */
          if (iodev->direction == CRAS_STREAM_INPUT) {
            return (iodev->active_node->btflags & CRAS_BT_FLAG_SWB)
                       ? CRAS_METRICS_DEVICE_BLUETOOTH_SWB_MIC
                       : CRAS_METRICS_DEVICE_BLUETOOTH_WB_MIC;
          } else {
            return CRAS_METRICS_DEVICE_HFP;
          }
        default:
          break;
      }
      return CRAS_METRICS_DEVICE_BLUETOOTH;
    }
    case CRAS_NODE_TYPE_BLUETOOTH_NB_MIC:
      return CRAS_METRICS_DEVICE_BLUETOOTH_NB_MIC;
    case CRAS_NODE_TYPE_ALSA_LOOPBACK:
      return CRAS_METRICS_DEVICE_ALSA_LOOPBACK;
    case CRAS_NODE_TYPE_UNKNOWN:
    default:
      return CRAS_METRICS_DEVICE_UNKNOWN;
  }
}

static enum CRAS_METRICS_DEVICE_TYPE get_metrics_device_type(
    const struct cras_iodev* iodev) {
  // Check whether it is a special device.
  if (iodev->info.idx < MAX_SPECIAL_DEVICE_IDX) {
    switch (iodev->info.idx) {
      case NO_DEVICE:
        syslog(LOG_ERR, "The invalid device has been used.");
        return CRAS_METRICS_DEVICE_NO_DEVICE;
      case SILENT_RECORD_DEVICE:
      case SILENT_PLAYBACK_DEVICE:
        if (iodev->active_node->type == CRAS_NODE_TYPE_FALLBACK_NORMAL) {
          return CRAS_METRICS_DEVICE_NORMAL_FALLBACK;
        } else {
          return CRAS_METRICS_DEVICE_ABNORMAL_FALLBACK;
        }
      case SILENT_HOTWORD_DEVICE:
        return CRAS_METRICS_DEVICE_SILENT_HOTWORD;
    }
  }

  return get_metrics_device_type_from_active_node_type(iodev);
}

/*
 * Logs metrics for each group it belongs to. The UMA does not merge subgroups
 * automatically so we need to log them separately.
 *
 * For example, if we call this function with argument (3, 48000,
 * Cras.StreamSamplingRate, Input, Chrome), it will send 48000 to below
 * metrics:
 * Cras.StreamSamplingRate.Input.Chrome
 * Cras.StreamSamplingRate.Input
 * Cras.StreamSamplingRate
 */
static void log_sparse_histogram_each_level(int num, int sample, ...) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE] = {};
  va_list valist;
  int i, len = 0;

  va_start(valist, sample);

  for (i = 0; i < num && len < METRICS_NAME_BUFFER_SIZE; i++) {
    int metric_len =
        snprintf(metrics_name + len, METRICS_NAME_BUFFER_SIZE - len, "%s%s",
                 i ? "." : "", va_arg(valist, char*));
    // Exit early on error or running out of bufferspace. Avoids
    // logging partial or corrupted strings.
    if (metric_len < 0 || metric_len > METRICS_NAME_BUFFER_SIZE - len) {
      break;
    }
    len += metric_len;
    cras_metrics_log_sparse_histogram(metrics_name, sample);
  }

  va_end(valist);
}

static void log_histogram_each_level(int num,
                                     int sample,
                                     int min,
                                     int max,
                                     int nbuckets,
                                     ...) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE] = {};
  va_list valist;
  int i, len = 0;

  va_start(valist, nbuckets);

  for (i = 0; i < num && len < METRICS_NAME_BUFFER_SIZE; i++) {
    int metric_len =
        snprintf(metrics_name + len, METRICS_NAME_BUFFER_SIZE - len, "%s%s",
                 i ? "." : "", va_arg(valist, char*));
    // Exit early on error or running out of bufferspace. Avoids
    // logging partial or corrupted strings.
    if (metric_len < 0 || metric_len > METRICS_NAME_BUFFER_SIZE - len) {
      break;
    }
    len += metric_len;
    cras_metrics_log_histogram(metrics_name, sample, min, max, nbuckets);
  }

  va_end(valist);
}

int cras_server_metrics_hfp_sco_connection_error(
    enum CRAS_METRICS_BT_SCO_ERROR_TYPE type) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.value = type;
  init_server_metrics_msg(&msg, BT_SCO_CONNECTION_ERROR, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "BT_SCO_CONNECTION_ERROR");
    return err;
  }
  return 0;
}

int cras_server_metrics_hfp_sco_reconnection_on_busy(bool success) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.value = success;
  init_server_metrics_msg(&msg, BT_SCO_RECONNECTION_ON_BUSY, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: BT_SCO_RECONNECTION_ON_BUSY");
    return err;
  }
  return 0;
}

int cras_server_metrics_hfp_battery_indicator(int battery_indicator_support) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.value = battery_indicator_support;
  init_server_metrics_msg(&msg, BT_BATTERY_INDICATOR_SUPPORTED, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "BT_BATTERY_INDICATOR_SUPPORTED");
    return err;
  }
  return 0;
}

int cras_server_metrics_hfp_battery_report(int battery_report) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.value = battery_report;
  init_server_metrics_msg(&msg, BT_BATTERY_REPORT, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "BT_BATTERY_REPORT");
    return err;
  }
  return 0;
}

int cras_server_metrics_hfp_packet_loss(float packet_loss_ratio) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  /* Percentage is too coarse for packet loss, so we use number of bad
   * packets per thousand packets instead. */
  data.value = (unsigned)(round(packet_loss_ratio * 1000));
  init_server_metrics_msg(&msg, BT_WIDEBAND_PACKET_LOSS, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: BT_WIDEBAND_PACKET_LOSS");
    return err;
  }
  return 0;
}

int cras_server_metrics_hfp_wideband_support(bool supported) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.value = supported;
  init_server_metrics_msg(&msg, BT_WIDEBAND_SUPPORTED, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: BT_WIDEBAND_SUPPORTED");
    return err;
  }
  return 0;
}

int cras_server_metrics_hfp_wideband_selected_codec(int codec) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.value = codec;
  init_server_metrics_msg(&msg, BT_WIDEBAND_SELECTED_CODEC, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "BT_WIDEBAND_SELECTED_CODEC");
    return err;
  }
  return 0;
}

int cras_server_metrics_hfp_mic_sr_status(
    struct cras_iodev* iodev,
    enum CRAS_METRICS_HFP_MIC_SR_STATUS status) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.device_data.type = get_metrics_device_type_from_active_node_type(iodev);
  data.device_data.value = status;

  init_server_metrics_msg(&msg, BT_MIC_SUPER_RESOLUTION_STATUS, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "BT_MIC_SUPER_RESOLUTION_STATUS");
    return err;
  }

  return 0;
}

int cras_server_metrics_dlc_install_retried_times_on_success(
    enum CrasDlcId dlc_id,
    int num_retry_times) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.dlc_manager_data.dlc_id = dlc_id;
  data.dlc_manager_data.num_retry_times = num_retry_times;

  init_server_metrics_msg(&msg, DLC_MANAGER_STATUS, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "DLC_MANAGER_STATUS");
    return err;
  }

  return 0;
}

int cras_server_metrics_ap_nc_start_status(bool success) {
  int err = send_unsigned_metrics(AP_NC_START_STATUS, success);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: AP_NC_START_STATUS");
    return err;
  }
  return 0;
}

int cras_server_metrics_ap_nc_runtime(unsigned runtime_second) {
  int err = send_unsigned_metrics(AP_NC_RUNTIME, runtime_second);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: AP_NC_RUNTIME");
    return err;
  }
  return 0;
}

int cras_server_metrics_webrtc_devs_runtime(
    const struct cras_iodev* in_dev,
    const struct cras_iodev* out_dev,
    const struct timespec* rtc_start_ts) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  struct timespec now;
  int err;

  data.rtc_data.in_type = get_metrics_device_type(in_dev);
  data.rtc_data.out_type = get_metrics_device_type(out_dev);
  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  subtract_timespecs(&now, rtc_start_ts, &data.rtc_data.runtime);

  // Skip logging RTC streams which run less than 1s.
  if (data.rtc_data.runtime.tv_sec < 1) {
    return 0;
  }

  init_server_metrics_msg(&msg, RTC_RUNTIME, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: RTC_RUNTIME");
    return err;
  }

  return 0;
}

int cras_server_metrics_device_runtime(struct cras_iodev* iodev) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  struct timespec now;
  int err;

  data.device_data.type = get_metrics_device_type(iodev);
  data.device_data.direction = iodev->direction;
  data.device_data.value = iodev->active_node->btflags;
  data.device_data.use_case = cras_iodev_get_use_case(iodev);

  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  subtract_timespecs(&now, &iodev->open_ts, &data.device_data.runtime);

  init_server_metrics_msg(&msg, DEVICE_RUNTIME, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: DEVICE_RUNTIME");
    return err;
  }

  return 0;
}

int cras_server_metrics_device_configure_time(struct cras_iodev* iodev,
                                              struct timespec* beg,
                                              struct timespec* end) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.device_data.type = get_metrics_device_type(iodev);
  data.device_data.direction = iodev->direction;
  data.device_data.value = iodev->active_node->btflags;
  data.device_data.use_case = cras_iodev_get_use_case(iodev);

  subtract_timespecs(end, beg, &data.device_data.runtime);

  init_server_metrics_msg(&msg, DEVICE_CONFIGURE_TIME, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: DEVICE_CONFIGURE_TIME");
    return err;
  }

  return 0;
}

int cras_server_metrics_device_gain(struct cras_iodev* iodev) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    return 0;
  }

  data.device_data.type = get_metrics_device_type(iodev);
  data.device_data.value = (unsigned)100 * iodev->active_node->ui_gain_scaler;

  init_server_metrics_msg(&msg, DEVICE_GAIN, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: DEVICE_GAIN");
    return err;
  }

  return 0;
}

int cras_server_metrics_device_volume(struct cras_iodev* iodev) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  if (iodev->direction == CRAS_STREAM_INPUT) {
    return 0;
  }

  data.device_data.type = get_metrics_device_type(iodev);
  data.device_data.value = iodev->active_node->volume;

  init_server_metrics_msg(&msg, DEVICE_VOLUME, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: DEVICE_VOLUME");
    return err;
  }

  return 0;
}

int cras_server_metrics_device_noise_cancellation_status(
    struct cras_iodev* iodev,
    int status) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.device_data.type = get_metrics_device_type(iodev);
  data.device_data.value = status;

  init_server_metrics_msg(&msg, DEVICE_NOISE_CANCELLATION_STATUS, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "DEVICE_NOISE_CANCELLATION_STATUS");
    return err;
  }

  return 0;
}

int cras_server_metrics_device_sample_rate(struct cras_iodev* iodev) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.device_data.type = get_metrics_device_type(iodev);
  data.device_data.direction = iodev->direction;
  data.device_data.sample_rate = iodev->format->frame_rate;
  data.device_data.use_case = cras_iodev_get_use_case(iodev);

  init_server_metrics_msg(&msg, DEVICE_SAMPLE_RATE, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: DEVICE_SAMPLE_RATE");
    return err;
  }

  return 0;
}

int cras_server_metrics_set_aec_ref_device_type(struct cras_iodev* iodev) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  /* NO_DEVICE means to track system default as echo ref. We expect
   * this is the majority. */
  if (iodev == NULL) {
    data.device_data.type = CRAS_METRICS_DEVICE_NO_DEVICE;
  } else {
    data.device_data.type = get_metrics_device_type(iodev);
  }

  init_server_metrics_msg(&msg, SET_AEC_REF_DEVICE_TYPE, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "SET_AEC_REF_DEVICE_TYPE");
    return err;
  }
  return 0;
}

int cras_server_metrics_highest_device_delay(
    unsigned int hw_level,
    unsigned int largest_cb_level,
    enum CRAS_STREAM_DIRECTION direction) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  if (largest_cb_level == 0) {
    syslog(LOG_WARNING, "Failed to record device delay: divided by zero");
    return -EINVAL;
  }

  /*
   * Because the latency depends on the callback threshold of streams, it
   * should be calculated as dividing the highest hardware level by largest
   * callback threshold of streams. For output device, this value should fall
   * around 2 because CRAS 's scheduling maintain device buffer level around
   * 1~2 minimum callback level. For input device, this value should be around
   * 1 because the device buffer level is around 0~1 minimum callback level.
   * Besides, UMA cannot record float so this ratio is multiplied by 1000.
   */
  data.value = hw_level * 1000 / largest_cb_level;

  switch (direction) {
    case CRAS_STREAM_INPUT:
      init_server_metrics_msg(&msg, HIGHEST_DEVICE_DELAY_INPUT, data);
      break;
    case CRAS_STREAM_OUTPUT:
      init_server_metrics_msg(&msg, HIGHEST_DEVICE_DELAY_OUTPUT, data);
      break;
    default:
      return 0;
  }

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: HIGHEST_DEVICE_DELAY");
    return err;
  }

  return 0;
}

int cras_server_metrics_highest_hw_level(unsigned hw_level,
                                         enum CRAS_STREAM_DIRECTION direction) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.value = hw_level;

  switch (direction) {
    case CRAS_STREAM_INPUT:
      init_server_metrics_msg(&msg, HIGHEST_INPUT_HW_LEVEL, data);
      break;
    case CRAS_STREAM_OUTPUT:
      init_server_metrics_msg(&msg, HIGHEST_OUTPUT_HW_LEVEL, data);
      break;
    default:
      return 0;
  }

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: HIGHEST_HW_LEVEL");
    return err;
  }

  return 0;
}

// Logs longest fetch delay of a stream.
int cras_server_metrics_longest_fetch_delay(const struct cras_rstream* stream) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.stream_data.client_type = stream->client_type;
  data.stream_data.stream_type = stream->stream_type;
  data.stream_data.direction = stream->direction;

  /*
   * There is no delay when the sleep_interval_ts larger than the
   * longest_fetch_interval.
   */
  if (!timespec_after(&stream->longest_fetch_interval,
                      &stream->sleep_interval_ts)) {
    data.stream_data.runtime.tv_sec = 0;
    data.stream_data.runtime.tv_nsec = 0;
  } else {
    subtract_timespecs(&stream->longest_fetch_interval,
                       &stream->sleep_interval_ts, &data.stream_data.runtime);
  }

  init_server_metrics_msg(&msg, LONGEST_FETCH_DELAY, data);
  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: LONGEST_FETCH_DELAY");
    return err;
  }

  return 0;
}

int cras_server_metrics_num_underruns(unsigned num_underruns) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.value = num_underruns;
  init_server_metrics_msg(&msg, NUM_UNDERRUNS, data);
  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: NUM_UNDERRUNS");
    return err;
  }

  return 0;
}

// Logs the frequency of missed callback.
static int cras_server_metrics_missed_cb_frequency(
    const struct cras_rstream* stream) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  struct timespec now, time_since;
  double seconds, frequency;
  int err;

  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  subtract_timespecs(&now, &stream->start_ts, &time_since);
  seconds = (double)time_since.tv_sec + time_since.tv_nsec / 1000000000.0;

  // Ignore streams which do not have enough runtime.
  if (seconds < MISSED_CB_FREQUENCY_SECONDS_MIN) {
    return 0;
  }

  // Compute how many callbacks are missed in a day.
  frequency = (double)stream->num_missed_cb * 86400.0 / seconds;
  data.value = (unsigned)(round(frequency) + 1e-9);

  if (stream->direction == CRAS_STREAM_INPUT) {
    init_server_metrics_msg(&msg, MISSED_CB_FREQUENCY_INPUT, data);
  } else {
    init_server_metrics_msg(&msg, MISSED_CB_FREQUENCY_OUTPUT, data);
  }

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: MISSED_CB_FREQUENCY");
    return err;
  }

  /*
   * If missed callback happened at least once, also record frequency after
   * rescheduling.
   */
  if (!stream->num_missed_cb) {
    return 0;
  }

  subtract_timespecs(&now, &stream->first_missed_cb_ts, &time_since);
  seconds = (double)time_since.tv_sec + time_since.tv_nsec / 1000000000.0;

  // Compute how many callbacks are missed in a day.
  frequency = (double)(stream->num_missed_cb - 1) * 86400.0 / seconds;
  data.value = (unsigned)(round(frequency) + 1e-9);

  if (stream->direction == CRAS_STREAM_INPUT) {
    init_server_metrics_msg(&msg, MISSED_CB_FREQUENCY_AFTER_RESCHEDULING_INPUT,
                            data);
  } else {
    init_server_metrics_msg(&msg, MISSED_CB_FREQUENCY_AFTER_RESCHEDULING_OUTPUT,
                            data);
  }

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: MISSED_CB_FREQUENCY");
    return err;
  }

  return 0;
}

/*
 * Logs the duration between stream starting time and the first missed
 * callback.
 */
static int cras_server_metrics_missed_cb_first_time(
    const struct cras_rstream* stream) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  struct timespec time_since;
  int err;

  subtract_timespecs(&stream->first_missed_cb_ts, &stream->start_ts,
                     &time_since);
  data.value = (unsigned)time_since.tv_sec;

  if (stream->direction == CRAS_STREAM_INPUT) {
    init_server_metrics_msg(&msg, MISSED_CB_FIRST_TIME_INPUT, data);
  } else {
    init_server_metrics_msg(&msg, MISSED_CB_FIRST_TIME_OUTPUT, data);
  }
  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "MISSED_CB_FIRST_TIME");
    return err;
  }

  return 0;
}

// Logs the duration between the first and the second missed callback events.
static int cras_server_metrics_missed_cb_second_time(
    const struct cras_rstream* stream) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  struct timespec now, time_since;
  int err;

  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  subtract_timespecs(&now, &stream->first_missed_cb_ts, &time_since);
  data.value = (unsigned)time_since.tv_sec;

  if (stream->direction == CRAS_STREAM_INPUT) {
    init_server_metrics_msg(&msg, MISSED_CB_SECOND_TIME_INPUT, data);
  } else {
    init_server_metrics_msg(&msg, MISSED_CB_SECOND_TIME_OUTPUT, data);
  }
  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: "
           "MISSED_CB_SECOND_TIME");
    return err;
  }

  return 0;
}

int cras_server_metrics_missed_cb_event(struct cras_rstream* stream) {
  int rc = 0;

  stream->num_missed_cb += 1;
  if (stream->num_missed_cb == 1) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &stream->first_missed_cb_ts);
  }

  // Do not record missed cb if the stream has these flags.
  if (stream->flags & (BULK_AUDIO_OK | USE_DEV_TIMING | TRIGGER_ONLY)) {
    return 0;
  }

  // Only record the first and the second events.
  if (stream->num_missed_cb == 1) {
    rc = cras_server_metrics_missed_cb_first_time(stream);
  } else if (stream->num_missed_cb == 2) {
    rc = cras_server_metrics_missed_cb_second_time(stream);
  }

  return rc;
}

// Logs the stream configurations from clients.
static int cras_server_metrics_stream_config(
    const struct cras_rstream_config* config) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.stream_config.direction = config->direction;
  data.stream_config.cb_threshold = (unsigned)config->cb_threshold;
  data.stream_config.flags = (unsigned)config->flags;
  data.stream_config.effects = (unsigned)config->effects;
  data.stream_config.format = (int)config->format->format;
  data.stream_config.rate = (unsigned)config->format->frame_rate;
  data.stream_config.num_channels = (unsigned)config->format->num_channels;
  data.stream_config.client_type = config->client_type;

  init_server_metrics_msg(&msg, STREAM_CONFIG, data);
  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: STREAM_CONFIG");
    return err;
  }

  return 0;
}

// Logs runtime of a stream.
int cras_server_metrics_stream_runtime(const struct cras_rstream* stream) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  struct timespec now;
  int err;

  data.stream_data.client_type = stream->client_type;
  data.stream_data.stream_type = stream->stream_type;
  data.stream_data.direction = stream->direction;
  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  subtract_timespecs(&now, &stream->start_ts, &data.stream_data.runtime);

  init_server_metrics_msg(&msg, STREAM_RUNTIME, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: STREAM_RUNTIME");
    return err;
  }

  return 0;
}

int cras_server_metrics_stream_create(
    const struct cras_rstream_config* config) {
  return cras_server_metrics_stream_config(config);
}

int cras_server_metrics_stream_destroy(const struct cras_rstream* stream) {
  int rc;
  rc = cras_server_metrics_missed_cb_frequency(stream);
  if (rc < 0) {
    return rc;
  }
  rc = cras_server_metrics_stream_runtime(stream);
  if (rc < 0) {
    return rc;
  }
  return cras_server_metrics_longest_fetch_delay(stream);
}

int cras_server_metrics_busyloop(struct timespec* ts, unsigned count) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.timespec_data.runtime = *ts;
  data.timespec_data.count = count;

  init_server_metrics_msg(&msg, BUSYLOOP, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: BUSYLOOP");
    return err;
  }
  return 0;
}

static int send_unsigned_metrics(enum CRAS_SERVER_METRICS_TYPE type,
                                 unsigned num) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;

  data.value = num;

  init_server_metrics_msg(&msg, type, data);

  return cras_server_metrics_message_send((struct cras_main_message*)&msg);
}

int cras_server_metrics_busyloop_length(unsigned length) {
  int err;
  err = send_unsigned_metrics(BUSYLOOP_LENGTH, length);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: BUSYLOOP_LENGTH");
    return err;
  }
  return 0;
}

int cras_server_metrics_a2dp_exit(enum A2DP_EXIT_CODE code) {
  int err;
  err = send_unsigned_metrics(A2DP_EXIT_CODE, code);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: A2DP_EXIT_CODE");
    return err;
  }
  return 0;
}

int cras_server_metrics_a2dp_20ms_failure_over_stream(unsigned num) {
  int err;
  err = send_unsigned_metrics(A2DP_20MS_FAILURE_OVER_STREAM, num);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: A2DP_20MS_FAILURE_OVER_STREAM");
    return err;
  }
  return 0;
}

int cras_server_metrics_a2dp_100ms_failure_over_stream(unsigned num) {
  int err;
  err = send_unsigned_metrics(A2DP_100MS_FAILURE_OVER_STREAM, num);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: A2DP_100MS_FAILURE_OVER_STREAM");
    return err;
  }
  return 0;
}

int cras_server_metrics_stream_add_failure(enum CRAS_STREAM_ADD_ERROR code) {
  int err;
  err = send_unsigned_metrics(STREAM_ADD_ERROR, code);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message:  STREAM_ADD_ERROR");
    return err;
  }
  return 0;
}

int cras_server_metrics_stream_connect_status(
    enum CRAS_STREAM_CONNECT_STATUS code) {
  int err;
  err = send_unsigned_metrics(STREAM_CONNECT_STATUS, code);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message:  STREAM_CONNECT_STATUS");
    return err;
  }
  return 0;
}

int cras_server_metrics_stream_create_failure(
    enum CRAS_STREAM_CREATE_ERROR code) {
  int err;
  err = send_unsigned_metrics(STREAM_CREATE_ERROR, code);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message:  STREAM_CREATE_ERROR");
    return err;
  }
  return 0;
}

int cras_server_metrics_device_open_status(struct cras_iodev* iodev,
                                           enum CRAS_DEVICE_OPEN_STATUS code,
                                           bool has_open_dev) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  int err;
  union cras_server_metrics_data data = {
      .device_data =
          {
              .type = get_metrics_device_type(iodev),
              .direction = iodev->direction,
              .value = code,
              .use_case = cras_iodev_get_use_case(iodev),
              .has_open_dev = has_open_dev,
          },
  };

  init_server_metrics_msg(&msg, DEVICE_OPEN_STATUS, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING, "Failed to send metrics message: DEVICE_OPEN_STATUS");
    return err;
  }
  return 0;
}

int cras_server_metrics_device_dsp_offload_status(
    const struct cras_iodev* iodev,
    enum CRAS_DEVICE_DSP_OFFLOAD_STATUS code) {
  struct cras_server_metrics_message msg = CRAS_MAIN_MESSAGE_INIT;
  union cras_server_metrics_data data;
  int err;

  data.device_data.type = get_metrics_device_type(iodev);
  data.device_data.value = code;

  init_server_metrics_msg(&msg, DEVICE_DSP_OFFLOAD_STATUS, data);

  err = cras_server_metrics_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: DEVICE_DSP_OFFLOAD_STATUS");
    return err;
  }
  return 0;
}

int cras_server_metrics_internal_soundcard_status(bool detected, int sec) {
  int err;
  enum CRAS_SERVER_METRICS_TYPE metric;

  if (sec == 5) {
    metric = INTERNAL_SOUNDCARD_STATUS_5S;
  } else if (sec == 10) {
    metric = INTERNAL_SOUNDCARD_STATUS_10S;
  } else {
    syslog(LOG_WARNING,
           "Not support to log InternalSoundcardStatus with %d sec", sec);
    return 1;
  }

  err = send_unsigned_metrics(metric, detected);
  if (err < 0) {
    syslog(LOG_WARNING,
           "Failed to send metrics message: INTERNAL_SOUNDCARD_STATUS");
    return err;
  }
  return 0;
}

static void metrics_device_runtime(
    struct cras_server_metrics_device_data data) {
  switch (data.type) {
    case CRAS_METRICS_DEVICE_BLUETOOTH_NB_MIC:
    case CRAS_METRICS_DEVICE_BLUETOOTH_WB_MIC:
    case CRAS_METRICS_DEVICE_BLUETOOTH_SWB_MIC:
      log_histogram_each_level(
          5, (unsigned)data.runtime.tv_sec, 0, 10000, 20, "Cras.DeviceRuntime",
          "Input", "HFP",
          data.value & CRAS_BT_FLAG_SCO_OFFLOAD ? "Offloading"
                                                : "NonOffloading",
          data.type == CRAS_METRICS_DEVICE_BLUETOOTH_NB_MIC ? "NarrowBandMic"
          : data.type == CRAS_METRICS_DEVICE_BLUETOOTH_WB_MIC
              ? "WideBandMic"
              : "SuperWideBandMic");
      break;
    case CRAS_METRICS_DEVICE_HFP:
      log_histogram_each_level(4, (unsigned)data.runtime.tv_sec, 0, 10000, 20,
                               "Cras.DeviceRuntime", "Output", "HFP",
                               data.value & CRAS_BT_FLAG_SCO_OFFLOAD
                                   ? "Offloading"
                                   : "NonOffloading");
      break;
    default:
      log_histogram_each_level(
          4, (unsigned)data.runtime.tv_sec, 0, 10000, 20, "Cras.DeviceRuntime",
          data.direction == CRAS_STREAM_INPUT ? "Input" : "Output",
          metrics_device_type_str(data.type), cras_use_case_str(data.use_case));
      break;
  }

  // TODO(jrwu): deprecate old device runtime metrics
  char metrics_name[METRICS_NAME_BUFFER_SIZE];

  snprintf(metrics_name, METRICS_NAME_BUFFER_SIZE, "Cras.%sDevice%sRuntime",
           data.direction == CRAS_STREAM_INPUT ? "Input" : "Output",
           metrics_device_type_str(data.type));
  cras_metrics_log_histogram(metrics_name, (unsigned)data.runtime.tv_sec, 0,
                             10000, 20);

  // Logs the usage of each device.
  if (data.direction == CRAS_STREAM_INPUT) {
    cras_metrics_log_sparse_histogram(kDeviceTypeInput, data.type);
  } else {
    cras_metrics_log_sparse_histogram(kDeviceTypeOutput, data.type);
  }
}

static void metrics_device_configure_time(
    struct cras_server_metrics_device_data data) {
  unsigned msec = data.runtime.tv_sec * 1000 + data.runtime.tv_nsec / 1000000;
  switch (data.type) {
    case CRAS_METRICS_DEVICE_BLUETOOTH_NB_MIC:
    case CRAS_METRICS_DEVICE_BLUETOOTH_WB_MIC:
    case CRAS_METRICS_DEVICE_BLUETOOTH_SWB_MIC:
      log_histogram_each_level(
          5, msec, 0, 10000, 20, "Cras.DeviceConfigureTime", "Input", "HFP",
          data.value & CRAS_BT_FLAG_SCO_OFFLOAD ? "Offloading"
                                                : "NonOffloading",
          data.type == CRAS_METRICS_DEVICE_BLUETOOTH_NB_MIC ? "NarrowBandMic"
          : data.type == CRAS_METRICS_DEVICE_BLUETOOTH_WB_MIC
              ? "WideBandMic"
              : "SuperWideBandMic");
      break;
    case CRAS_METRICS_DEVICE_HFP:
      log_histogram_each_level(
          4, msec, 0, 10000, 20, "Cras.DeviceConfigureTime", "Output", "HFP",
          data.value & CRAS_BT_FLAG_SCO_OFFLOAD ? "Offloading"
                                                : "NonOffloading");
      break;
    default:
      log_histogram_each_level(
          4, msec, 0, 10000, 20, "Cras.DeviceConfigureTime",
          data.direction == CRAS_STREAM_INPUT ? "Input" : "Output",
          metrics_device_type_str(data.type), cras_use_case_str(data.use_case));
      break;
  }
}

static void metrics_device_gain(struct cras_server_metrics_device_data data) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE];

  snprintf(metrics_name, METRICS_NAME_BUFFER_SIZE, "%s.%s", kDeviceGain,
           metrics_device_type_str(data.type));
  cras_metrics_log_histogram(metrics_name, data.value, 0, 2000, 20);
}

static void metrics_device_volume(struct cras_server_metrics_device_data data) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE];

  snprintf(metrics_name, METRICS_NAME_BUFFER_SIZE, "%s.%s", kDeviceVolume,
           metrics_device_type_str(data.type));
  cras_metrics_log_histogram(metrics_name, data.value, 0, 100, 20);
}

static void metrics_device_noise_cancellation_status(
    struct cras_server_metrics_device_data data) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE];

  snprintf(metrics_name, METRICS_NAME_BUFFER_SIZE, "%s.%s",
           kDeviceNoiseCancellationStatus, metrics_device_type_str(data.type));
  cras_metrics_log_sparse_histogram(metrics_name, data.value);
}

static void metrics_device_sample_rate(
    struct cras_server_metrics_device_data data) {
  log_sparse_histogram_each_level(
      4, data.sample_rate, kDeviceSampleRate,
      data.direction == CRAS_STREAM_INPUT ? "Input" : "Output",
      metrics_device_type_str(data.type), cras_use_case_str(data.use_case));
}

static void metrics_device_dsp_offload_status(
    struct cras_server_metrics_device_data data) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE];

  snprintf(metrics_name, METRICS_NAME_BUFFER_SIZE, "%s.%s",
           kDeviceDspOffloadStatus, metrics_device_type_str(data.type));
  cras_metrics_log_sparse_histogram(metrics_name, data.value);
}

static void metrics_hfp_mic_sr_status(
    struct cras_server_metrics_device_data data) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE];

  snprintf(metrics_name, METRICS_NAME_BUFFER_SIZE, "%s.%s",
           kHfpMicSuperResolutionStatus, metrics_device_type_str(data.type));
  cras_metrics_log_sparse_histogram(metrics_name, data.value);
}

static void metrics_longest_fetch_delay(
    struct cras_server_metrics_stream_data data) {
  int fetch_delay_msec =
      data.runtime.tv_sec * 1000 + data.runtime.tv_nsec / 1000000;
  log_histogram_each_level(3, fetch_delay_msec, 0, 10000, 20,
                           kFetchDelayMilliSeconds,
                           metrics_client_type_str(data.client_type),
                           metrics_stream_type_str(data.stream_type));
}

static void metrics_dlc_install_retried_times_on_success(
    struct cras_server_metrics_dlc_manager_data data) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE];

  // Logs num_retry_times
  snprintf(metrics_name, METRICS_NAME_BUFFER_SIZE,
           "%s.RetriedTimesOnSuccess.%s", kCrasDlcManagerStatus,
           metrics_dlc_id_str(data.dlc_id));
  cras_metrics_log_sparse_histogram(metrics_name, data.num_retry_times);
}

static void metrics_rtc_runtime(struct cras_server_metrics_rtc_data data) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE];
  int value;

  snprintf(metrics_name, METRICS_NAME_BUFFER_SIZE, "Cras.RtcRuntime.%s.%s",
           metrics_device_type_str(data.in_type),
           metrics_device_type_str(data.out_type));
  cras_metrics_log_histogram(metrics_name, (unsigned)data.runtime.tv_sec, 0,
                             10000, 20);

  /*
   * The first 2 digits represents the input device which the last 2 digits
   * represents the output device. The type is from CRAS_METRICS_DEVICE_TYPE.
   */
  value = data.in_type * 100 + data.out_type;
  cras_metrics_log_sparse_histogram(kRtcDevicePair, value);
}

static void metrics_stream_runtime(
    struct cras_server_metrics_stream_data data) {
  log_histogram_each_level(
      4, (int)data.runtime.tv_sec, 0, 10000, 20, kStreamRuntime,
      data.direction == CRAS_STREAM_INPUT ? "Input" : "Output",
      metrics_client_type_str(data.client_type),
      metrics_stream_type_str(data.stream_type));

  if ((int)data.runtime.tv_sec >= 10) {
    log_histogram_each_level(
        4, (int)data.runtime.tv_sec, 10, 10000, 20,
        kStreamRuntimeWithMinimum10s,
        data.direction == CRAS_STREAM_INPUT ? "Input" : "Output",
        metrics_client_type_str(data.client_type),
        metrics_stream_type_str(data.stream_type));
  }
}

static void metrics_busyloop(struct cras_server_metrics_timespec_data data) {
  char metrics_name[METRICS_NAME_BUFFER_SIZE];

  snprintf(metrics_name, METRICS_NAME_BUFFER_SIZE, "%s.%s", kBusyloop,
           get_timespec_period_str(data.runtime));

  cras_metrics_log_histogram(metrics_name, data.count, 0, 1000, 20);
}

static void metrics_stream_config(
    struct cras_server_metrics_stream_config config) {
  const char* direction;

  if (config.direction == CRAS_STREAM_INPUT) {
    direction = "Input";
  } else {
    direction = "Output";
  }

  // Logs stream callback threshold.
  log_sparse_histogram_each_level(3, config.cb_threshold,
                                  kStreamCallbackThreshold, direction,
                                  metrics_client_type_str(config.client_type));

  // Logs stream flags.
  log_sparse_histogram_each_level(3, config.flags, kStreamFlags, direction,
                                  metrics_client_type_str(config.client_type));

  // Logs stream effects.
  log_sparse_histogram_each_level(3, config.effects, kStreamEffects, direction,
                                  metrics_client_type_str(config.client_type));

  // Logs stream sampling format.
  log_sparse_histogram_each_level(3, config.format, kStreamSamplingFormat,
                                  direction,
                                  metrics_client_type_str(config.client_type));

  // Logs stream sampling rate.
  log_sparse_histogram_each_level(3, config.rate, kStreamSamplingRate,
                                  direction,
                                  metrics_client_type_str(config.client_type));

  // Logs stream channel count.
  log_sparse_histogram_each_level(3, config.num_channels, kStreamChannelCount,
                                  direction,
                                  metrics_client_type_str(config.client_type));

  // Logs stream client type.
  if (config.direction == CRAS_STREAM_INPUT) {
    cras_metrics_log_sparse_histogram(kStreamClientTypeInput,
                                      config.client_type);
  } else {
    cras_metrics_log_sparse_histogram(kStreamClientTypeOutput,
                                      config.client_type);
  }
}

static void metrics_device_open_status(
    struct cras_server_metrics_device_data data) {
  log_sparse_histogram_each_level(
      5, data.value, kDeviceOpenStatus,
      data.direction == CRAS_STREAM_INPUT ? "Input" : "Output",
      metrics_device_type_str(data.type), cras_use_case_str(data.use_case),
      data.has_open_dev ? "HasOpenDev" : "FirstOpen");
}

static void handle_metrics_message(struct cras_main_message* msg, void* arg) {
  struct cras_server_metrics_message* metrics_msg =
      (struct cras_server_metrics_message*)msg;
  switch (metrics_msg->metrics_type) {
    case AP_NC_START_STATUS:
      cras_metrics_log_sparse_histogram(kApNcStartStatus,
                                        metrics_msg->data.value);
      break;
    case AP_NC_RUNTIME:
      cras_metrics_log_sparse_histogram(kApNcRuntime, metrics_msg->data.value);
      break;
    case BT_SCO_CONNECTION_ERROR:
      cras_metrics_log_sparse_histogram(kHfpScoConnectionError,
                                        metrics_msg->data.value);
      break;
    case BT_SCO_RECONNECTION_ON_BUSY:
      cras_metrics_log_sparse_histogram(kHfpScoReconnectionOnBusy,
                                        metrics_msg->data.value);
      break;
    case BT_BATTERY_INDICATOR_SUPPORTED:
      cras_metrics_log_sparse_histogram(kHfpBatteryIndicatorSupported,
                                        metrics_msg->data.value);
      break;
    case BT_BATTERY_REPORT:
      cras_metrics_log_sparse_histogram(kHfpBatteryReport,
                                        metrics_msg->data.value);
      break;
    case BT_WIDEBAND_PACKET_LOSS:
      cras_metrics_log_histogram(kHfpWidebandSpeechPacketLoss,
                                 metrics_msg->data.value, 0, 1000, 20);
      break;
    case BT_WIDEBAND_SUPPORTED:
      cras_metrics_log_sparse_histogram(kHfpWidebandSpeechSupported,
                                        metrics_msg->data.value);
      break;
    case BT_WIDEBAND_SELECTED_CODEC:
      cras_metrics_log_sparse_histogram(kHfpWidebandSpeechSelectedCodec,
                                        metrics_msg->data.value);
      break;
    case BT_MIC_SUPER_RESOLUTION_STATUS:
      metrics_hfp_mic_sr_status(metrics_msg->data.device_data);
      break;
    case DEVICE_CONFIGURE_TIME:
      metrics_device_configure_time(metrics_msg->data.device_data);
      break;
    case DEVICE_GAIN:
      metrics_device_gain(metrics_msg->data.device_data);
      break;
    case DEVICE_RUNTIME:
      metrics_device_runtime(metrics_msg->data.device_data);
      break;
    case DEVICE_VOLUME:
      metrics_device_volume(metrics_msg->data.device_data);
      break;
    case DEVICE_NOISE_CANCELLATION_STATUS:
      metrics_device_noise_cancellation_status(metrics_msg->data.device_data);
      break;
    case DEVICE_SAMPLE_RATE:
      metrics_device_sample_rate(metrics_msg->data.device_data);
      break;
    case DEVICE_DSP_OFFLOAD_STATUS:
      metrics_device_dsp_offload_status(metrics_msg->data.device_data);
      break;
    case DLC_MANAGER_STATUS:
      metrics_dlc_install_retried_times_on_success(
          metrics_msg->data.dlc_manager_data);
      break;
    case HIGHEST_DEVICE_DELAY_INPUT:
      cras_metrics_log_histogram(kHighestDeviceDelayInput,
                                 metrics_msg->data.value, 1, 10000, 20);
      break;
    case HIGHEST_DEVICE_DELAY_OUTPUT:
      cras_metrics_log_histogram(kHighestDeviceDelayOutput,
                                 metrics_msg->data.value, 1, 10000, 20);
      break;
    case HIGHEST_INPUT_HW_LEVEL:
      cras_metrics_log_histogram(kHighestInputHardwareLevel,
                                 metrics_msg->data.value, 1, 10000, 20);
      break;
    case HIGHEST_OUTPUT_HW_LEVEL:
      cras_metrics_log_histogram(kHighestOutputHardwareLevel,
                                 metrics_msg->data.value, 1, 10000, 20);
      break;
    case LONGEST_FETCH_DELAY:
      metrics_longest_fetch_delay(metrics_msg->data.stream_data);
      break;
    case MISSED_CB_FIRST_TIME_INPUT:
      cras_metrics_log_histogram(kMissedCallbackFirstTimeInput,
                                 metrics_msg->data.value, 0, 90000, 20);
      break;
    case MISSED_CB_FIRST_TIME_OUTPUT:
      cras_metrics_log_histogram(kMissedCallbackFirstTimeOutput,
                                 metrics_msg->data.value, 0, 90000, 20);
      break;
    case MISSED_CB_FREQUENCY_INPUT:
      cras_metrics_log_histogram(kMissedCallbackFrequencyInput,
                                 metrics_msg->data.value, 0, 90000, 20);
      break;
    case MISSED_CB_FREQUENCY_OUTPUT:
      cras_metrics_log_histogram(kMissedCallbackFrequencyOutput,
                                 metrics_msg->data.value, 0, 90000, 20);
      break;
    case MISSED_CB_FREQUENCY_AFTER_RESCHEDULING_INPUT:
      cras_metrics_log_histogram(kMissedCallbackFrequencyAfterReschedulingInput,
                                 metrics_msg->data.value, 0, 90000, 20);
      break;
    case MISSED_CB_FREQUENCY_AFTER_RESCHEDULING_OUTPUT:
      cras_metrics_log_histogram(
          kMissedCallbackFrequencyAfterReschedulingOutput,
          metrics_msg->data.value, 0, 90000, 20);
      break;
    case MISSED_CB_SECOND_TIME_INPUT:
      cras_metrics_log_histogram(kMissedCallbackSecondTimeInput,
                                 metrics_msg->data.value, 0, 90000, 20);
      break;
    case MISSED_CB_SECOND_TIME_OUTPUT:
      cras_metrics_log_histogram(kMissedCallbackSecondTimeOutput,
                                 metrics_msg->data.value, 0, 90000, 20);
      break;
    case NUM_UNDERRUNS:
      cras_metrics_log_histogram(kUnderrunsPerDevice, metrics_msg->data.value,
                                 0, 1000, 10);
      break;
    case RTC_RUNTIME:
      metrics_rtc_runtime(metrics_msg->data.rtc_data);
      break;
    case STREAM_ADD_ERROR:
      cras_metrics_log_sparse_histogram(kStreamAddError,
                                        metrics_msg->data.value);
      break;
    case STREAM_CONFIG:
      metrics_stream_config(metrics_msg->data.stream_config);
      break;
    case STREAM_CONNECT_STATUS:
      cras_metrics_log_sparse_histogram(kStreamConnectStatus,
                                        metrics_msg->data.value);
      break;
    case STREAM_CREATE_ERROR:
      cras_metrics_log_sparse_histogram(kStreamCreateError,
                                        metrics_msg->data.value);
      break;
    case STREAM_RUNTIME:
      metrics_stream_runtime(metrics_msg->data.stream_data);
      break;
    case BUSYLOOP:
      metrics_busyloop(metrics_msg->data.timespec_data);
      break;
    case BUSYLOOP_LENGTH:
      cras_metrics_log_histogram(kBusyloopLength, metrics_msg->data.value, 0,
                                 1000, 50);
      break;
    case A2DP_EXIT_CODE:
      cras_metrics_log_sparse_histogram(kA2dpExitCode, metrics_msg->data.value);
      break;
    case A2DP_20MS_FAILURE_OVER_STREAM:
      cras_metrics_log_histogram(kA2dp20msFailureOverStream,
                                 metrics_msg->data.value, 0, 1000000000, 20);
      break;
    case A2DP_100MS_FAILURE_OVER_STREAM:
      cras_metrics_log_histogram(kA2dp100msFailureOverStream,
                                 metrics_msg->data.value, 0, 1000000000, 20);
      break;
    case SET_AEC_REF_DEVICE_TYPE:
      cras_metrics_log_sparse_histogram(kSetAecRefDeviceType,
                                        metrics_msg->data.device_data.type);
      break;
    case DEVICE_OPEN_STATUS:
      metrics_device_open_status(metrics_msg->data.device_data);
      break;
    case INTERNAL_SOUNDCARD_STATUS_5S:
      cras_metrics_log_sparse_histogram(kInternalSoundcardStatus5s,
                                        metrics_msg->data.value);
      break;
    case INTERNAL_SOUNDCARD_STATUS_10S:
      cras_metrics_log_sparse_histogram(kInternalSoundcardStatus10s,
                                        metrics_msg->data.value);
      break;
    default:
      syslog(LOG_ERR, "Unknown metrics type %u", metrics_msg->metrics_type);
      break;
  }
}

int cras_server_metrics_init() {
  return cras_main_message_add_handler(CRAS_MAIN_METRICS,
                                       handle_metrics_message, NULL);
}
