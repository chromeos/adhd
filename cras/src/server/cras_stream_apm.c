/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_stream_apm.h"

#include <inttypes.h>
#include <string.h>
#include <syslog.h>
#include <webrtc-apm/webrtc_apm.h>

#include "cras/src/audio_processor/c/plugin_processor.h"
#include "cras/src/common/byte_buffer.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/common/dumper.h"
#include "cras/src/dsp/dsp_util.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_apm_reverse.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_main_message.h"
#include "cras/src/server/cras_processor_config.h"
#include "cras/src/server/cras_speak_on_mute_detector.h"
#include "cras/src/server/float_buffer.h"
#include "cras/src/server/iniparser_wrapper.h"
#include "cras/src/server/rust/include/cras_processor.h"
#include "cras_audio_format.h"
#include "third_party/utlist/utlist.h"

#define AEC_CONFIG_NAME "aec.ini"
#define APM_CONFIG_NAME "apm.ini"
#define WEBRTC_CHANNELS_SUPPORTED_MAX 2

/*
 * Structure holding a WebRTC audio processing module and necessary
 * info to process and transfer input buffer from device to stream.
 *
 * Below chart describes the buffer structure inside APM and how an input buffer
 * flows from a device through the APM to stream. APM processes audio buffers in
 * fixed 10ms width, and that's the main reason we need two copies of the
 * buffer:
 * (1) to cache input buffer from device until 10ms size is filled.
 * (2) to store the interleaved buffer, of 10ms size also, after APM processing.
 *
 *  ________   _______     _______________________________
 *  |      |   |     |     |_____________APM ____________|
 *  |input |-> | DSP |---> ||           |    |          || -> stream 1
 *  |device|   |     | |   || float buf | -> | byte buf ||
 *  |______|   |_____| |   ||___________|    |__________||
 *                     |   |_____________________________|
 *                     |   _______________________________
 *                     |-> |             APM 2           | -> stream 2
 *                     |   |_____________________________|
 *                     |                                       ...
 *                     |
 *                     |------------------------------------> stream N
 */
struct cras_apm {
  // An APM instance from libwebrtc_audio_processing
  webrtc_apm apm_ptr;
  // Pointer to the input device this APM is associated with.
  struct cras_iodev* idev;
  // Stores the processed/interleaved data ready for stream to read.
  struct byte_buffer* buffer;
  // Stores the floating pointer buffer from input device waiting
  // for APM to process.
  struct float_buffer* fbuffer;
  // The format used by the iodev this APM attaches to.
  struct cras_audio_format dev_fmt;
  // The audio data format configured for this APM.
  struct cras_audio_format fmt;
  // The cras_audio_area used for copying processed data to client
  // stream.
  struct cras_audio_area* area;
  // A task queue instance created and destroyed by
  // libwebrtc_apm.
  void* work_queue;
  // Flag to indicate whether content has
  // beenobserved in the left or right channel which is not identical.
  bool only_symmetric_content_in_render;
  // Counter for the number of
  // consecutive frames where nonsymmetric content in render has been
  // observed. Used to avoid triggering on short stereo content.
  int blocks_with_nonsymmetric_content_in_render;
  // Counter for the number of
  // consecutive frames where symmetric content in render has been
  // observed. Used for falling-back to mono processing.
  int blocks_with_symmetric_content_in_render;
  // The audio processor pipeline which is run after the APM.
  // If the APM is created successfully, pp is always non-NULL.
  struct plugin_processor* pp;
  struct cras_apm *prev, *next;
};

/*
 * Structure to hold cras_apm instances created for a stream. A stream may
 * have more than one cras_apm when multiple input devices are enabled.
 * The most common scenario is the silent input iodev be enabled when
 * CRAS switches active input device.
 *
 * Note that cras_stream_apm is owned and modified in main thread.
 * Access with cautious from audio thread.
 */
struct cras_stream_apm {
  // The effecets bit map of APM.
  uint64_t effects;
  // List of APMs for stream processing. It is a list because
  // multiple input devices could be configured by user.
  struct cras_apm* apms;
  // If specified, the pointer to an output iodev which shall be
  // used as echo ref for this apm. When set to NULL it means to
  // follow what the default_rmod provides as echo ref.
  struct cras_iodev* echo_ref;
};

/*
 * Wrappers of APM instances that are active, which means it is associated
 * to a dev/stream pair in audio thread and ready for processing.
 * The existance of an |active_apm| is the key to treat a |cras_apm| is alive
 * and can be used for processing.
 */
struct active_apm {
  // The APM for audio data processing.
  struct cras_apm* apm;
  // The associated |cras_stream_apm| instance. It is ensured by
  // the objects life cycle that whenever an |active_apm| is valid
  // in audio thread, it's safe to access its |stream| member.
  struct cras_stream_apm* stream;
  struct active_apm *prev, *next;
}* active_apms;

// Commands sent to be handled in main thread.
enum CRAS_STREAM_APM_MSG_TYPE {
  APM_DISALLOW_AEC_ON_DSP,
};

struct cras_stream_apm_message {
  struct cras_main_message header;
  enum CRAS_STREAM_APM_MSG_TYPE message_type;
  uint32_t arg1;
  uint32_t arg2;
};

/*
 * Initializes message sent to main thread with type APM_DISALLOW_AEC_ON_DSP.
 * Arguments:
 *   dev_idx - the index of input device APM takes effect on.
 *   is_disallowed - 1 to disallow, 0 otherwise.
 */
static void init_disallow_aec_on_dsp_msg(struct cras_stream_apm_message* msg,
                                         uint32_t dev_idx,
                                         uint32_t is_disallowed) {
  memset(msg, 0, sizeof(*msg));
  msg->header.type = CRAS_MAIN_STREAM_APM;
  msg->header.length = sizeof(*msg);
  msg->message_type = APM_DISALLOW_AEC_ON_DSP;
  msg->arg1 = dev_idx;
  msg->arg2 = is_disallowed;
}

