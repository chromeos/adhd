/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_stream_apm.h"

#include <inttypes.h>
#include <percetto.h>
#include <string.h>
#include <syslog.h>

#include "audio_processor/c/plugin_processor.h"
#include "cras/common/check.h"
#include "cras/common/rust_common.h"
#include "cras/server/cras_thread.h"
#include "cras/server/cras_trace.h"
#include "cras/server/main_message.h"
#include "cras/server/platform/features/features.h"
#include "cras/server/processor/processor.h"
#include "cras/server/s2/s2.h"
#include "cras/src/common/byte_buffer.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/common/dumper.h"
#include "cras/src/dsp/dsp_util.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_apm_reverse.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_speak_on_mute_detector.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/float_buffer.h"
#include "cras/src/server/iniparser_wrapper.h"
#include "cras_audio_format.h"
#include "third_party/utlist/utlist.h"
#include "webrtc_apm/webrtc_apm.h"

#define AEC_CONFIG_NAME "aec.ini"
#define APM_CONFIG_NAME "apm.ini"

// Information of APM.
struct cras_apm_state {
  // Number of streams enabling Noise Cancellation.
  unsigned num_nc;
  // The last time Noise Cancellation was closed.
  struct timespec last_nc_closed;
} apm_state = {};

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
  // An APM instance from libwebrtc_audio_processing.
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
  // The audio processor pipeline which runs various effects including APM.
  // If the APM is created successfully, cras_processor is always non-NULL.
  struct plugin_processor* cras_processor;
  // The active effect of plugin_processor cras_processor.
  enum CrasProcessorEffect cras_processor_effect;
  // Processor that wraps webrtc_apm.
  struct plugin_processor webrtc_apm_wrapper_processor;
  // The time when the apm started.
  struct timespec start_ts;
  struct cras_apm *prev, *next;
  // Indicate if AEC dump is active on this APM. If this APM is
  // stopped while AEC dump is active, the dump will be stopped.
  bool aec_dump_active;
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
  // The effects bit map of APM.
  uint64_t effects;
  // List of APMs for stream processing. It is a list because
  // multiple input devices could be configured by user.
  struct cras_apm* apms;
  // If specified, the pointer to an output iodev which shall be
  // used as echo ref for this apm. When set to NULL it means to
  // follow what the default_rmod provides as echo ref.
  struct cras_iodev* echo_ref;
  // Indicate if AEC dump is running or not. AEC dump can be
  // started and stopped via cras_stream_apm_set_aec_dump.
  bool aec_dump_enabled;
  // fd to the AEC dump file if it is running.
  int aec_dump_fd;
  // Reference to the APM with AEC dump active.
  struct cras_apm* aec_dump_active_apm;
};