// Commands from main thread to be handled in audio thread.
enum APM_THREAD_CMD {
  APM_REVERSE_DEV_CHANGED,
  APM_SET_AEC_REF,
  APM_VAD_TARGET_CHANGED,
};

// Message to send command to audio thread.
struct apm_message {
  enum APM_THREAD_CMD cmd;
  void* data1;
};

// Socket pair to send message from main thread to audio thread.
static int to_thread_fds[2] = {-1, -1};

static const char* aec_config_dir = NULL;
static char ini_name[MAX_INI_NAME_LENGTH + 1];
static dictionary* aec_ini = NULL;
static dictionary* apm_ini = NULL;

/* VAD target selected by the cras_speak_on_mute_detector module.
 * This is a cached value for use in the audio thread.
 * Should be only updated through update_vad_target().
 */
static struct cras_stream_apm* cached_vad_target = NULL;

/* Mono front center format used to configure the process output end of
 * APM to work around an issue that APM might pick the 1st channel of
 * input, process and then writes to all output channels.
 *
 * The exact condition to trigger this:
 * (1) More than one channel in input
 * (2) More than one channel in output
 * (3) multi_channel_capture is false
 *
 * We're not ready to turn on multi_channel_capture so the best option is
 * to address (2). This is an acceptable fix because it makes APM's
 * behavior align with browser APM.
 */
static struct cras_audio_format mono_channel = {
    0,  // unused
    0,  // unused
    1,  // mono, front center
    {-1, -1, -1, -1, 0, -1, -1, -1, -1, -1, -1}};

static inline bool should_enable_dsp_aec(uint64_t effects) {
  return (effects & DSP_ECHO_CANCELLATION_ALLOWED) &&
         (effects & APM_ECHO_CANCELLATION);
}

static inline bool should_enable_dsp_ns(uint64_t effects) {
  return (effects & DSP_NOISE_SUPPRESSION_ALLOWED) &&
         (effects & APM_NOISE_SUPRESSION);
}

static inline bool should_enable_dsp_agc(uint64_t effects) {
  return (effects & DSP_GAIN_CONTROL_ALLOWED) && (effects & APM_GAIN_CONTROL);
}

static bool stream_apm_should_enable_vad(struct cras_stream_apm* stream_apm) {
  /* There is no stream_apm->effects bit allocated for client VAD
   * usage. Determine whether VAD should be enabled solely by the
   * requirements of speak-on-mute detection. */
  return cached_vad_target == stream_apm;
}

/*
 * Analyzes the active APMs on the idev and returns whether any of them
 * cause a conflict to enabling DSP |effect| on |idev|.
 */
static bool dsp_effect_check_conflict(struct cras_iodev* const idev,
                                      enum RTC_PROC_ON_DSP effect) {
  struct active_apm* active;

  // Return true if any APM should not apply the effect on DSP.
  DL_FOREACH (active_apms, active) {
    if (active->apm->idev != idev) {
      continue;
    }

    switch (effect) {
      case RTC_PROC_AEC: {
        /*
         * The AEC effect can only be applied if the audio
         * output devices configuration meets our AEC usecase.
         */
        bool is_dsp_aec_use_case =
            cras_iodev_is_dsp_aec_use_case(idev->active_node) &&
            cras_apm_reverse_is_aec_use_case(active->stream->echo_ref);
        if (!(is_dsp_aec_use_case &&
              should_enable_dsp_aec(active->stream->effects))) {
          return true;
        }
        break;
      }
      case RTC_PROC_NS:
        if (!should_enable_dsp_ns(active->stream->effects)) {
          return true;
        }
        break;
      case RTC_PROC_AGC:
        if (!should_enable_dsp_agc(active->stream->effects)) {
          return true;
        }
        break;
      default:
        syslog(LOG_WARNING, "Unhandled RTC_PROC %d", effect);
        break;
    }
  }

  /* Return false otherwise. Nothing conflicts with activating |effect|
   * on |idev| */
  return false;
}

/*
 * Analyzes the active APMs and returns whether the effect is active in any of
 * them.
 */
static bool effect_needed_for_dev(const struct cras_iodev* const idev,
                                  uint64_t effect) {
  struct active_apm* active;
  struct cras_apm* apm;
  DL_FOREACH (active_apms, active) {
    apm = active->apm;
    if (apm->idev == idev && ((bool)(active->stream->effects & effect))) {
      return true;
    }
  }
  return false;
}

/*
 * Try setting the DSP effect to the desired state. Returns the state that was
 * achieved for the DSP effect.
 */
static bool toggle_dsp_effect(struct cras_iodev* const idev,
                              uint64_t effect,
                              bool should_be_activated) {
  bool dsp_effect_activated = cras_iodev_get_rtc_proc_enabled(idev, effect);

  /* Toggle the DSP effect if it is not activated according to what is
   * specified. */
  if (dsp_effect_activated != should_be_activated) {
    syslog(LOG_DEBUG, "cras_iodev_set_rtc_proc_enabled DSP effect %u=%d",
           (uint32_t)effect, should_be_activated ? 1 : 0);
    cras_iodev_set_rtc_proc_enabled(idev, effect, should_be_activated ? 1 : 0);

    // Verify the DSP effect activation state.
    dsp_effect_activated = cras_iodev_get_rtc_proc_enabled(idev, effect);
  }

  if (dsp_effect_activated != should_be_activated) {
    syslog(LOG_WARNING, "Failed to %s DSP effect 0x%lx",
           should_be_activated ? "enable" : "disable", (unsigned long)effect);
  }

  return dsp_effect_activated;
}

/*
 * Adds the info of APM_DISALLOW_AEC_ON_DSP messages to a list. The list is used
 * for reducing repeated and conflicted infos, minimizing the number of messgaes
 * to be sent to the main thread.
 */
static void add_disallow_aec_on_dsp_info_to_list(uint32_t* info_list,
                                                 size_t* info_list_size,
                                                 uint32_t dev_idx,
                                                 uint32_t is_disallowed) {
  size_t i;
  // info: [31:16] for dev_idx, [15:0] for is_disallowed
  uint32_t info = (dev_idx << 16) + (is_disallowed & 0xFFFF);

  for (i = 0; i < *info_list_size; i++) {
    uint32_t list_info = *(info_list + i);
    // repeated info
    if (list_info == info) {
      return;
    }
    // conflicted info for the same device, replace with the latter
    if (list_info >> 16 == info >> 16) {
      *(info_list + i) = info;
      syslog(LOG_ERR, "disallow_aec_on_dsp conflicted on dev:%u, %u", dev_idx,
             is_disallowed);
      return;
    }
  }

  *(info_list + *info_list_size) = info;
  (*info_list_size)++;
}

// Sends APM_DISALLOW_AEC_ON_DSP message to the main thread.
static void send_disallow_aec_on_dsp_msg_from_info(uint32_t info) {
  struct cras_stream_apm_message msg = CRAS_MAIN_MESSAGE_INIT;
  int err;

  // info: [31:16] for dev_idx, [15:0] for is_disallowed
  syslog(LOG_DEBUG, "Send APM_DISALLOW_AEC_ON_DSP(%u, %u) message to main",
         info >> 16, info & 0xFFFF);
  init_disallow_aec_on_dsp_msg(&msg, info >> 16, info & 0xFFFF);
  err = cras_main_message_send((struct cras_main_message*)&msg);
  if (err < 0) {
    syslog(LOG_ERR, "Failed to send stream apm message %d: %d",
           APM_DISALLOW_AEC_ON_DSP, err);
  }
}

/*
 * Iterates all active apms and applies the restrictions to determine
 * whether or not to activate effects on DSP for each associated
 * input devices. Called in audio thread.
 */
static void update_supported_dsp_effects_activation() {
  /*
   * DSP effect restriction rules to follow in order.
   *
   * Note that there could be more than one APMs attach to the same idev
   * that request different effects. Therefore we need to iterate through
   * all APMs to find out the answer to each rule.
   *
   * 1. If the associated input and output device pair don't allign with
   * the AEC use case, we shall deactivate DSP effects on idev.
   * 2. We shall deactivate DSP effects on idev if any APM has requested
   * to not be applied.
   * 3. We shall activate DSP effects on idev if there's at least one APM
   * requesting it.
   */

  // Toggle between having effects applied on DSP and in CRAS for each APM
  struct active_apm* active;
  struct cras_apm* apm;
  uint32_t disallow_aec_on_dsp_infos[CRAS_MAX_IODEVS];
  size_t disallow_aec_on_dsp_info_size = 0;
  size_t i;
  LL_FOREACH (active_apms, active) {
    apm = active->apm;
    struct cras_iodev* const idev = apm->idev;

    // Try to activate effects on DSP.
    bool aec_on_dsp = false;
    bool ns_on_dsp = false;
    bool agc_on_dsp = false;

    /*
     * Check all APMs on idev to see what effects are active and
     * what effects can be applied on DSP.
     */
    bool aec_needed = effect_needed_for_dev(idev, APM_ECHO_CANCELLATION);
    bool ns_needed = effect_needed_for_dev(idev, APM_NOISE_SUPRESSION);
    bool agc_needed = effect_needed_for_dev(idev, APM_GAIN_CONTROL);

    /*
     * Identify if effects can be activated on DSP and attempt
     * toggling the DSP effect.
     */
    aec_on_dsp = aec_needed && !dsp_effect_check_conflict(idev, RTC_PROC_AEC);
    aec_on_dsp = toggle_dsp_effect(idev, RTC_PROC_AEC, aec_on_dsp);

    ns_on_dsp = ns_needed && (aec_on_dsp || !aec_needed) &&
                !dsp_effect_check_conflict(idev, RTC_PROC_NS);
    ns_on_dsp = toggle_dsp_effect(idev, RTC_PROC_NS, ns_on_dsp);

    agc_on_dsp = agc_needed && (ns_on_dsp || !(ns_needed || aec_needed)) &&
                 !dsp_effect_check_conflict(idev, RTC_PROC_AGC);
    agc_on_dsp = toggle_dsp_effect(idev, RTC_PROC_AGC, agc_on_dsp);

    /*
     * Toggle effects on CRAS APM depending on what the state of
     * effect activation is on DSP.
     */
    webrtc_apm_enable_effects(
        active->apm->apm_ptr,
        (active->stream->effects & APM_ECHO_CANCELLATION) && !aec_on_dsp,
        (active->stream->effects & APM_NOISE_SUPRESSION) && !ns_on_dsp,
        (active->stream->effects & APM_GAIN_CONTROL) && !agc_on_dsp);

    if (!cras_iodev_support_rtc_proc_on_dsp(idev, RTC_PROC_AEC)) {
      continue;
    }

    // Collect APM_DISALLOW_AEC_ON_DSP infos to be sent.
    add_disallow_aec_on_dsp_info_to_list(disallow_aec_on_dsp_infos,
                                         &disallow_aec_on_dsp_info_size,
                                         idev->info.idx, !aec_on_dsp);
  }

  // Send APM_DISALLOW_AEC_ON_DSP messages to the main thread.
  for (i = 0; i < disallow_aec_on_dsp_info_size; i++) {
    send_disallow_aec_on_dsp_msg_from_info(disallow_aec_on_dsp_infos[i]);
  }
}

// Reconfigure APMs to update their VAD enabled status.
static void reconfigure_apm_vad() {
  struct active_apm* active;
  LL_FOREACH (active_apms, active) {
    webrtc_apm_enable_vad(active->apm->apm_ptr,
                          stream_apm_should_enable_vad(active->stream));
  }
}

// Set the VAD target to new_vad_target and propagate the changes to APMs.
static void update_vad_target(struct cras_stream_apm* new_vad_target) {
  cached_vad_target = new_vad_target;
  reconfigure_apm_vad();
}