static struct actx_apm {
  /*
   * Wrappers of APM instances that are active, which means it is associated
   * to a dev/stream pair in audio thread and ready for processing.
   * The existence of an |active_apm| is the key to treat a |cras_apm| is alive
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
} ACTX_APM;

// Commands sent to be handled in main thread.
enum CRAS_STREAM_APM_MSG_TYPE {
  // Only having this here to keep the enum.
  // Remove this when adding the next msg type.
  APM_DISALLOW_AEC_ON_DSP_B301535557_UNUSED,
};

struct cras_stream_apm_message {
  struct cras_main_message header;
  enum CRAS_STREAM_APM_MSG_TYPE message_type;
  uint32_t arg1;
  uint32_t arg2;
};

// Commands from main thread to be handled in audio thread.
enum APM_THREAD_CMD {
  APM_REVERSE_DEV_CHANGED,
  APM_SET_AEC_REF,
  APM_VAD_TARGET_CHANGED,
  APM_DSP_INPUT_EFFECTS_BLOCKED,
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

static struct cras_apm* get_apm_wrapper_processor(struct plugin_processor* p) {
  return (struct cras_apm*)((char*)p - offsetof(struct cras_apm,
                                                webrtc_apm_wrapper_processor));
}

static enum status apm_wrapper_processor_run(struct plugin_processor* p,
                                             const struct multi_slice* input,
                                             struct multi_slice* output) {
  TRACE_EVENT(audio, __func__);

  struct cras_apm* apm = get_apm_wrapper_processor(p);
  webrtc_apm_process_stream_f(apm->apm_ptr, input->channels,
                              apm->fmt.frame_rate, input->data);
  *output = *input;
  return StatusOk;
}

static enum status apm_wrapper_processor_destroy(struct plugin_processor* p) {
  // Do nothing.
  // Destruction handled by cras_apm, not plugin_processor.
  return StatusOk;
}

static enum status apm_wrapper_processor_get_output_frame_rate(
    struct plugin_processor* p,
    size_t* output_frame_rate) {
  struct cras_apm* apm = get_apm_wrapper_processor(p);
  *output_frame_rate = apm->fmt.frame_rate;
  return StatusOk;
}

static const struct plugin_processor_ops apm_wrapper_processor_ops = {
    .run = apm_wrapper_processor_run,
    .destroy = apm_wrapper_processor_destroy,
    .get_output_frame_rate = apm_wrapper_processor_get_output_frame_rate,
};

static bool stream_apm_should_enable_vad(struct cras_stream_apm* stream_apm) {
  /* There is no stream_apm->effects bit allocated for client VAD
   * usage. Determine whether VAD should be enabled solely by the
   * requirements of speak-on-mute detection. */
  return cached_vad_target == stream_apm;
}

static void update_supported_dsp_effects_activation(
    struct cras_audio_ctx* actx);

static bool dsp_input_effects_blocked = false;

void apm_thread_set_dsp_input_effects_blocked(bool blocked) {
  struct cras_audio_ctx* actx = checked_audio_ctx();

  if (dsp_input_effects_blocked == blocked) {
    return;
  }
  dsp_input_effects_blocked = blocked;
  update_supported_dsp_effects_activation(actx);
}

/*
 * Analyzes the active APMs on the idev and returns whether any of them
 * cause a conflict to enabling DSP |effect| on |idev|.
 */
static bool dsp_effect_check_conflict(struct cras_iodev* const idev,
                                      enum RTC_PROC_ON_DSP effect) {
  return dsp_input_effects_blocked ||
         !cras_iodev_is_dsp_aec_use_case(idev->active_node);
}

/*
 * Analyzes the active APMs and returns whether the effect is active in any of
 * them.
 */