static void apm_destroy(struct cras_apm** apm) {
  if (*apm == NULL) {
    return;
  }

  if ((*apm)->pp) {
    (*apm)->pp->ops->destroy((*apm)->pp);
  }

  byte_buffer_destroy(&(*apm)->buffer);
  float_buffer_destroy(&(*apm)->fbuffer);
  cras_audio_area_destroy((*apm)->area);

  // Any unfinished AEC dump handle will be closed.
  webrtc_apm_destroy((*apm)->apm_ptr);
  free(*apm);
  *apm = NULL;
}

struct cras_stream_apm* cras_stream_apm_create(uint64_t effects) {
  struct cras_stream_apm* stream;

  if (effects == 0) {
    return NULL;
  }

  stream = (struct cras_stream_apm*)calloc(1, sizeof(*stream));
  if (stream == NULL) {
    syslog(LOG_ERR, "No memory in creating stream apm");
    return NULL;
  }
  stream->effects = effects;
  stream->apms = NULL;
  stream->echo_ref = NULL;

  return stream;
}

static struct active_apm* get_active_apm(struct cras_stream_apm* stream,
                                         const struct cras_iodev* idev) {
  struct active_apm* active;

  DL_FOREACH (active_apms, active) {
    if ((active->apm->idev == idev) && (active->stream == stream)) {
      return active;
    }
  }
  return NULL;
}

struct cras_apm* cras_stream_apm_get_active(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev) {
  struct active_apm* active = get_active_apm(stream, idev);
  return active ? active->apm : NULL;
}

uint64_t cras_stream_apm_get_effects(struct cras_stream_apm* stream) {
  if (stream == NULL) {
    return 0;
  } else {
    return stream->effects;
  }
}

void cras_stream_apm_remove(struct cras_stream_apm* stream,
                            const struct cras_iodev* idev) {
  struct cras_apm* apm;

  DL_FOREACH (stream->apms, apm) {
    if (apm->idev == idev) {
      DL_DELETE(stream->apms, apm);
      apm_destroy(&apm);
    }
  }
}

/*
 * For playout, Chromium generally upmixes mono audio content to stereo before
 * passing the signal to CrAS. To avoid that APM in CrAS treats these as proper
 * stereo signals, this method detects when the content in the first two
 * channels is non-symmetric. That detection allows APM to treat stereo signal
 * as upmixed mono.
 */
int left_and_right_channels_are_symmetric(int num_channels,
                                          int rate,
                                          float* const* data) {
  if (num_channels <= 1) {
    return true;
  }

  const int frame_length = rate / APM_NUM_BLOCKS_PER_SECOND;
  return (0 == memcmp(data[0], data[1], frame_length * sizeof(float)));
}

/*
 * WebRTC APM handles no more than stereo + keyboard mic channels.
 * Ignore keyboard mic feature for now because that requires processing on
 * mixed buffer from two input devices. Based on that we should modify the best
 * channel layout for APM use.
 * Args:
 *    apm_fmt - Pointer to a format struct already filled with the value of
 *        the open device format. Its content may be modified for APM use.
 */
static void get_best_channels(struct cras_audio_format* apm_fmt) {
  int ch;
  int8_t layout[CRAS_CH_MAX];

  /* Using the format from dev_fmt is dangerous because input device
   * could have wild configurations like unuse the 1st channel and
   * connects 2nd channel to the only mic. Data in the first channel
   * is what APM cares about so always construct a new channel layout
   * containing subset of original channels that matches either FL, FR,
   * or FC.
   * TODO(hychao): extend the logic when we have a stream that wants
   * to record channels like RR(rear right).
   */
  for (ch = 0; ch < CRAS_CH_MAX; ch++) {
    layout[ch] = -1;
  }

  apm_fmt->num_channels = 0;
  if (apm_fmt->channel_layout[CRAS_CH_FL] != -1) {
    layout[CRAS_CH_FL] = apm_fmt->num_channels++;
  }
  if (apm_fmt->channel_layout[CRAS_CH_FR] != -1) {
    layout[CRAS_CH_FR] = apm_fmt->num_channels++;
  }
  if (apm_fmt->channel_layout[CRAS_CH_FC] != -1) {
    layout[CRAS_CH_FC] = apm_fmt->num_channels++;
  }

  for (ch = 0; ch < CRAS_CH_MAX; ch++) {
    apm_fmt->channel_layout[ch] = layout[ch];
  }
}

struct cras_apm* cras_stream_apm_add(struct cras_stream_apm* stream,
                                     struct cras_iodev* idev,
                                     const struct cras_audio_format* dev_fmt) {
  struct cras_apm* apm;
  bool aec_applied_on_dsp = false;
  bool ns_applied_on_dsp = false;
  bool agc_applied_on_dsp = false;

  DL_FOREACH (stream->apms, apm) {
    if (apm->idev == idev) {
      return apm;
    }
  }

  // TODO(hychao): Remove the check when we enable more effects.
  if (!((stream->effects & APM_ECHO_CANCELLATION) ||
        (stream->effects & APM_NOISE_SUPRESSION) ||
        (stream->effects & APM_GAIN_CONTROL))) {
    return NULL;
  }

  apm = (struct cras_apm*)calloc(1, sizeof(*apm));

  /* Configures APM to the format used by input device. If the channel
   * count is larger than stereo, use the standard channel count/layout
   * in APM. */
  apm->dev_fmt = *dev_fmt;
  apm->fmt = *dev_fmt;
  get_best_channels(&apm->fmt);

  // Reset detection of proper stereo
  apm->only_symmetric_content_in_render = true;
  apm->blocks_with_nonsymmetric_content_in_render = 0;
  apm->blocks_with_symmetric_content_in_render = 0;

  aec_applied_on_dsp = cras_iodev_get_rtc_proc_enabled(idev, RTC_PROC_AEC);
  ns_applied_on_dsp = cras_iodev_get_rtc_proc_enabled(idev, RTC_PROC_NS);
  agc_applied_on_dsp = cras_iodev_get_rtc_proc_enabled(idev, RTC_PROC_AGC);

  /* Determine whether to enforce effects to be on (regardless of settings
   * in the apm.ini file). */
  unsigned int enforce_aec_on = 0;
  if (stream->effects & APM_ECHO_CANCELLATION) {
    enforce_aec_on = !aec_applied_on_dsp;
  }
  unsigned int enforce_ns_on = 0;
  if (stream->effects & APM_NOISE_SUPRESSION) {
    enforce_ns_on = !ns_applied_on_dsp;
  }
  unsigned int enforce_agc_on = 0;
  if (stream->effects & APM_GAIN_CONTROL) {
    enforce_agc_on = !agc_applied_on_dsp;
  }

  /*
   * |aec_ini| and |apm_ini| are tuned specifically for the typical aec
   * use case, i.e when both audio input and output are internal devices.
   * Check for that before we use these settings, or just pass NULL so
   * the default generic settings are used.
   */
  const bool is_aec_use_case =
      cras_iodev_is_tuned_aec_use_case(idev->active_node) &&
      cras_apm_reverse_is_aec_use_case(stream->echo_ref);

  dictionary* aec_ini_use = is_aec_use_case ? aec_ini : NULL;
  dictionary* apm_ini_use = is_aec_use_case ? apm_ini : NULL;

  const size_t num_channels =
      MIN(apm->fmt.num_channels, WEBRTC_CHANNELS_SUPPORTED_MAX);

  apm->apm_ptr = webrtc_apm_create_with_enforced_effects(
      num_channels, apm->fmt.frame_rate, aec_ini_use, apm_ini_use,
      enforce_aec_on, enforce_ns_on, enforce_agc_on);
  if (apm->apm_ptr == NULL) {
    syslog(LOG_ERR,
           "Fail to create webrtc apm for ch %zu"
           " rate %zu effect %" PRIu64,
           dev_fmt->num_channels, dev_fmt->frame_rate, stream->effects);
    free(apm);
    return NULL;
  }

  apm->idev = idev;
  apm->work_queue = NULL;

  /* WebRTC APM wants 1/100 second equivalence of data(a block) to
   * process. Allocate buffer based on how many frames are in this block.
   */
  const int frame_length = apm->fmt.frame_rate / APM_NUM_BLOCKS_PER_SECOND;
  apm->buffer =
      byte_buffer_create(frame_length * cras_get_format_bytes(&apm->fmt));
  apm->fbuffer = float_buffer_create(frame_length, apm->fmt.num_channels);
  apm->area = cras_audio_area_create(apm->fmt.num_channels);

  bool nc_provided_by_ap =
      idev->active_node &&
      idev->active_node->nc_provider == CRAS_IONODE_NC_PROVIDER_AP;
  struct CrasProcessorConfig cfg = {
      // TODO(b/268276912): Removed hard-coded mono once we have multi-channel
      // AEC capture.
      .channels = 1,
      .block_size = frame_length,
      .frame_rate = apm->fmt.frame_rate,
      .effect = cras_processor_get_effect(nc_provided_by_ap),
  };
  apm->pp = cras_processor_create(&cfg);
  if (apm->pp == NULL) {
    // cras_processor_create should never fail.
    // If it ever fails, give up using the APM.
    // TODO: Add UMA about this failure.
    syslog(LOG_ERR, "cras_processor_create returned NULL");
    apm_destroy(&apm);
    return NULL;
  }

  /* TODO(hychao):remove mono_channel once we're ready for multi
   * channel capture process. */
  cras_audio_area_config_channels(apm->area, &mono_channel);

  DL_APPEND(stream->apms, apm);

  return apm;
}

void cras_stream_apm_start(struct cras_stream_apm* stream,
                           const struct cras_iodev* idev) {
  struct active_apm* active;
  struct cras_apm* apm;

  if (stream == NULL) {
    return;
  }

  // Check if this apm has already been started.
  apm = cras_stream_apm_get_active(stream, idev);
  if (apm) {
    return;
  }

  DL_SEARCH_SCALAR(stream->apms, apm, idev, idev);
  if (apm == NULL) {
    return;
  }

  active = (struct active_apm*)calloc(1, sizeof(*active));
  if (active == NULL) {
    syslog(LOG_ERR, "No memory to start apm.");
    return;
  }
  active->apm = apm;
  active->stream = stream;
  DL_APPEND(active_apms, active);

  cras_apm_reverse_state_update();
  update_supported_dsp_effects_activation();
  reconfigure_apm_vad();
}

void cras_stream_apm_stop(struct cras_stream_apm* stream,
                          struct cras_iodev* idev) {
  struct active_apm* active;

  if (stream == NULL) {
    return;
  }

  active = get_active_apm(stream, idev);
  if (active) {
    DL_DELETE(active_apms, active);
    free(active);
  }

  cras_apm_reverse_state_update();
  update_supported_dsp_effects_activation();

  /* If there's still an APM using |idev| at this moment, the above call
   * to update_supported_dsp_effects_activation has decided the final
   * state of DSP effects on |idev|.
   */
  DL_FOREACH (active_apms, active) {
    if (active->apm->idev == idev) {
      return;
    }
  }

  /* Otherwise |idev| is no longer being used by stream APMs. Deactivate
   * any effects that has been activated. */
  toggle_dsp_effect(idev, RTC_PROC_AEC, 0);
  toggle_dsp_effect(idev, RTC_PROC_NS, 0);
  toggle_dsp_effect(idev, RTC_PROC_AGC, 0);

  /* If the APM which causes AEC on DSP disallowed is the last active_apm
   * on |idev| to stop, update_supported_dsp_effects_activation cannot
   * detect the condition is released because there is no active_apm
   * applied on |idev| at that moment.
   * To solve such cases, send message to clear the disallowed state when
   * |idev| is no longer being used by APMs.
   */
  if (cras_iodev_support_rtc_proc_on_dsp(idev, RTC_PROC_AEC)) {
    send_disallow_aec_on_dsp_msg_from_info(idev->info.idx << 16);
  }
}