static bool effect_needed_for_dev(struct cras_audio_ctx* actx,
                                  const struct cras_iodev* const idev,
                                  uint64_t effect) {
  struct active_apm* active;
  struct cras_apm* apm;
  DL_FOREACH (actx->apm->active_apms, active) {
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
 * Iterates all active apms and applies the restrictions to determine
 * whether or not to activate effects on DSP for each associated
 * input devices.
 */
static void update_supported_dsp_effects_activation(
    struct cras_audio_ctx* actx) {
  /*
   * DSP effect restriction rules to follow in order.
   *
   * Note that there could be more than one APMs attach to the same idev
   * that request different effects. Therefore we need to iterate through
   * all APMs to find out the answer to each rule.
   *
   * 1. If the associated input and output device pair don't align with
   * the AEC use case, we shall deactivate DSP effects on idev.
   * 2. We shall deactivate DSP effects on idev if any APM has requested
   * to not be applied.
   * 3. We shall activate DSP effects on idev if there's at least one APM
   * requesting it.
   */

  // Toggle between having effects applied on DSP and in CRAS for each APM
  struct active_apm* active;
  struct cras_apm* apm;
  LL_FOREACH (actx->apm->active_apms, active) {
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
    bool aec_needed = effect_needed_for_dev(actx, idev, APM_ECHO_CANCELLATION);
    bool ns_needed = effect_needed_for_dev(actx, idev, APM_NOISE_SUPRESSION);
    bool agc_needed = effect_needed_for_dev(actx, idev, APM_GAIN_CONTROL);

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
  }
}

// Reconfigure APMs to update their VAD enabled status.
static void reconfigure_apm_vad(struct cras_audio_ctx* actx) {
  struct active_apm* active;
  LL_FOREACH (actx->apm->active_apms, active) {
    webrtc_apm_enable_vad(active->apm->apm_ptr,
                          stream_apm_should_enable_vad(active->stream));
  }
}

// Set the VAD target to new_vad_target and propagate the changes to APMs.
static void update_vad_target(struct cras_audio_ctx* actx,
                              struct cras_stream_apm* new_vad_target) {
  cached_vad_target = new_vad_target;
  reconfigure_apm_vad(actx);
}

static void apm_destroy(struct cras_apm** apm) {
  if (*apm == NULL) {
    return;
  }

  if ((*apm)->cras_processor) {
    (*apm)->cras_processor->ops->destroy((*apm)->cras_processor);
  }

  byte_buffer_destroy(&(*apm)->buffer);
  float_buffer_destroy(&(*apm)->fbuffer);
  cras_audio_area_destroy((*apm)->area);

  // Any unfinished AEC dump handle will be closed.
  webrtc_apm_destroy((*apm)->apm_ptr);
  free(*apm);
  *apm = NULL;
}

static inline bool apm_needed_for_effects(uint64_t effects,
                                          bool cras_processor_needed) {
  if (effects &
      (APM_ECHO_CANCELLATION | APM_NOISE_SUPRESSION | APM_GAIN_CONTROL)) {
    // Required for webrtc-apm.
    return true;
  }
  if (cras_processor_needed) {
    return true;
  }
  if (cras_processor_is_override_enabled()) {
    return true;
  }
  return false;
}

// Start AEC dump for the APM. Caller must ensure that the stream contains the
// APM.
void possibly_start_apm_aec_dump(struct cras_stream_apm* stream,
                                 struct cras_apm* apm) {
  if (!stream->aec_dump_enabled) {
    return;
  }
  if (stream->aec_dump_active_apm) {
    return;
  }

  // Create or append to the dump file fd.
  //
  // aecdump format is appendable. It consists of events in the
  // third_party/webrtc/modules/audio_processing/debug.proto file.
  //
  // Example of aecdump file:
  //  [CONFIG] [INIT] [REVERSE_STREAM] [STREAM] [REVERSE_STREAM] [STREAM] ...
  //
  // unpack_aecdump read through the events. It will create a new set of wave
  // files on INIT event, and append REVERSE_STREAM and STREAM data to them.
  //
  // Appended aecdump content will be in different wave files due to the INIT
  // event. The frame counter will be continued from the previous aecdump
  // instead of starting from 0, but the data will be correct.
  FILE* handle = fdopen(dup(stream->aec_dump_fd), "w");
  if (handle == NULL) {
    syslog(LOG_WARNING, "Create dump handle failed, errno %d", errno);
    return;
  }

  // webrtc apm will own the FILE handle and close it.
  int rc = webrtc_apm_aec_dump(apm->apm_ptr, &apm->work_queue, 1, handle);
  if (rc) {
    syslog(LOG_WARNING, "Start apm aec dump failed, rc %d", rc);
  }

  apm->aec_dump_active = true;
  stream->aec_dump_active_apm = apm;
}

// Stop AEC dump for the APM. Caller must ensure that the stream contains the
// APM.
void possibly_stop_apm_aec_dump(struct cras_stream_apm* stream,
                                struct cras_apm* apm) {
  if (!apm->aec_dump_active) {
    return;
  }

  int rc = webrtc_apm_aec_dump(apm->apm_ptr, &apm->work_queue, 0, NULL);
  if (rc) {
    syslog(LOG_WARNING, "Stop apm aec dump failed, rc %d", rc);
  }

  apm->aec_dump_active = false;
  stream->aec_dump_active_apm = NULL;
}

struct cras_stream_apm* cras_stream_apm_create(uint64_t effects) {
  if (!apm_needed_for_effects(
          effects,
          // Assume the stream may need cras_processor.
          // Whether a stream need NC, and by extension cras_processor
          // cannot be known at stream creation as the active device
          // may change (from not support NC to support NC), or the user
          // may touch the platform NC toggle.
          /*cras_processor_needed=*/true) &&
      // If effects is non-zero, create a cras_stream_apm to store the bits.
      !effects) {
    return NULL;
  }

  struct cras_stream_apm* stream =
      (struct cras_stream_apm*)calloc(1, sizeof(*stream));
  if (stream == NULL) {
    syslog(LOG_ERR, "No memory in creating stream apm");
    return NULL;
  }
  stream->effects = effects;
  stream->apms = NULL;
  stream->echo_ref = NULL;
  stream->aec_dump_enabled = false;
  stream->aec_dump_fd = -1;
  stream->aec_dump_active_apm = NULL;

  return stream;
}

static struct active_apm* get_active_apm(struct cras_audio_ctx* actx,
                                         struct cras_stream_apm* stream,
                                         const struct cras_iodev* idev) {
  struct active_apm* active;

  DL_FOREACH (actx->apm->active_apms, active) {
    if ((active->apm->idev == idev) && (active->stream == stream)) {
      return active;
    }
  }
  return NULL;
}

static struct cras_apm* find_active_apm(struct cras_audio_ctx* actx,
                                        struct cras_stream_apm* stream) {
  if (stream == NULL) {
    return NULL;
  }

  struct active_apm* active;

  DL_FOREACH (actx->apm->active_apms, active) {
    if (active->stream == stream) {
      return active->apm;
    }
  }
  return NULL;
}

struct cras_apm* cras_stream_apm_get_active(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev) {
  struct cras_audio_ctx* actx = checked_audio_ctx();

  struct active_apm* active = get_active_apm(actx, stream, idev);
  return active ? active->apm : NULL;
}

uint64_t cras_stream_apm_get_effects(struct cras_stream_apm* stream) {
  if (stream == NULL) {
    return 0;
  } else {
    return stream->effects;
  }
}

static CRAS_STREAM_ACTIVE_AP_EFFECT get_active_ap_effects(
    struct cras_apm* apm) {
  CRAS_STREAM_ACTIVE_AP_EFFECT effects = 0;

  if (apm->apm_ptr) {
    struct WebRtcApmActiveEffects webrtc_effects =
        webrtc_apm_get_active_effects(apm->apm_ptr);
    if (webrtc_effects.echo_cancellation) {
      effects |= CRAS_STREAM_ACTIVE_AP_EFFECT_ECHO_CANCELLATION;
    }
    if (webrtc_effects.noise_suppression) {
      effects |= CRAS_STREAM_ACTIVE_AP_EFFECT_NOISE_SUPPRESSION;
    }
    if (webrtc_effects.voice_activity_detection) {
      effects |= CRAS_STREAM_ACTIVE_AP_EFFECT_VOICE_ACTIVITY_DETECTION;
    }
  }

  effects |=
      cras_processor_effect_to_active_ap_effects(apm->cras_processor_effect);

  return effects;
}

void cras_stream_apm_remove(struct cras_stream_apm* stream,
                            const struct cras_iodev* idev) {
  struct cras_apm* apm;

  DL_FOREACH (stream->apms, apm) {
    if (apm->idev == idev) {
      DL_DELETE(stream->apms, apm);
      if (stream->aec_dump_active_apm == apm) {
        stream->aec_dump_active_apm = NULL;
      }
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

// Returns the format that APM should use given a device format.
static struct cras_audio_format get_best_channels(
    const struct cras_audio_format* dev_fmt,
    int channel_limit) {
  struct cras_audio_format apm_fmt = {
      .format = dev_fmt->format,
      .frame_rate = dev_fmt->frame_rate,
  };

  for (int ch = 0; ch < CRAS_CH_MAX; ch++) {
    apm_fmt.channel_layout[ch] = -1;
  }

  if (channel_limit > MULTI_SLICE_MAX_CH) {
    channel_limit = MULTI_SLICE_MAX_CH;
  }

  for (int ch = CRAS_CH_FL;
       ch < CRAS_CH_MAX && apm_fmt.num_channels < channel_limit; ch++) {
    if (dev_fmt->channel_layout[ch] != -1) {
      apm_fmt.channel_layout[ch] = apm_fmt.num_channels++;
    }
  }

  return apm_fmt;
}

static size_t get_aec3_fixed_capture_delay_samples() {
  if (str_equals("zork", cras_system_get_board_name())) {
    return 320;
  }
  if (cras_feature_enabled(CrOSLateBootCrasAecFixedCaptureDelay320Samples)) {
    return 320;
  }
  return 0;
}

struct cras_apm* cras_stream_apm_add(
    struct cras_stream_apm* stream,
    struct cras_iodev* idev,
    const struct cras_audio_format* dev_fmt,
    const struct cras_audio_format* stream_fmt) {
  struct cras_apm* apm;
  bool aec_applied_on_dsp = false;
  bool ns_applied_on_dsp = false;
  bool agc_applied_on_dsp = false;

  DL_FOREACH (stream->apms, apm) {
    if (apm->idev == idev) {
      return apm;
    }
  }

  CRAS_NC_PROVIDER nc_providers = (idev->active_node)
                                      ? idev->active_node->nc_providers
                                      : CRAS_NC_PROVIDER_NONE;
  bool client_controlled = stream->effects & CLIENT_CONTROLLED_VOICE_ISOLATION;
  bool client_enabled = stream->effects & VOICE_ISOLATION;
  enum CrasProcessorEffect cp_effect = cras_s2_get_cras_processor_effect(
      nc_providers, client_controlled, client_enabled);
  char* nc_providers_str = cras_nc_providers_bitset_to_str(nc_providers);
  syslog(LOG_DEBUG,
         "idev: %s, Effect: %s, compatible NC providers: %s, voice isolation "
         "(client_controlled, client_enabled): (%s, %s)",
         idev->info.name, cras_processor_effect_to_str(cp_effect),
         nc_providers_str, client_controlled ? "true" : "false",
         client_enabled ? "true" : "false");
  cras_rust_free_string(nc_providers_str);

  // TODO(hychao): Remove the check when we enable more effects.
  if (!apm_needed_for_effects(
          stream->effects, /*cras_processor_needed=*/cp_effect != NoEffects)) {
    return NULL;
  }

  apm = (struct cras_apm*)calloc(1, sizeof(*apm));

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

  // Configure APM to the format used by input device.
  // If beamforming is not in use, limit the channel count to the stream
  // channel count to reduce AEC complexity.
  apm->dev_fmt = *dev_fmt;
  int channel_limit = INT_MAX;
  if (enforce_aec_on && cp_effect != Beamforming) {
    channel_limit = stream_fmt->num_channels;
  }
  apm->fmt = get_best_channels(dev_fmt, channel_limit);

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

  const struct WebRtcApmConfig webrtc_apm_config = {
      .enforce_aec_on = enforce_aec_on,
      .enforce_ns_on = enforce_ns_on,
      .enforce_agc_on = enforce_agc_on,
      .aec3_fixed_capture_delay_samples =
          get_aec3_fixed_capture_delay_samples(),
  };
  apm->apm_ptr = webrtc_apm_create_with_enforced_effects(
      apm->fmt.num_channels, apm->fmt.frame_rate, aec_ini_use, apm_ini_use,
      &webrtc_apm_config);
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
  cras_audio_area_config_channels(apm->area, &apm->fmt);

  apm->webrtc_apm_wrapper_processor.ops = &apm_wrapper_processor_ops;
  struct CrasProcessorConfig cfg = {
      .channels = apm->fmt.num_channels,
      .block_size = frame_length,
      .frame_rate = apm->fmt.frame_rate,
      .effect = cp_effect,
      .wrap_mode =
          cp_effect != NoEffects &&
                  cras_feature_enabled(CrOSLateBootCrasProcessorDedicatedThread)
              ? WrapModeDedicatedThread
              : WrapModeNone,
      .wav_dump = cras_feature_enabled(CrOSLateBootCrasProcessorWavDump),
  };
  struct CrasProcessorCreateResult cras_processor_create_result =
      cras_processor_create(&cfg, &apm->webrtc_apm_wrapper_processor);
  if (cras_processor_create_result.plugin_processor == NULL) {
    // cras_processor_create should never fail.
    // If it ever fails, give up using the APM.
    // TODO: Add UMA about this failure.
    syslog(LOG_ERR, "cras_processor_create returned NULL");
    apm_destroy(&apm);
    return NULL;
  }
  if (cp_effect != NoEffects) {
    const bool success = cras_processor_create_result.effect == cp_effect;
    switch (cp_effect) {
      case NoiseCancellation:
        cras_server_metrics_ap_nc_start_status(success);
        break;
      case StyleTransfer:
        cras_server_metrics_ast_start_status(success);
        break;
      default:
        break;
    }
    apm_state.num_nc++;
  }
  apm->cras_processor = cras_processor_create_result.plugin_processor;
  apm->cras_processor_effect = cras_processor_create_result.effect;

  DL_APPEND(stream->apms, apm);

  return apm;
}

void cras_stream_apm_start(struct cras_stream_apm* stream,
                           const struct cras_iodev* idev) {
  struct cras_audio_ctx* actx = checked_audio_ctx();

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
  DL_APPEND(actx->apm->active_apms, active);

  clock_gettime(CLOCK_MONOTONIC_RAW, &apm->start_ts);

  cras_apm_reverse_state_update();
  update_supported_dsp_effects_activation(actx);
  reconfigure_apm_vad(actx);

  // If AEC dump is running, start AEC dump for this new APM.
  if (stream->aec_dump_enabled) {
    possibly_start_apm_aec_dump(stream, apm);
  }
}

void cras_stream_apm_stop(struct cras_stream_apm* stream,
                          struct cras_iodev* idev) {
  struct cras_audio_ctx* actx = checked_audio_ctx();

  struct active_apm* active;

  if (stream == NULL) {
    return;
  }

  active = get_active_apm(actx, stream, idev);
  if (active) {
    if (active->apm && active->apm->cras_processor_effect != NoEffects) {
      struct timespec now, runtime;
      clock_gettime(CLOCK_MONOTONIC_RAW, &now);
      subtract_timespecs(&now, &active->apm->start_ts, &runtime);
      switch (active->apm->cras_processor_effect) {
        case NoiseCancellation:
          cras_server_metrics_ap_nc_runtime(runtime.tv_sec);
          break;
        case StyleTransfer:
          cras_server_metrics_ast_runtime(runtime.tv_sec);
          break;
        default:
          break;
      }
      apm_state.num_nc--;
      apm_state.last_nc_closed = now;
    }

    // If AEC dump is active on this APM, stop it.
    if (active->apm->aec_dump_active) {
      possibly_stop_apm_aec_dump(stream, active->apm);
    }

    DL_DELETE(actx->apm->active_apms, active);
    free(active);
  }

  cras_apm_reverse_state_update();
  update_supported_dsp_effects_activation(actx);

  /* If there's still an APM using |idev| at this moment, the above call
   * to update_supported_dsp_effects_activation has decided the final
   * state of DSP effects on |idev|.
   */
  DL_FOREACH (actx->apm->active_apms, active) {
    if (active->apm->idev == idev) {
      return;
    }
  }

  /* Otherwise |idev| is no longer being used by stream APMs. Deactivate
   * any effects that has been activated. */
  toggle_dsp_effect(idev, RTC_PROC_AEC, 0);
  toggle_dsp_effect(idev, RTC_PROC_NS, 0);
  toggle_dsp_effect(idev, RTC_PROC_AGC, 0);
}

int cras_stream_apm_destroy(struct cras_stream_apm* stream) {
  struct cras_apm* apm;

  // Unlink any linked echo ref.
  cras_apm_reverse_link_echo_ref(stream, NULL);

  if (stream->aec_dump_enabled) {
    close(stream->aec_dump_fd);
  }

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
  struct cras_audio_ctx* actx = checked_audio_ctx();

  struct active_apm* active;
  int ret;
  float* const* rp;
  unsigned int unused;

  // Caller side ensures fbuf is full and hasn't been read at all.
  rp = float_buffer_read_pointer(fbuf, 0, &unused);

  DL_FOREACH (actx->apm->active_apms, active) {
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
  struct cras_audio_ctx* actx = checked_audio_ctx();

  struct active_apm* active;

  DL_FOREACH (actx->apm->active_apms, active) {
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
    syslog(LOG_DEBUG, "No aec ini file %s", ini_name);
  }

  if (0 == iniparser_getnsec(aec_ini)) {
    iniparser_freedict(aec_ini);
    aec_ini = NULL;
    syslog(LOG_DEBUG, "Empty aec ini file %s", ini_name);
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
    syslog(LOG_DEBUG, "No apm ini file %s", ini_name);
  }

  if (0 == iniparser_getnsec(apm_ini)) {
    iniparser_freedict(apm_ini);
    apm_ini = NULL;
    syslog(LOG_DEBUG, "Empty apm ini file %s", ini_name);
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
static void on_output_devices_changed(void) {
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
  struct cras_audio_ctx* actx = checked_audio_ctx();

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
        update_supported_dsp_effects_activation(actx);
        break;
      case APM_VAD_TARGET_CHANGED:
        update_vad_target(actx, msg.data1);
        break;
      case APM_DSP_INPUT_EFFECTS_BLOCKED:
        apm_thread_set_dsp_input_effects_blocked(msg.data1);
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

static void possibly_track_voice_activity(struct cras_audio_ctx* actx,
                                          struct cras_apm* apm) {
  if (!cached_vad_target) {
    return;
  }

  struct active_apm* active;
  DL_FOREACH (actx->apm->active_apms, active) {
    // Match only the first apm. We don't care multiple inputs.
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
  checked_audio_ctx()->apm = &ACTX_APM;

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
  struct cras_audio_ctx* actx = checked_audio_ctx();

  unsigned int writable, nframes, nread;
  int ch, i, j;
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

    // Process audio with cras_processor.
    struct multi_slice input = {
        .channels = apm->fmt.num_channels,
        .num_frames = nread,
    };
    struct multi_slice output = {};
    for (int ch = 0; ch < input.channels; ch++) {
      input.data[ch] = rp[ch];
    }
    enum status st =
        apm->cras_processor->ops->run(apm->cras_processor, &input, &output);
    if (st != StatusOk) {
      syslog(LOG_ERR, "cras_processor run failed");
      return -ENOTRECOVERABLE;
    }

    CRAS_CHECK(output.channels == apm->fmt.num_channels);
    CRAS_CHECK(output.num_frames == nread);

    dsp_util_interleave(output.data, buf_write_pointer(apm->buffer),
                        output.channels, apm->fmt.format, nread);
    buf_increment_write(apm->buffer, nread * cras_get_format_bytes(&apm->fmt));
    float_buffer_reset(apm->fbuffer);

    possibly_track_voice_activity(actx, apm);
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
  struct cras_audio_ctx* actx = checked_audio_ctx();

  struct active_apm* active = get_active_apm(actx, stream, idev);
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

  DL_SEARCH_SCALAR(stream->apms, apm, idev, idev);
  if (apm == NULL) {
    return;
  }

  if (start) {
    if (stream->aec_dump_enabled) {
      // If AEC dump is already running, keep it running. If the new fd is
      // different, close the previous one.
      //
      // If there is an APM with active AEC dump, possibly_start_apm_aec_dump
      // will fail (do nothing). New APM created after that will use the new fd.
      syslog(LOG_WARNING,
             "got aec dump start request, but it is already running");
      if (fd != stream->aec_dump_fd) {
        close(stream->aec_dump_fd);
      }
    }
    stream->aec_dump_enabled = true;
    stream->aec_dump_fd = fd;
    possibly_start_apm_aec_dump(stream, apm);
  } else {
    if (!stream->aec_dump_enabled) {
      syslog(LOG_WARNING, "got aec dump stop request, but it is not running");
      return;
    }

    // Stop the AEC dump on the APM with active dump.
    if (stream->aec_dump_active_apm) {
      possibly_stop_apm_aec_dump(stream, stream->aec_dump_active_apm);
    }

    stream->aec_dump_enabled = false;
    close(stream->aec_dump_fd);
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

struct cras_iodev* cras_stream_apm_get_aec_ref(struct cras_stream_apm* stream) {
  return stream ? stream->echo_ref : NULL;
}

void cras_stream_apm_notify_vad_target_changed(
    struct cras_stream_apm* vad_target) {
  int rc = send_apm_message_explicit(APM_VAD_TARGET_CHANGED, vad_target);
  if (rc < 0) {
    syslog(LOG_ERR, "Error sending vad target changed message");
  }
}

void cras_stream_apm_notify_dsp_input_effects_blocked(bool blocked) {
  int rc = send_apm_message_explicit(APM_DSP_INPUT_EFFECTS_BLOCKED,
                                     blocked ? (void*)1 : NULL);
  if (rc < 0) {
    syslog(LOG_ERR, "Error sending APM_AEC_ON_DSP_DISALLOWED message: %d", rc);
  }
}

static void handle_stream_apm_message(struct cras_main_message* msg,
                                      void* arg) {
  struct cras_stream_apm_message* stream_apm_msg =
      (struct cras_stream_apm_message*)msg;

  switch (stream_apm_msg->message_type) {
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

bool cras_stream_apm_vad_available(struct cras_stream_apm* stream) {
  // A stream can only provide VAD if the stream has the echo cancellation
  // effect:
  // 1. We don't want to detect speech coming from the device's speaker.
  //    An APM with APM_ECHO_CANCELLATION will always have echo cancelled:
  //    either inside the WebRTC-APM instance or already cancelled by DSP AEC.
  // 2. cras_stream_apm is just a container for streams to hold multiple
  // cras_apms.
  //    A stream with the APM_ECHO_CANCELLATION effect will always have a
  //    cras_apm attached. Streams that don't have AEC may get or lose their
  //    cras_apms when the user toggles the NC effect from the UI or switch the
  //    default input device. For simplicity, the voice activity detection
  //    target stream selection algorithm should not worry about device changes
  //    or preference changes.
  return stream && (stream->effects & APM_ECHO_CANCELLATION);
}

unsigned cras_apm_state_get_num_nc() {
  return apm_state.num_nc;
}

struct timespec cras_apm_state_get_last_nc_closed() {
  return apm_state.last_nc_closed;
}

struct cras_stream_apm_state cras_stream_apm_get_state(
    struct cras_stream_apm* stream) {
  struct cras_audio_ctx* actx = checked_audio_ctx();

  struct cras_apm* apm = find_active_apm(actx, stream);
  if (!apm) {
    return (struct cras_stream_apm_state){};
  }

  struct WebRtcApmStats stats = webrtc_apm_get_stats(apm->apm_ptr);
  return (struct cras_stream_apm_state){
      .active_ap_effects = get_active_ap_effects(apm),
      .webrtc_apm_forward_blocks_processed = stats.forward_blocks_processed,
      .webrtc_apm_reverse_blocks_processed = stats.reverse_blocks_processed,
  };
}