int cras_stream_apm_destroy(struct cras_stream_apm* stream) {
  struct cras_apm* apm;

  // Unlink any linked echo ref.
  cras_apm_reverse_link_echo_ref(stream, NULL);

  DL_FOREACH (stream->apms, apm) {
    DL_DELETE(stream->apms, apm);
    apm_destroy(&apm);
  }
  free(stream);

  return 0;
}

// See comments for process_reverse_t
static int process_reverse(struct float_buffer* fbuf,
                           unsigned int frame_rate,
                           const struct cras_iodev* echo_ref) {
  struct active_apm* active;
  int ret;
  float* const* rp;
  unsigned int unused;

  // Caller side ensures fbuf is full and hasn't been read at all.
  rp = float_buffer_read_pointer(fbuf, 0, &unused);

  DL_FOREACH (active_apms, active) {
    if (!(active->stream->effects & APM_ECHO_CANCELLATION)) {
      continue;
    }

    /* Client could assign specific echo ref to an APM. If the
     * running echo_ref doesn't match then do nothing. */
    if (active->stream->echo_ref && (active->stream->echo_ref != echo_ref)) {
      continue;
    }

    if (active->apm->only_symmetric_content_in_render) {
      bool symmetric_content = left_and_right_channels_are_symmetric(
          fbuf->num_channels, frame_rate, rp);

      int non_sym_frames =
          active->apm->blocks_with_nonsymmetric_content_in_render;
      int sym_frames = active->apm->blocks_with_symmetric_content_in_render;

      /* Count number of consecutive frames with symmetric
         and non-symmetric content. */
      non_sym_frames = symmetric_content ? 0 : non_sym_frames + 1;
      sym_frames = symmetric_content ? sym_frames + 1 : 0;

      if (non_sym_frames > 2 * APM_NUM_BLOCKS_PER_SECOND) {
        /* Only flag render content to be non-symmetric if it has
   been non-symmetric for at least 2 seconds. */

        active->apm->only_symmetric_content_in_render = false;
      } else if (sym_frames > 5 * 60 * APM_NUM_BLOCKS_PER_SECOND) {
        /* Fall-back to consider render content as symmetric if it has
     been symmetric for 5 minutes. */
        active->apm->only_symmetric_content_in_render = false;
      }

      active->apm->blocks_with_nonsymmetric_content_in_render = non_sym_frames;
      active->apm->blocks_with_symmetric_content_in_render = sym_frames;
    }
    int num_unique_channels =
        active->apm->only_symmetric_content_in_render ? 1 : fbuf->num_channels;

    ret = webrtc_apm_process_reverse_stream_f(
        active->apm->apm_ptr, num_unique_channels, frame_rate, rp);
    if (ret) {
      syslog(LOG_ERR, "APM process reverse err");
      return ret;
    }
  }
  return 0;
}

/*
 * When APM reverse module has state changes, this callback function is called
 * to ask stream APMs if there's need to process data on the reverse side.
 * This is expected to be called from cras_apm_reverse_state_update() in
 * audio thread so it's safe to access |active_apms|.
 * Args:
 *     default_reverse - True means |echo_ref| is the default reverse module
 *         provided by the system default audio output device.
 *     echo_ref - The device to check if reverse data processing is needed.
 * Returns:
 *     If |echo_ref| should be processed as reverse data for a subset of
 *     active apms.
 */
static int process_reverse_needed(bool default_reverse,
                                  const struct cras_iodev* echo_ref) {
  struct active_apm* active;

  DL_FOREACH (active_apms, active) {
    // No processing need when APM doesn't ask for AEC.
    if (!(active->stream->effects & APM_ECHO_CANCELLATION)) {
      continue;
    }
    // APM with NULL echo_ref means it tracks default.
    if (default_reverse && (active->stream->echo_ref == NULL)) {
      return 1;
    }
    // APM asked to track given echo_ref specifically.
    if (echo_ref && (active->stream->echo_ref == echo_ref)) {
      return 1;
    }
  }
  return 0;
}

static void get_aec_ini(const char* config_dir) {
  snprintf(ini_name, MAX_INI_NAME_LENGTH, "%s/%s", config_dir, AEC_CONFIG_NAME);
  ini_name[MAX_INI_NAME_LENGTH] = '\0';

  if (aec_ini) {
    iniparser_freedict(aec_ini);
    aec_ini = NULL;
  }
  aec_ini = iniparser_load_wrapper(ini_name);
  if (aec_ini == NULL) {
    syslog(LOG_INFO, "No aec ini file %s", ini_name);
  }

  if (0 == iniparser_getnsec(aec_ini)) {
    iniparser_freedict(aec_ini);
    aec_ini = NULL;
    syslog(LOG_INFO, "Empty aec ini file %s", ini_name);
  }
}

static void get_apm_ini(const char* config_dir) {
  snprintf(ini_name, MAX_INI_NAME_LENGTH, "%s/%s", config_dir, APM_CONFIG_NAME);
  ini_name[MAX_INI_NAME_LENGTH] = '\0';

  if (apm_ini) {
    iniparser_freedict(apm_ini);
    apm_ini = NULL;
  }
  apm_ini = iniparser_load_wrapper(ini_name);
  if (apm_ini == NULL) {
    syslog(LOG_INFO, "No apm ini file %s", ini_name);
  }

  if (0 == iniparser_getnsec(apm_ini)) {
    iniparser_freedict(apm_ini);
    apm_ini = NULL;
    syslog(LOG_INFO, "Empty apm ini file %s", ini_name);
  }
}

static int send_apm_message_explicit(enum APM_THREAD_CMD cmd, void* data1) {
  struct apm_message msg;
  msg.cmd = cmd;
  msg.data1 = data1;
  return write(to_thread_fds[1], &msg, sizeof(msg));
}

static int send_apm_message(enum APM_THREAD_CMD cmd) {
  return send_apm_message_explicit(cmd, NULL);
}

/* Triggered in main thread when devices state has changed in APM
 * reverse modules. */
static void on_output_devices_changed() {
  int rc;
  /* Send a message to audio thread because we need to access
   * |active_apms|. */
  rc = send_apm_message(APM_REVERSE_DEV_CHANGED);
  if (rc < 0) {
    syslog(LOG_ERR, "Error sending output devices changed message");
  }

  /* The output device has just changed so for all stream_apms the
   * settings might need to change accordingly. Call out to iodev_list
   * for reconnecting those streams and apply the correct settings.
   */
  cras_iodev_list_reconnect_streams_with_apm();
}

// Receives commands and handles them in audio thread.
static int apm_thread_callback(void* arg, int revents) {
  struct apm_message msg;
  int rc;

  if (revents & (POLLERR | POLLHUP)) {
    syslog(LOG_ERR, "Error polling APM message sockect");
    goto read_write_err;
  }

  if (revents & POLLIN) {
    rc = read(to_thread_fds[0], &msg, sizeof(msg));
    if (rc <= 0) {
      syslog(LOG_ERR, "Read APM message error");
      goto read_write_err;
    }
    switch (msg.cmd) {
      case APM_REVERSE_DEV_CHANGED:
      case APM_SET_AEC_REF:
        cras_apm_reverse_state_update();
        update_supported_dsp_effects_activation();
        break;
      case APM_VAD_TARGET_CHANGED:
        update_vad_target(msg.data1);
        break;
      default:
        break;
    }
  }

  return 0;

read_write_err:
  audio_thread_rm_callback(to_thread_fds[0]);
  return 0;
}

static void possibly_track_voice_activity(struct cras_apm* apm) {
  if (!cached_vad_target) {
    return;
  }

  struct active_apm* active;
  DL_FOREACH (active_apms, active) {
    // Match only the first apm. We don't care mutiple inputs.
    if (active->stream->apms != apm) {
      continue;
    }

    if (active->stream != cached_vad_target) {
      continue;
    }

    int rc = cras_speak_on_mute_detector_add_voice_activity(
        webrtc_apm_get_voice_detected(apm->apm_ptr));
    if (rc < 0) {
      syslog(LOG_ERR, "failed to send speak on mute message: %s",
             cras_strerror(-rc));
    }
    return;
  }
}

int cras_stream_apm_init(const char* device_config_dir) {
  static const char* cras_apm_metrics_prefix = "Cras.";
  int rc;

  aec_config_dir = device_config_dir;
  get_aec_ini(aec_config_dir);
  get_apm_ini(aec_config_dir);
  webrtc_apm_init_metrics(cras_apm_metrics_prefix);

  rc = pipe(to_thread_fds);
  if (rc < 0) {
    syslog(LOG_ERR, "Failed to pipe");
    return rc;
  }

  audio_thread_add_events_callback(to_thread_fds[0], apm_thread_callback, NULL,
                                   POLLIN | POLLERR | POLLHUP);

  return cras_apm_reverse_init(process_reverse, process_reverse_needed,
                               on_output_devices_changed);
}

void cras_stream_apm_reload_aec_config() {
  if (NULL == aec_config_dir) {
    return;
  }

  get_aec_ini(aec_config_dir);
  get_apm_ini(aec_config_dir);

  // Dump the config content at reload only, for debug.
  webrtc_apm_dump_configs(apm_ini, aec_ini);
}

int cras_stream_apm_deinit() {
  cras_apm_reverse_deinit();
  audio_thread_rm_callback_sync(cras_iodev_list_get_audio_thread(),
                                to_thread_fds[0]);
  if (to_thread_fds[0] != -1) {
    close(to_thread_fds[0]);
    close(to_thread_fds[1]);
  }
  return 0;
}

// Clamp the value between -1 and +1.
static inline float clamp1(float value) {
  return value < -1 ? -1 : (value > 1 ? 1 : value);
}

int cras_stream_apm_process(struct cras_apm* apm,
                            struct float_buffer* input,
                            unsigned int offset,
                            float preprocessing_gain_scalar) {
  unsigned int writable, nframes, nread;
  int ch, i, j, ret;
  size_t num_channels;
  float* const* wp;
  float* const* rp;

  nread = float_buffer_level(input);
  if (nread < offset) {
    syslog(LOG_ERR, "Process offset exceeds read level");
    return -EINVAL;
  }

  writable = float_buffer_writable(apm->fbuffer);
  writable = MIN(nread - offset, writable);

  // Read from shared fbuffer and apply gain
  nframes = writable;
  while (nframes) {
    nread = nframes;
    wp = float_buffer_write_pointer(apm->fbuffer);
    rp = float_buffer_read_pointer(input, offset, &nread);

    for (i = 0; i < apm->fbuffer->num_channels; i++) {
      /* Look up the channel position and copy from
       * the correct index of |input| buffer.
       */
      for (ch = 0; ch < CRAS_CH_MAX; ch++) {
        if (apm->fmt.channel_layout[ch] == i) {
          break;
        }
      }
      if (ch == CRAS_CH_MAX) {
        continue;
      }

      j = apm->dev_fmt.channel_layout[ch];
      if (j == -1) {
        continue;
      }

      for (int f = 0; f < nread; f++) {
        wp[i][f] = clamp1(rp[j][f] * preprocessing_gain_scalar);
      }
    }

    nframes -= nread;
    offset += nread;

    float_buffer_written(apm->fbuffer, nread);
  }

  // process and move to int buffer
  if ((float_buffer_writable(apm->fbuffer) == 0) &&
      (buf_queued(apm->buffer) == 0)) {
    nread = float_buffer_level(apm->fbuffer);
    rp = float_buffer_read_pointer(apm->fbuffer, 0, &nread);
    num_channels = MIN(apm->fmt.num_channels, WEBRTC_CHANNELS_SUPPORTED_MAX);
    ret = webrtc_apm_process_stream_f(apm->apm_ptr, num_channels,
                                      apm->fmt.frame_rate, rp);
    if (ret) {
      syslog(LOG_ERR, "APM process stream f err");
      return ret;
    }

    possibly_track_voice_activity(apm);

    // Process audio with cras_processor.
    struct multi_slice input = {
        // TODO(b/268276912): Removed hard-coded mono once we have multi-channel
        // AEC capture.
        .channels = 1,
        .num_frames = nread,
    };
    struct multi_slice output = {};
    for (int ch = 0; ch < input.channels; ch++) {
      input.data[ch] = rp[ch];
    }
    enum status st = apm->pp->ops->run(apm->pp, &input, &output);
    if (st != StatusOk) {
      syslog(LOG_ERR, "cras_processor run failed");
      return -ENOTRECOVERABLE;
    }

    /* We configure APM for N-ch input to 1-ch output processing
     * and that has the side effect that the rest of channels are
     * filled with the unprocessed content from hardware mic.
     * Overwrite it with the processed data from first channel to
     * avoid leaking it later.
     * TODO(hychao): remove this when we're ready for multi channel
     * capture process.
     */
    assert(output.channels == 1);
    assert(output.num_frames == nread);
    for (ch = 0; ch < apm->fbuffer->num_channels; ch++) {
      // TODO(aaronyu): audio_processor does not guarantee that the output and
      // the input don't overlap. It's better to call dsp_util_interleave
      // on `output.data`, instead of copying data back to `rp`.
      memcpy(rp[ch], output.data[0], nread * sizeof(float));
    }

    dsp_util_interleave(rp, buf_write_pointer(apm->buffer),
                        apm->fbuffer->num_channels, apm->fmt.format, nread);
    buf_increment_write(apm->buffer, nread * cras_get_format_bytes(&apm->fmt));
    float_buffer_reset(apm->fbuffer);
  }

  return writable;
}

struct cras_audio_area* cras_stream_apm_get_processed(struct cras_apm* apm) {
  uint8_t* buf_ptr;

  buf_ptr = buf_read_pointer_size(apm->buffer, &apm->area->frames);
  apm->area->frames /= cras_get_format_bytes(&apm->fmt);
  cras_audio_area_config_buf_pointers(apm->area, &apm->fmt, buf_ptr);
  return apm->area;
}

void cras_stream_apm_put_processed(struct cras_apm* apm, unsigned int frames) {
  buf_increment_read(apm->buffer, frames * cras_get_format_bytes(&apm->fmt));
}

struct cras_audio_format* cras_stream_apm_get_format(struct cras_apm* apm) {
  return &apm->fmt;
}

bool cras_stream_apm_get_use_tuned_settings(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev) {
  struct active_apm* active = get_active_apm(stream, idev);
  if (active == NULL) {
    return false;
  }

  /* If input and output devices in AEC use case, plus that a
   * tuned setting is provided. */
  return cras_iodev_is_tuned_aec_use_case(idev->active_node) &&
         cras_apm_reverse_is_aec_use_case(stream->echo_ref) &&
         (aec_ini || apm_ini);
}

void cras_stream_apm_set_aec_dump(struct cras_stream_apm* stream,
                                  const struct cras_iodev* idev,
                                  int start,
                                  int fd) {
  struct cras_apm* apm;
  char file_name[256];
  int rc;
  FILE* handle;

  DL_SEARCH_SCALAR(stream->apms, apm, idev, idev);
  if (apm == NULL) {
    return;
  }

  if (start) {
    handle = fdopen(fd, "w");
    if (handle == NULL) {
      syslog(LOG_WARNING, "Create dump handle fail, errno %d", errno);
      return;
    }
    // webrtc apm will own the FILE handle and close it.
    rc = webrtc_apm_aec_dump(apm->apm_ptr, &apm->work_queue, start, handle);
    if (rc) {
      syslog(LOG_WARNING, "Fail to dump debug file %s, rc %d", file_name, rc);
    }
  } else {
    rc = webrtc_apm_aec_dump(apm->apm_ptr, &apm->work_queue, 0, NULL);
    if (rc) {
      syslog(LOG_WARNING, "Failed to stop apm debug, rc %d", rc);
    }
  }
}

int cras_stream_apm_set_aec_ref(struct cras_stream_apm* stream,
                                struct cras_iodev* echo_ref) {
  int rc;

  // Do nothing if this is a duplicate call from client.
  if (stream->echo_ref == echo_ref) {
    return 0;
  }

  stream->echo_ref = echo_ref;

  rc = cras_apm_reverse_link_echo_ref(stream, stream->echo_ref);
  if (rc) {
    syslog(LOG_ERR, "Failed to add echo ref for set aec ref call");
    return rc;
  }

  rc = send_apm_message(APM_SET_AEC_REF);
  if (rc < 0) {
    syslog(LOG_ERR, "Error sending set aec ref message.");
    return rc;
  }
  return 0;
}

void cras_stream_apm_notify_vad_target_changed(
    struct cras_stream_apm* vad_target) {
  int rc = send_apm_message_explicit(APM_VAD_TARGET_CHANGED, vad_target);
  if (rc < 0) {
    syslog(LOG_ERR, "Error sending vad target changed message");
  }
}

static void handle_stream_apm_message(struct cras_main_message* msg,
                                      void* arg) {
  struct cras_stream_apm_message* stream_apm_msg =
      (struct cras_stream_apm_message*)msg;

  switch (stream_apm_msg->message_type) {
    case APM_DISALLOW_AEC_ON_DSP:
      cras_iodev_list_set_aec_on_dsp_is_disallowed(
          stream_apm_msg->arg1,   // dev_idx
          stream_apm_msg->arg2);  // is_disallowed
      break;
    default:
      syslog(LOG_ERR, "Unknown stream apm message type %u",
             stream_apm_msg->message_type);
      break;
  }
}

int cras_stream_apm_message_handler_init() {
  return cras_main_message_add_handler(CRAS_MAIN_STREAM_APM,
                                       handle_stream_apm_message, NULL);
}
