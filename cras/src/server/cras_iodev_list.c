/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_iodev_list.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <syslog.h>
#include <time.h>

#include "cras/server/platform/features/features.h"
#include "cras/src/common/cras_hats.h"
#include "cras/src/common/cras_observer_ops.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_empty_iodev.h"
#include "cras/src/server/cras_floop_iodev.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_loopback_iodev.h"
#include "cras/src/server/cras_main_thread_log.h"
#include "cras/src/server/cras_nc.h"
#include "cras/src/server/cras_observer.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_speak_on_mute_detector.h"
#include "cras/src/server/cras_stream_apm.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_tm.h"
#include "cras/src/server/rust/include/cras_s2.h"
#include "cras/src/server/server_stream.h"
#include "cras/src/server/softvol_curve.h"
#include "cras/src/server/stream_list.h"
#include "cras/src/server/test_iodev.h"
#include "cras_iodev_info.h"
#include "cras_types.h"
#include "cras_util.h"
#include "third_party/strlcpy/strlcpy.h"
#include "third_party/utlist/utlist.h"

#define NUM_OPEN_DEVS_MAX 10
#define NUM_FLOOP_PAIRS_MAX 20

const struct timespec idle_timeout_interval = {.tv_sec = 10, .tv_nsec = 0};

// Linked list of available devices.
struct iodev_list {
  struct cras_iodev* iodevs;
  size_t size;
};

// List of enabled input/output devices.
struct enabled_dev {
  // The device.
  struct cras_iodev* dev;
  struct enabled_dev *prev, *next;
};

struct dev_init_retry {
  int dev_idx;
  struct cras_timer* init_timer;
  struct dev_init_retry *next, *prev;
};

struct device_enabled_cb {
  device_enabled_callback_t enabled_cb;
  device_disabled_callback_t disabled_cb;
  device_removed_callback_t removed_cb;
  void* cb_data;
  struct device_enabled_cb *next, *prev;
};

struct main_thread_event_log* main_log;

// Lists for devs[CRAS_STREAM_INPUT] and devs[CRAS_STREAM_OUTPUT].
static struct iodev_list devs[CRAS_NUM_DIRECTIONS];
// The observer client iodev_list used to listen on various events.
static struct cras_observer_client* list_observer;
// Keep a list of enabled inputs and outputs.
static struct enabled_dev* enabled_devs[CRAS_NUM_DIRECTIONS];
// Keep an empty device per direction.
static struct cras_iodev* fallback_devs[CRAS_NUM_DIRECTIONS];
// Special empty device for hotword streams.
static struct cras_iodev* empty_hotword_dev;
// Loopback devices.
static struct cras_iodev* loopdev_post_mix;
static struct cras_iodev* loopdev_post_dsp;
static struct cras_iodev* loopdev_post_dsp_delayed;
// List of pending device init retries.
static struct dev_init_retry* init_retries;

static struct cras_floop_pair* floop_pair_list;

/* Keep a constantly increasing index for iodevs. Index 0 is reserved
 * to mean "no device". */
static uint32_t next_iodev_idx = MAX_SPECIAL_DEVICE_IDX;

// Call when a device is enabled or disabled.
struct device_enabled_cb* device_enable_cbs;

// Thread that handles audio input and output.
static struct audio_thread* audio_thread;
// List of all streams.
static struct stream_list* stream_list;
// Idle device timer.
static struct cras_timer* idle_timer;
// Floop clear timer.
static struct cras_timer* floop_timer;
// Flag to indicate that the stream list is disconnected from audio thread.
static int stream_list_suspended = 0;
// If init device failed, retry after 1 second.
static const unsigned int INIT_DEV_DELAY_MS = 1000;
// Flag to indicate that hotword streams are suspended.
static int hotword_suspended = 0;
/* Flag to indicate that suspended hotword streams should be auto-resumed at
 * system resume. */
static int hotword_auto_resume = 0;

/*
 * Flags to indicate that Noise Cancellation is blocked. Each flag handles own
 * scenario and will be updated in respective timing.
 *
 * 1. non_dsp_aec_echo_ref_dev_alive
 *     scenario:
 *         detect if there exists an enabled or opened output device which can't
 *         be applied as echo reference for AEC on DSP.
 *     timing for updating state:
 *         check rising edge on enable_dev() & open_dev() of output devices.
 *         check falling edge on disable_dev() & close_dev() of output devices.
 *
 * 2. aec_on_dsp_is_disallowed
 *     scenario:
 *         detect if there exists an input stream requesting AEC on DSP
 *         disallowed while it is supported.
 *     timing for updating state:
 *         check accompanied with dsp_effect_check_conflict(RTC_PROC_AEC) under
 *         update_supported_dsp_effects_activation() in cras_stream_apm.
 *
 * The final NC blocking state is determined by:
 *     nc_blocked_state = (non_dsp_aec_echo_ref_dev_alive ||
 *                         aec_on_dsp_is_disallowed)
 *
 * CRAS will notify Chrome promptly when nc_blocked_state is altered.
 */
static bool non_dsp_aec_echo_ref_dev_alive = false;
static bool aec_on_dsp_is_disallowed = false;

// Returns true for blocking DSP input effects; false for unblocking.
static bool get_dsp_input_effects_blocked_state() {
  // TODO(b/272408566) remove this WA when the formal fix is landed
  if (cras_system_get_noise_cancellation_standalone_mode()) {
    return non_dsp_aec_echo_ref_dev_alive;
  }

  return non_dsp_aec_echo_ref_dev_alive || aec_on_dsp_is_disallowed;
}

static void update_dsp_input_effects_blocked_state(
    bool new_non_dsp_aec_echo_ref_dev_alive,
    bool new_aec_on_dsp_is_disallowed) {
  bool prev_state = get_dsp_input_effects_blocked_state();

  non_dsp_aec_echo_ref_dev_alive = new_non_dsp_aec_echo_ref_dev_alive;
  aec_on_dsp_is_disallowed = new_aec_on_dsp_is_disallowed;

  if (prev_state != get_dsp_input_effects_blocked_state()) {
    MAINLOG(main_log, MAIN_THREAD_NC_BLOCK_STATE,
            get_dsp_input_effects_blocked_state(),
            non_dsp_aec_echo_ref_dev_alive, aec_on_dsp_is_disallowed);
    syslog(LOG_INFO, "DSP input effects are %s: non_echo=%d disallow=%d",
           get_dsp_input_effects_blocked_state() ? "deactivated" : "activated",
           non_dsp_aec_echo_ref_dev_alive, aec_on_dsp_is_disallowed);

    cras_stream_apm_notify_dsp_input_effects_blocked(
        get_dsp_input_effects_blocked_state());

    // notify Chrome for NC blocking state change
    cras_iodev_list_update_device_list();
    cras_iodev_list_notify_nodes_changed();
  }
}

static void set_non_dsp_aec_echo_ref_dev_alive(bool state) {
  update_dsp_input_effects_blocked_state(state, aec_on_dsp_is_disallowed);
}

static void set_aec_on_dsp_is_disallowed(bool disallowed) {
  update_dsp_input_effects_blocked_state(non_dsp_aec_echo_ref_dev_alive,
                                         disallowed);
}

static void idle_dev_check(struct cras_timer* timer, void* data);

static struct cras_iodev* find_dev(size_t dev_index) {
  struct cras_iodev* dev;

  DL_FOREACH (devs[CRAS_STREAM_OUTPUT].iodevs, dev) {
    if (dev->info.idx == dev_index) {
      return dev;
    }
  }

  DL_FOREACH (devs[CRAS_STREAM_INPUT].iodevs, dev) {
    if (dev->info.idx == dev_index) {
      return dev;
    }
  }

  return NULL;
}

static struct cras_ionode* find_node(struct cras_iodev* iodev,
                                     unsigned int node_idx) {
  struct cras_ionode* node;
  DL_SEARCH_SCALAR(iodev->nodes, node, idx, node_idx);
  return node;
}

// Adds a device to the list.  Used from add_input and add_output.
static int add_dev_to_list(struct cras_iodev* dev) {
  struct cras_iodev* tmp;
  uint32_t new_idx;
  struct iodev_list* list = &devs[dev->direction];

  DL_FOREACH (list->iodevs, tmp) {
    if (tmp == dev) {
      return -EEXIST;
    }
  }

  dev->format = NULL;
  dev->format = NULL;
  dev->prev = dev->next = NULL;

  // Move to the next index and make sure it isn't taken.
  new_idx = next_iodev_idx;
  while (1) {
    if (new_idx < MAX_SPECIAL_DEVICE_IDX) {
      new_idx = MAX_SPECIAL_DEVICE_IDX;
    }
    DL_SEARCH_SCALAR(list->iodevs, tmp, info.idx, new_idx);
    if (tmp == NULL) {
      break;
    }
    new_idx++;
  }
  dev->info.idx = new_idx;
  next_iodev_idx = new_idx + 1;
  list->size++;

  syslog(LOG_DEBUG, "Adding %s dev at index %u.",
         dev->direction == CRAS_STREAM_OUTPUT ? "output" : "input",
         dev->info.idx);
  DL_PREPEND(list->iodevs, dev);

  cras_iodev_list_update_device_list();
  return 0;
}

// Removes a device from all direciton lists.
static int rm_dev_from_list(struct cras_iodev* dev) {
  struct cras_iodev* tmp;
  struct device_enabled_cb* callback;

  //
  DL_FOREACH (device_enable_cbs, callback) {
    if (callback->removed_cb) {
      callback->removed_cb(dev);
    }
  }

  DL_FOREACH (devs[dev->direction].iodevs, tmp) {
    if (tmp == dev) {
      if (cras_iodev_is_open(dev)) {
        return -EBUSY;
      }
      DL_DELETE(devs[dev->direction].iodevs, dev);
      devs[dev->direction].size--;
      return 0;
    }
  }

  // Device not found.
  return -EINVAL;
}

// Fills a dev_info array from the iodev_list.
static void fill_dev_list(struct iodev_list* list,
                          struct cras_iodev_info* dev_info,
                          size_t out_size) {
  int i = 0;
  struct cras_iodev* tmp;
  DL_FOREACH (list->iodevs, tmp) {
    memcpy(&dev_info[i], &tmp->info, sizeof(dev_info[0]));
    i++;
    if (i == out_size) {
      return;
    }
  }
}

// Auxiliary information for fill_node_list.
struct fill_node_list_auxiliary {
  bool dsp_nc_allowed;
  bool ap_nc_allowed;
};

static struct fill_node_list_auxiliary get_fill_node_list_auxiliary() {
  struct fill_node_list_auxiliary aux = {
      .dsp_nc_allowed = !get_dsp_input_effects_blocked_state() ||
                        cras_system_get_bypass_block_noise_cancellation(),
      .ap_nc_allowed = cras_s2_get_ap_nc_allowed(),
  };
  return aux;
}

// Fills an ionode_info array from the iodev_list.
// Must not block.
static int fill_node_list(struct iodev_list* list,
                          struct cras_ionode_info node_info[],
                          size_t out_size,
                          const struct fill_node_list_auxiliary* aux) {
  int i = 0;
  struct cras_iodev* dev;
  struct cras_ionode* node;

  DL_FOREACH (list->iodevs, dev) {
    DL_FOREACH (dev->nodes, node) {
      node_info->iodev_idx = dev->info.idx;
      node_info->ionode_idx = node->idx;
      node_info->plugged = node->plugged;
      node_info->plugged_time.tv_sec = node->plugged_time.tv_sec;
      node_info->plugged_time.tv_usec = node->plugged_time.tv_usec;
      node_info->active = dev->is_enabled && (dev->active_node == node);
      node_info->volume = node->volume;
      node_info->capture_gain = node->internal_capture_gain;
      node_info->ui_gain_scaler = node->ui_gain_scaler;
      node_info->left_right_swapped = node->left_right_swapped;
      node_info->display_rotation = node->display_rotation;
      node_info->stable_id = node->stable_id;
      strlcpy(node_info->name, node->name, sizeof(node_info->name));
      strlcpy(node_info->active_hotword_model, node->active_hotword_model,
              sizeof(node_info->active_hotword_model));
      snprintf(node_info->type, sizeof(node_info->type), "%s",
               cras_node_type_to_str(node->type, node->position));
      node_info->type_enum = node->type;
      node_info->audio_effect = 0;

      node->desired_nc_provider = cras_nc_resolve_provider(
          node->nc_providers, aux->dsp_nc_allowed, aux->ap_nc_allowed);
      if (node->desired_nc_provider != CRAS_NC_PROVIDER_NONE) {
        node_info->audio_effect |= EFFECT_TYPE_NOISE_CANCELLATION;
      }

      if (cras_system_get_sr_bt_supported() &&
          node->type == CRAS_NODE_TYPE_BLUETOOTH_NB_MIC) {
        node_info->audio_effect |= EFFECT_TYPE_HFP_MIC_SR;
      }
      node_info->number_of_volume_steps = node->number_of_volume_steps;
      node_info++;
      i++;
      if (i == out_size) {
        return i;
      }
    }
  }
  return i;
}

// Copies the info for each device in the list to "list_out".
static int get_dev_list(struct iodev_list* list,
                        struct cras_iodev_info** list_out) {
  struct cras_iodev_info* dev_info;

  if (!list_out) {
    return list->size;
  }

  *list_out = NULL;
  if (list->size == 0) {
    return 0;
  }

  dev_info = (struct cras_iodev_info*)malloc(sizeof(*list_out[0]) * list->size);
  if (dev_info == NULL) {
    return -ENOMEM;
  }

  fill_dev_list(list, dev_info, list->size);

  *list_out = dev_info;
  return list->size;
}

/* Called when the system volume changes.  Pass the current volume setting to
 * the default output if it is active. */
static void sys_vol_change(void* context, int32_t volume) {
  struct cras_iodev* dev;

  DL_FOREACH (devs[CRAS_STREAM_OUTPUT].iodevs, dev) {
    if (dev->set_volume && cras_iodev_is_open(dev)) {
      dev->set_volume(dev);
    }
  }
}

/* Called when the system mute state changes.  Pass the current mute setting
 * to the default output if it is active. */
static void sys_mute_change(void* context,
                            int muted,
                            int user_muted,
                            int mute_locked) {
  struct cras_iodev* dev;
  int should_mute = muted || user_muted;

  DL_FOREACH (devs[CRAS_STREAM_OUTPUT].iodevs, dev) {
    if (!cras_iodev_is_open(dev)) {
      // For closed devices, just set its mute state.
      cras_iodev_set_mute(dev);
    } else {
      audio_thread_dev_start_ramp(
          audio_thread, dev->info.idx,
          (should_mute ? CRAS_IODEV_RAMP_REQUEST_DOWN_MUTE
                       : CRAS_IODEV_RAMP_REQUEST_UP_UNMUTE));
    }
  }
}

static void remove_all_streams_from_dev(struct cras_iodev* dev) {
  struct cras_rstream* rstream;

  audio_thread_rm_open_dev(audio_thread, dev->direction, dev->info.idx);

  DL_FOREACH (stream_list_get(stream_list), rstream) {
    if (rstream->stream_apm == NULL) {
      continue;
    }
    cras_stream_apm_remove(rstream->stream_apm, dev);
  }
}

/*
 * If output dev has an echo reference dev associated, add a server
 * stream to read audio data from it so APM can analyze.
 */
static void possibly_enable_echo_reference(struct cras_iodev* dev) {
  if (dev->direction != CRAS_STREAM_OUTPUT) {
    return;
  }

  if (dev->echo_reference_dev == NULL) {
    return;
  }

  int rc =
      server_stream_create(stream_list, SERVER_STREAM_ECHO_REF,
                           dev->echo_reference_dev->info.idx, dev->format, 0);
  if (rc) {
    syslog(LOG_ERR, "Failed to create echo ref server stream");
  }
}

/*
 * If output dev has an echo reference dev associated, check if there
 * is server stream opened for it and remove it.
 */
static void possibly_disable_echo_reference(struct cras_iodev* dev) {
  if (dev->echo_reference_dev == NULL) {
    return;
  }

  server_stream_destroy(stream_list, SERVER_STREAM_ECHO_REF,
                        dev->echo_reference_dev->info.idx);
}

static bool is_dsp_aec_use_case(const struct cras_ionode* node) {
  /* For NC standalone mode, this checker should be interpreted to
   * old-fashioned use case, i.e. any node but Internal Speaker.
   * TODO(b/272408566) remove this WA when the formal fix is landed
   */
  if (cras_system_get_noise_cancellation_standalone_mode()) {
    return node->type != CRAS_NODE_TYPE_INTERNAL_SPEAKER;
  }

  return cras_iodev_is_dsp_aec_use_case(node);
}

/*
 * Sets |non_dsp_aec_echo_ref_dev_alive| true for NC blocking state decision
 * if the output device |dev| is enabled or opened while it can't be applied
 * as echo reference for AEC on DSP.
 */
static void possibly_set_non_dsp_aec_echo_ref_dev_alive(
    const struct cras_iodev* dev) {
  if (non_dsp_aec_echo_ref_dev_alive) {
    return;
  }

  // skip silent devices
  if (dev->info.idx < MAX_SPECIAL_DEVICE_IDX) {
    return;
  }

  // skip input devices
  if (dev->direction == CRAS_STREAM_INPUT) {
    return;
  }

  // skip if the device is not alive (neither enabled nor opened)
  if (!cras_iodev_list_dev_is_enabled(dev) && !cras_iodev_is_open(dev)) {
    return;
  }

  if (dev->active_node && !is_dsp_aec_use_case(dev->active_node)) {
    syslog(LOG_DEBUG, "non_dsp_aec_echo_ref_dev_alive=1 by output dev: %u",
           dev->info.idx);
    set_non_dsp_aec_echo_ref_dev_alive(true);
  }
}

/*
 * Sets |non_dsp_aec_echo_ref_dev_alive| false for NC blocking state decision
 * when there is no enabled or opened output which can't be applied as echo
 * reference.
 */
static void possibly_clear_non_dsp_aec_echo_ref_dev_alive() {
  struct cras_iodev* dev;

  if (!non_dsp_aec_echo_ref_dev_alive) {
    return;
  }

  DL_FOREACH (devs[CRAS_STREAM_OUTPUT].iodevs, dev) {
    // neglect silent devices
    if (dev->info.idx < MAX_SPECIAL_DEVICE_IDX) {
      continue;
    }

    // neglect devices which are neither enabled nor opened
    if (!(dev->is_enabled || cras_iodev_is_open(dev))) {
      continue;
    }

    if (dev->active_node && !is_dsp_aec_use_case(dev->active_node)) {
      /* find one active device which is not applicable for DSP AEC echo
       * reference, do not clear the state then. */
      return;
    }
  }

  syslog(LOG_DEBUG, "non_dsp_aec_echo_ref_dev_alive=0");
  set_non_dsp_aec_echo_ref_dev_alive(false);
}

/*
 * Removes all attached streams and close dev if it's opened.
 */
static void close_dev(struct cras_iodev* dev) {
  if (!cras_iodev_is_open(dev)) {
    return;
  }

  MAINLOG(main_log, MAIN_THREAD_DEV_CLOSE, dev->info.idx, 0, 0);
  remove_all_streams_from_dev(dev);
  if (dev->active_node->type != CRAS_NODE_TYPE_FLOOP) {
    dev->idle_timeout.tv_sec = 0;
  }
  // close echo ref first to avoid underrun in hardware
  possibly_disable_echo_reference(dev);
  cras_iodev_close(dev);

  possibly_clear_non_dsp_aec_echo_ref_dev_alive();
}

static void idle_dev_check(struct cras_timer* timer, void* data) {
  struct enabled_dev* edev;
  struct timespec now;
  struct timespec min_idle_expiration;
  unsigned int num_idle_devs = 0;
  unsigned int min_idle_timeout_ms;

  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  min_idle_expiration.tv_sec = 0;
  min_idle_expiration.tv_nsec = 0;

  DL_FOREACH (enabled_devs[CRAS_STREAM_OUTPUT], edev) {
    if (edev->dev->idle_timeout.tv_sec == 0) {
      continue;
    }
    if (timespec_after(&now, &edev->dev->idle_timeout)) {
      close_dev(edev->dev);
      continue;
    }
    num_idle_devs++;
    if (min_idle_expiration.tv_sec == 0 ||
        timespec_after(&min_idle_expiration, &edev->dev->idle_timeout)) {
      min_idle_expiration = edev->dev->idle_timeout;
    }
  }

  idle_timer = NULL;
  if (!num_idle_devs) {
    return;
  }
  if (timespec_after(&now, &min_idle_expiration)) {
    min_idle_timeout_ms = 0;
  } else {
    struct timespec timeout;
    subtract_timespecs(&min_idle_expiration, &now, &timeout);
    min_idle_timeout_ms = timespec_to_ms(&timeout);
  }
  /* Wake up when it is time to close the next idle device.  Sleep for a
   * minimum of 10 milliseconds. */
  idle_timer =
      cras_tm_create_timer(cras_system_state_get_tm(),
                           MAX(min_idle_timeout_ms, 10), idle_dev_check, NULL);
}

static void idle_floop_check(struct cras_timer* timer, void* data) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC_RAW, &now);

  struct timespec next_expiry_check = {.tv_sec = 0, .tv_nsec = 0};
  struct cras_floop_pair* pair;
  DL_FOREACH (floop_pair_list, pair) {
    if (timespec_is_zero(&pair->input.idle_timeout)) {
      continue;
    }
    if (timespec_after(&now, &pair->input.idle_timeout)) {
      DL_DELETE(floop_pair_list, pair);
      cras_floop_pair_destroy(pair);
      continue;
    }
    if (timespec_is_zero(&next_expiry_check) ||
        timespec_after(&next_expiry_check, &pair->input.idle_timeout)) {
      next_expiry_check = pair->input.idle_timeout;
    }
  }

  floop_timer = NULL;
  if (timespec_is_zero(&next_expiry_check)) {
    return;
  }

  unsigned int min_idle_timeout_ms;
  if (timespec_after(&now, &next_expiry_check)) {
    min_idle_timeout_ms = 0;
  } else {
    struct timespec timeout;
    subtract_timespecs(&next_expiry_check, &now, &timeout);
    min_idle_timeout_ms = timespec_to_ms(&timeout);
  }

  /* Wake up when it is time to close the next floop device.  Sleep for a
   * minimum of 10 milliseconds. */
  floop_timer = cras_tm_create_timer(cras_system_state_get_tm(),
                                     MAX(min_idle_timeout_ms, 10),
                                     idle_floop_check, NULL);
}

/*
 * Cancel pending init tries. Called at device initialization or when device
 * is disabled.
 */
static void cancel_pending_init_retries(unsigned int dev_idx) {
  struct dev_init_retry* retry;

  DL_FOREACH (init_retries, retry) {
    if (retry->dev_idx != dev_idx) {
      continue;
    }
    cras_tm_cancel_timer(cras_system_state_get_tm(), retry->init_timer);
    DL_DELETE(init_retries, retry);
    free(retry);
  }
}

// Open the device potentially filling the output with a pre buffer.
static int init_device(struct cras_iodev* dev, struct cras_rstream* rstream) {
  int rc;

  cras_iodev_exit_idle(dev);

  if (cras_iodev_is_open(dev)) {
    return 0;
  }

  dev->info.last_open_result = SUCCESS;
  cancel_pending_init_retries(dev->info.idx);
  MAINLOG(main_log, MAIN_THREAD_DEV_INIT, dev->info.idx,
          rstream->format.num_channels, rstream->format.frame_rate);

  rc = cras_iodev_open(dev, rstream->cb_threshold, &rstream->format);
  if (rc) {
    dev->info.last_open_result = FAILURE;
    return rc;
  }

  rc = audio_thread_add_open_dev(audio_thread, dev);
  if (rc) {
    cras_iodev_close(dev);
  }

  possibly_enable_echo_reference(dev);

  possibly_set_non_dsp_aec_echo_ref_dev_alive(dev);

  return rc;
}

static void suspend_devs() {
  struct enabled_dev* edev;
  struct cras_rstream* rstream;

  MAINLOG(main_log, MAIN_THREAD_SUSPEND_DEVS, 0, 0, 0);

  DL_FOREACH (stream_list_get(stream_list), rstream) {
    if (rstream->is_pinned) {
      struct cras_iodev* dev;

      /* Skip closing hotword stream in the first pass.
       * Closing an input device may resume hotword stream
       * with its post_close_iodev_hook so we should deal
       * with hotword stream in the second pass.
       */
      if ((rstream->flags & HOTWORD_STREAM) == HOTWORD_STREAM) {
        continue;
      }

      dev = find_dev(rstream->pinned_dev_idx);
      if (dev) {
        audio_thread_disconnect_stream(audio_thread, rstream, dev);
        if (!cras_iodev_list_dev_is_enabled(dev)) {
          close_dev(dev);
        }
      }
    } else {
      audio_thread_disconnect_stream(audio_thread, rstream, NULL);
    }
  }
  stream_list_suspended = 1;

  DL_FOREACH (enabled_devs[CRAS_STREAM_OUTPUT], edev) {
    close_dev(edev->dev);
  }
  DL_FOREACH (enabled_devs[CRAS_STREAM_INPUT], edev) {
    close_dev(edev->dev);
  }

  /* Doing this check after all the other enabled iodevs are closed to
   * ensure preempted hotword streams obey the pause_at_suspend flag.
   */
  if (cras_system_get_hotword_pause_at_suspend()) {
    cras_iodev_list_suspend_hotword_streams();
    hotword_auto_resume = 1;
  }
}

static int stream_added_cb(struct cras_rstream* rstream);

static void resume_devs() {
  struct enabled_dev* edev;
  struct cras_rstream* rstream;

  stream_list_suspended = 0;

  MAINLOG(main_log, MAIN_THREAD_RESUME_DEVS, 0, 0, 0);

  /* Auto-resume based on the local flag in case the system state flag has
   * changed.
   */
  if (hotword_auto_resume) {
    cras_iodev_list_resume_hotword_stream();
    hotword_auto_resume = 0;
  }

  /*
   * To remove the short popped noise caused by applications that can not
   * stop playback "right away" after resume, we mute all output devices
   * for a short time if there is any output stream.
   */
  if (stream_list_get_num_output(stream_list)) {
    DL_FOREACH (enabled_devs[CRAS_STREAM_OUTPUT], edev) {
      edev->dev->initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_RESUME_MUTE;
    }
  }

  DL_FOREACH (stream_list_get(stream_list), rstream) {
    if ((rstream->flags & HOTWORD_STREAM) == HOTWORD_STREAM) {
      continue;
    }
    stream_added_cb(rstream);
  }
}

// Called when the system audio is suspended or resumed.
void sys_suspend_change(void* arg, int suspended) {
  if (suspended) {
    suspend_devs();
  } else {
    resume_devs();
  }
}

// Callback for nodes changed.
static void nodes_changed(void* arg) {
  cras_iodev_list_reset_for_noise_cancellation();
}

static int disable_device(struct enabled_dev* edev, bool force);
static int enable_device(struct cras_iodev* dev);

static void possibly_disable_fallback(enum CRAS_STREAM_DIRECTION dir) {
  struct enabled_dev* edev;

  syslog(LOG_DEBUG, "possibly_disable_fallback(dir=%d)", dir);

  DL_FOREACH (enabled_devs[dir], edev) {
    if (edev->dev == fallback_devs[dir]) {
      disable_device(edev, false);
    }
  }
}

/*
 * Possibly enables fallback device to handle streams.
 * dir - output or input.
 * error - true if enable fallback device because no other iodevs can be
 * initialized successfully.
 */
static void possibly_enable_fallback(enum CRAS_STREAM_DIRECTION dir,
                                     bool error) {
  syslog(LOG_DEBUG, "possibly_enable_fallback(dir=%d)", dir);

  if (fallback_devs[dir] == NULL) {
    return;
  }

  /*
   * The fallback device is a special device. It doesn't have a real
   * device to get a correct node type. Therefore, we need to set it by
   * ourselves, which indicates the reason to use this device.
   * NORMAL - Use it because of nodes changed.
   * ABNORMAL - Use it because there are no other usable devices.
   */
  if (error) {
    syslog(LOG_ERR,
           "Enable fallback device because there are no other usable devices.");
  }

  fallback_devs[dir]->active_node->type =
      error ? CRAS_NODE_TYPE_FALLBACK_ABNORMAL : CRAS_NODE_TYPE_FALLBACK_NORMAL;
  if (!cras_iodev_list_dev_is_enabled(fallback_devs[dir])) {
    enable_device(fallback_devs[dir]);
  }
}

/*
 * Adds stream to one or more open iodevs. If the stream has processing effect
 * turned on, create new APM instance and add to the list. This makes sure the
 * time consuming APM creation happens in main thread.
 */
static int add_stream_to_open_devs(struct cras_rstream* stream,
                                   struct cras_iodev** iodevs,
                                   unsigned int num_iodevs) {
  int i;
  if (stream->stream_apm) {
    for (i = 0; i < num_iodevs; i++) {
      cras_stream_apm_add(stream->stream_apm, iodevs[i], iodevs[i]->format);
    }
  }
  return audio_thread_add_stream(audio_thread, stream, iodevs, num_iodevs);
}

// Returns true if dev is one of the fallback devices.
static inline bool is_fallback_dev(struct cras_iodev* dev) {
  return (SILENT_PLAYBACK_DEVICE == dev->info.idx) ||
         (SILENT_RECORD_DEVICE == dev->info.idx);
}

static int init_and_attach_streams(struct cras_iodev* dev) {
  int rc;
  enum CRAS_STREAM_DIRECTION dir = dev->direction;
  struct cras_rstream* stream;
  int dev_enabled = cras_iodev_list_dev_is_enabled(dev);

  /* If called after suspend, for example bluetooth
   * profile switching, don't add back the stream list. */
  if (stream_list_suspended) {
    return 0;
  }

  // If there are active streams to attach to this device, open it.
  DL_FOREACH (stream_list_get(stream_list), stream) {
    bool may_attach = false;

    if (stream->direction != dir) {
      continue;
    }

    // If this is an enabled fallback device, the stream may attach. Attaching
    // to fallback device is temporary during active node switch, but may last
    // longer if no device can be opened. possibly_disable_fallback ensures all
    // streams are removed from the fallback devices.
    if (is_fallback_dev(dev)) {
      may_attach = dev_enabled;
    }
    // For normal stream, if the device is enabled by UI via active node
    // settings, the stream may attach.
    else if (!stream->is_pinned) {
      may_attach = dev_enabled;
    }
    // For pinned stream, if this device is in the stream's target iodev group,
    // the stream may attach. stream->pinned_dev_idx indicates the group, not a
    // particular device. It's equivalent for clients to specify the index of
    // any device in the group, though only one is visible currently.
    else if (cras_iodev_group_has_dev(dev, stream->pinned_dev_idx)) {
      may_attach = true;
    }

    if (!may_attach) {
      continue;
    }

    if (!cras_iodev_should_attach_stream(dev, stream)) {
      continue;
    }

    /*
     * Note that the stream list is descending ordered by channel
     * count, which guarantees the first attachable stream will have
     * the highest channel count.
     */
    rc = init_device(dev, stream);
    if (rc) {
      syslog(LOG_WARNING, "Enable %s failed, rc = %d", dev->info.name, rc);
      return rc;
    }
    add_stream_to_open_devs(stream, &dev, 1);
  }
  return 0;
}

static void init_device_cb(struct cras_timer* timer, void* arg) {
  int rc;
  struct dev_init_retry* retry = (struct dev_init_retry*)arg;
  struct cras_iodev* dev = find_dev(retry->dev_idx);

  /*
   * First of all, remove retry record to avoid confusion to the
   * actual device init work.
   */
  DL_DELETE(init_retries, retry);
  free(retry);

  if (!dev || cras_iodev_is_open(dev)) {
    return;
  }

  rc = init_and_attach_streams(dev);
  if (rc < 0) {
    syslog(LOG_WARNING, "Init device retry failed");
  } else {
    possibly_disable_fallback(dev->direction);
  }
}

static int schedule_init_device_retry(struct cras_iodev* dev) {
  struct dev_init_retry* retry;
  struct cras_tm* tm = cras_system_state_get_tm();

  retry = (struct dev_init_retry*)calloc(1, sizeof(*retry));
  if (!retry) {
    return -ENOMEM;
  }

  retry->dev_idx = dev->info.idx;
  retry->init_timer =
      cras_tm_create_timer(tm, INIT_DEV_DELAY_MS, init_device_cb, retry);
  DL_APPEND(init_retries, retry);
  return 0;
}

static int init_pinned_device(struct cras_iodev* dev,
                              struct cras_rstream* rstream) {
  int rc;

  cras_iodev_exit_idle(dev);

  if (audio_thread_is_dev_open(audio_thread, dev)) {
    return 0;
  }

  // Don't disturb the active node if there is an open dev in the group.
  if (!cras_iodev_group_has_open_dev(dev)) {
    /* Make sure the active node is configured properly, it could be
     * disabled when last normal stream removed. */
    dev->update_active_node(dev, dev->active_node->idx, 1);
  }

  // Negative EAGAIN code indicates dev will be opened later.
  rc = init_device(dev, rstream);
  if (rc) {
    return rc;
  }
  return 0;
}

/*
 * Close device enabled by pinned stream. Since it's NOT in the enabled
 * dev list, make sure update_active_node() is called to correctly
 * configure the ALSA UCM or BT profile state.
 */
static int close_pinned_device(struct cras_iodev* dev) {
  if (dev->active_node->type == CRAS_NODE_TYPE_FLOOP) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &dev->idle_timeout);
    add_timespecs(&dev->idle_timeout, &idle_timeout_interval);
    idle_floop_check(NULL, NULL);
  }
  close_dev(dev);

  // Update active node after the last iodev in the group is closed.
  if (!cras_iodev_group_has_open_dev(dev)) {
    dev->update_active_node(dev, dev->active_node->idx, 0);
  }
  return 0;
}

static struct cras_iodev* find_pinned_device(struct cras_rstream* rstream) {
  struct cras_iodev* dev;
  if (!rstream->is_pinned) {
    return NULL;
  }

  dev = find_dev(rstream->pinned_dev_idx);

  if ((rstream->flags & HOTWORD_STREAM) != HOTWORD_STREAM) {
    return dev;
  }

  // Double check node type for hotword stream
  if (dev && dev->active_node->type != CRAS_NODE_TYPE_HOTWORD) {
    syslog(LOG_WARNING, "Hotword stream pinned to invalid dev %u",
           dev->info.idx);
    return NULL;
  }

  return hotword_suspended ? empty_hotword_dev : dev;
}

static int pinned_stream_added(struct cras_rstream* rstream) {
  struct cras_iodev* dev;

  // Check that the target device is valid for pinned streams.
  dev = find_pinned_device(rstream);
  if (!dev) {
    return -EINVAL;
  }

  syslog(LOG_DEBUG, "pinned_stream_added(stream=0x%x, dev=%s)",
         rstream->stream_id, dev->info.name);

  // Init the matched device(s) in the target group. If any of the matched
  // devices is already open, init_pinned_device exits early. The attach check
  // is still needed to find the matched device(s) for the pinned stream.
  size_t group_size;
  struct cras_iodev* const* group = cras_iodev_get_dev_group(dev, &group_size);

  if (!group) {
    group = &dev;
    group_size = 1;
  }

  size_t num_open_devs = 0, num_potentially_attached_devs = 0;
  struct cras_iodev* open_devs[NUM_OPEN_DEVS_MAX];
  int first_err = 0;

  for (size_t i = 0; i < group_size; i++) {
    if (!cras_iodev_should_attach_stream(group[i], rstream)) {
      continue;
    }

    if (num_open_devs >= ARRAY_SIZE(open_devs)) {
      syslog(LOG_ERR, "Too many pinned open devices");
      break;
    }

    int rc = init_pinned_device(group[i], rstream);
    if (rc) {
      syslog(LOG_DEBUG, "init_pinned_device failed, rc %d", rc);
      rc = schedule_init_device_retry(group[i]);
      if (rc) {
        syslog(LOG_ERR, "init_pinned_device retry schedule failed, rc %d", rc);
        if (!first_err) {
          first_err = rc;
        }
      } else {
        // The stream may be attached to the retrying device later. It is
        // considered pinned to the iodev at this point, even if the retry fails
        // later. pinned_stream_removed will be called.
        num_potentially_attached_devs++;
        group[i]->num_pinned_streams++;
      }
      continue;
    }
    open_devs[num_open_devs++] = group[i];
    // The pinned stream may attach to more than one devices in the target
    // group. Loop to find all matched devices.
  }

  // Attach the stream to devices that are opened successfully on the first try.
  if (num_open_devs) {
    int rc = add_stream_to_open_devs(rstream, open_devs, num_open_devs);
    if (rc) {
      syslog(LOG_ERR, "Adding pinned stream to thread failed, rc %d", rc);
      if (!first_err) {
        first_err = rc;
      }
    } else {
      num_potentially_attached_devs += num_open_devs;
      for (size_t i = 0; i < num_open_devs; i++) {
        open_devs[i]->num_pinned_streams++;
      }
    }
  }

  // Keep the stream if it's been or will be attached to at least one device,
  // even when there are errors at opening and attaching. A failed device should
  // not block the stream from running on the other matched device(s).
  if (num_potentially_attached_devs) {
    return 0;
  }
  // Otherwise destroy and free the stream. There is no chance to attach to any
  // device at this point. Exit with an error code to avoid client stream
  // blocking indefinitely.
  return first_err ? first_err : -ENODEV;
}

/*
 - This is to replace calling suspend_dev then immediately
   resume_dev without manipulating fallback_devs

 - Caller should take care of enabling fallback_devs to avoid
   blocking client streaming.
*/
static void restart_device_group(unsigned int dev_idx) {
  struct cras_iodev* dev = find_dev(dev_idx);

  if (!dev) {
    return;
  }

  syslog(LOG_DEBUG, "restart_device_group(dev=%s)", dev->info.name);

  size_t size;
  struct cras_iodev* const* group = cras_iodev_get_dev_group(dev, &size);

  if (!group) {
    group = &dev;
    size = 1;
  }

  for (size_t i = 0; i < size; i++) {
    close_dev(group[i]);
  }
  dev->update_active_node(dev, dev->active_node->idx, 0);

  dev->update_active_node(dev, dev->active_node->idx, 1);
  for (size_t i = 0; i < size; i++) {
    // init_and_attach_streams opens only the matched devices in the group.
    int rc = init_and_attach_streams(group[i]);
    if (rc) {
      syslog(LOG_ERR, "Enable dev fail at restart, rc %d", rc);
      schedule_init_device_retry(group[i]);
    }
  }
}

static int stream_added_cb(struct cras_rstream* rstream) {
  struct enabled_dev* edev;
  struct cras_iodev* iodevs[NUM_OPEN_DEVS_MAX];
  unsigned int num_iodevs;
  int rc;
  bool iodev_reopened;

  // Should there be a fallback at the end of this function,
  // check this variable to suppress unnecessary warnings.
  bool expect_fallback = false;

  if (stream_list_suspended) {
    return 0;
  }

  MAINLOG(main_log, MAIN_THREAD_STREAM_ADDED, rstream->stream_id,
          rstream->direction, rstream->buffer_frames);

  if (rstream->is_pinned) {
    return pinned_stream_added(rstream);
  }

  // Catch the stream with fallback if it is already enabled
  if (cras_iodev_list_dev_is_enabled(fallback_devs[rstream->direction])) {
    init_device(fallback_devs[rstream->direction], rstream);
    add_stream_to_open_devs(rstream, &fallback_devs[rstream->direction], 1);
  }

  /* Add the new stream to all enabled iodevs at once to avoid offset
   * in shm level between different output iodevs. */
  num_iodevs = 0;
  iodev_reopened = false;
  DL_FOREACH (enabled_devs[rstream->direction], edev) {
    if (edev->dev == fallback_devs[rstream->direction]) {
      continue;
    }

    if (!cras_iodev_should_attach_stream(edev->dev, rstream)) {
      continue;
    }

    if (num_iodevs >= ARRAY_SIZE(iodevs)) {
      syslog(LOG_ERR, "too many enabled devices");
      break;
    }

    if (cras_iodev_is_open(edev->dev) &&
        (rstream->format.num_channels > edev->dev->format->num_channels) &&
        (rstream->format.num_channels <=
         edev->dev->info.max_supported_channels)) {
      /* Re-open the device with the format of the attached
       * stream if it has higher channel count than the
       * current format of the device, and doesn't exceed the
       * max_supported_channels of the device.
       * Fallback device will be transciently enabled during
       * the device re-opening.
       */
      MAINLOG(main_log, MAIN_THREAD_DEV_REOPEN, rstream->format.num_channels,
              edev->dev->format->num_channels, edev->dev->format->frame_rate);
      syslog(LOG_DEBUG, "re-open %s for higher channel count",
             edev->dev->info.name);
      possibly_enable_fallback(rstream->direction, false);
      restart_device_group(edev->dev->info.idx);
      iodev_reopened = true;
    } else {
      rc = init_device(edev->dev, rstream);
      if (rc) {
        /* Error log but don't return error here, because
         * stopping audio could block video playback.
         */
        struct cras_ionode* node = edev->dev->active_node;
        bool is_hfp_mic = node &&
                          (node->type == CRAS_NODE_TYPE_BLUETOOTH ||
                           node->type == CRAS_NODE_TYPE_BLUETOOTH_NB_MIC) &&
                          edev->dev->direction == CRAS_STREAM_INPUT;
        if (is_hfp_mic && rc == -EAGAIN) {
          // Transitioning from A2DP to HFP is triggered when the mic is
          // activated, in which case it will block all attempts to open
          // the HFP device until cleanup is finished.
          // Note this is not expected in the path from the other way around,
          // since A2DP is never opened until HFP is completely closed.
          expect_fallback = true;
        } else {
          syslog(LOG_WARNING, "Init %s failed, rc = %d", edev->dev->info.name,
                 rc);
        }
        schedule_init_device_retry(edev->dev);
        continue;
      }

      iodevs[num_iodevs++] = edev->dev;
    }
  }

  // Add the stream to flexible loopback devices
  if (rstream->direction == CRAS_STREAM_OUTPUT) {
    struct cras_floop_pair* fpair;
    DL_FOREACH (floop_pair_list, fpair) {
      if (!cras_floop_pair_match_output_stream(fpair, rstream)) {
        continue;
      }
      if (num_iodevs >= ARRAY_SIZE(iodevs)) {
        syslog(LOG_ERR, "too many enabled devices");
        break;
      }
      rc = init_device(&fpair->output, rstream);
      if (!rc) {
        iodevs[num_iodevs++] = &fpair->output;
      }
    }
  }

  if (num_iodevs) {
    /* Add stream failure is considered critical because it'll
     * trigger client side error. Collect the error type and send
     * for UMA metrics. */
    rc = add_stream_to_open_devs(rstream, iodevs, num_iodevs);
    if (rc == -EIO) {
      cras_server_metrics_stream_add_failure(CRAS_STREAM_ADD_IO_ERROR);
    } else if (rc == -EINVAL) {
      cras_server_metrics_stream_add_failure(CRAS_STREAM_ADD_INVALID_ARG);
    } else if (rc) {
      cras_server_metrics_stream_add_failure(CRAS_STREAM_ADD_OTHER_ERR);
    }

    if (rc) {
      syslog(LOG_ERR, "adding stream to thread fail, rc %d", rc);
      return rc;
    }
  } else if (!iodev_reopened) {
    /* Enable fallback device if no other iodevs can be initialized
     * or re-opened successfully.
     * For error codes like EAGAIN and ENOENT, a new iodev will be
     * enabled soon so streams are going to route there. As for the
     * rest of the error cases, silence will be played or recorded
     * so client won't be blocked.
     * The enabled fallback device will be disabled when
     * cras_iodev_list_select_node() is called to re-select the
     * active node.
     */
    possibly_enable_fallback(rstream->direction, !expect_fallback);
  }

  if (num_iodevs || iodev_reopened) {
    possibly_disable_fallback(rstream->direction);
  }

  return 0;
}

static int possibly_close_enabled_devs(enum CRAS_STREAM_DIRECTION dir) {
  struct enabled_dev* edev;
  const struct cras_rstream* s;

  syslog(LOG_DEBUG, "possibly_close_enabled_devs(dir=%d)", dir);

  // Check if there are still default streams attached.
  DL_FOREACH (stream_list_get(stream_list), s) {
    if (s->direction == dir && !s->is_pinned) {
      return 0;
    }
  }

  /* No more default streams, close any device that doesn't have a stream
   * pinned to it. */
  DL_FOREACH (enabled_devs[dir], edev) {
    if (cras_iodev_has_pinned_stream(edev->dev)) {
      continue;
    }
    if (dir == CRAS_STREAM_INPUT) {
      if (edev->dev->active_node->type == CRAS_NODE_TYPE_FLOOP) {
        clock_gettime(CLOCK_MONOTONIC_RAW, &edev->dev->idle_timeout);
        add_timespecs(&edev->dev->idle_timeout, &idle_timeout_interval);
        idle_floop_check(NULL, NULL);
      }
      close_dev(edev->dev);
      continue;
    }
    // If output stream is empty, set loopback devices to stop
    struct cras_loopback* loopback;
    DL_FOREACH (edev->dev->loopbacks, loopback) {
      if (loopback->hook_control) {
        loopback->hook_control(false, loopback->cb_data);
      }
    }

    // Allow output devs to drain before closing.
    clock_gettime(CLOCK_MONOTONIC_RAW, &edev->dev->idle_timeout);
    add_timespecs(&edev->dev->idle_timeout, &idle_timeout_interval);
    idle_dev_check(NULL, NULL);
  }

  return 0;
}

static void pinned_stream_removed(struct cras_rstream* rstream) {
  struct cras_iodev* dev;

  dev = find_pinned_device(rstream);
  if (!dev) {
    return;
  }

  syslog(LOG_DEBUG, "pinned_stream_removed(stream=0x%x, dev=%s)",
         rstream->stream_id, dev->info.name);

  size_t group_size;
  struct cras_iodev* const* group = cras_iodev_get_dev_group(dev, &group_size);

  if (!group) {
    group = &dev;
    group_size = 1;
  }

  for (size_t i = 0; i < group_size; i++) {
    if (!cras_iodev_should_attach_stream(group[i], rstream)) {
      continue;
    }

    // The stream has already been drained at this point.
    if (group[i]->num_pinned_streams) {
      group[i]->num_pinned_streams--;
    } else {
      // This only happens if cras_iodev_should_attach_stream returned
      // inconsistent result at pinned_stream_add.
      syslog(LOG_ERR, "Device has no pinned stream: %s", group[i]->info.name);
    }

    if (!cras_iodev_list_dev_is_enabled(group[i]) &&
        !cras_iodev_has_pinned_stream(group[i])) {
      close_pinned_device(group[i]);
    }
  }
}

/* Returns the number of milliseconds left to drain this stream.  This is passed
 * directly from the audio thread. */
static int stream_removed_cb(struct cras_rstream* rstream) {
  struct timespec now, time_since;
  enum CRAS_STREAM_DIRECTION direction = rstream->direction;
  struct cras_iodev* dev = (struct cras_iodev*)rstream->main_dev.dev_ptr;
  int rc;

  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  subtract_timespecs(&now, &rstream->start_ts, &time_since);
  if (time_since.tv_sec >= CRAS_HATS_GENERAL_SURVEY_STREAM_LIVE_SEC) {
    cras_hats_trigger_general_survey(rstream->stream_type, rstream->client_type,
                                     cras_system_state_get_active_node_types());
  }

  if (time_since.tv_sec >= CRAS_HATS_BLUETOOTH_SURVEY_STREAM_LIVE_SEC) {
    if (dev && dev->active_node) {
      cras_hats_trigger_bluetooth_survey(dev->active_node->btflags);
    }
  }

  rc = audio_thread_drain_stream(audio_thread, rstream);
  if (rc) {
    return rc;
  }

  MAINLOG(main_log, MAIN_THREAD_STREAM_REMOVED, rstream->stream_id, 0, 0);

  if (rstream->is_pinned) {
    pinned_stream_removed(rstream);
  }

  possibly_close_enabled_devs(direction);

  return 0;
}

static bool can_use_dsp_aec(struct cras_rstream* all_streams) {
  struct cras_rstream* stream;
  DL_FOREACH (all_streams, stream) {
    if (stream->direction != CRAS_STREAM_INPUT) {
      continue;
    }
    if (cras_iodev_list_is_utility_stream(stream)) {
      continue;
    }

    unsigned int effects = cras_rstream_get_effects(stream);
    if (effects & PRIVATE_DONT_CARE_APM_EFFECTS) {
      continue;
    }

    // Block DSP AEC if any stream cannot use DSP AEC.
    unsigned int dsp_aec_bits =
        APM_ECHO_CANCELLATION | DSP_ECHO_CANCELLATION_ALLOWED;
    if ((effects & dsp_aec_bits) != dsp_aec_bits) {
      return false;
    }
  }
  return true;
}

static int stream_list_changed_cb(struct cras_rstream* all_streams) {
  cras_speak_on_mute_detector_streams_changed(all_streams);
  set_aec_on_dsp_is_disallowed(!can_use_dsp_aec(all_streams));

  return 0;
}

static int enable_device(struct cras_iodev* dev) {
  int rc;
  struct enabled_dev* edev;
  enum CRAS_STREAM_DIRECTION dir = dev->direction;
  struct device_enabled_cb* callback;

  syslog(LOG_DEBUG, "enable_device(dev=%s)", dev->info.name);

  DL_FOREACH (enabled_devs[dir], edev) {
    if (edev->dev == dev) {
      return -EEXIST;
    }
  }

  edev = (struct enabled_dev*)calloc(1, sizeof(*edev));
  edev->dev = dev;
  DL_APPEND(enabled_devs[dir], edev);
  dev->is_enabled = 1;

  rc = init_and_attach_streams(dev);
  if (rc < 0) {
    syslog(LOG_ERR, "Enable device fail, rc %d", rc);
    schedule_init_device_retry(dev);
    return rc;
  }

  DL_FOREACH (device_enable_cbs, callback) {
    callback->enabled_cb(dev, callback->cb_data);
  }

  possibly_set_non_dsp_aec_echo_ref_dev_alive(dev);

  return 0;
}

// Enable the iodev group containing the given iodev.
static int enable_device_group(struct cras_iodev* dev) {
  size_t size;
  struct cras_iodev* const* group = cras_iodev_get_dev_group(dev, &size);

  syslog(LOG_DEBUG, "enable_device_group(dev=%s)", dev->info.name);

  if (!group) {
    return enable_device(dev);
  }

  int first_err = 0;
  for (size_t i = 0; i < size; i++) {
    int rc = enable_device(group[i]);
    // Continue enabling other iodevs in the group even if an iodev failed.
    // Failed iodevs are scheduled for init retry.
    if (rc) {
      syslog(LOG_ERR, "Enable device group failed for device %s, rc %d",
             group[i]->info.name, rc);
      if (!first_err) {
        first_err = rc;
      }
    }
  }

  return first_err;
}

// Set force to true to flush any pinned streams before closing the device.
static int disable_device(struct enabled_dev* edev, bool force) {
  struct cras_iodev* dev = edev->dev;
  enum CRAS_STREAM_DIRECTION dir = dev->direction;
  struct cras_rstream* stream;
  struct device_enabled_cb* callback;

  MAINLOG(main_log, MAIN_THREAD_DEV_DISABLE, dev->info.idx, force, 0);
  syslog(LOG_DEBUG, "disable_device(edev=%s,force=%d)", dev->info.name, force);

  /*
   * Remove from enabled dev list. However this dev could have a stream
   * pinned to it, only cancel pending init timers when force flag is set.
   */
  DL_DELETE(enabled_devs[dir], edev);
  free(edev);
  dev->is_enabled = 0;
  if (force) {
    cancel_pending_init_retries(dev->info.idx);
  }
  /* If there's a pinned stream exists, simply disconnect all the normal
   * streams off this device and return. */
  else if (cras_iodev_has_pinned_stream(dev)) {
    DL_FOREACH (stream_list_get(stream_list), stream) {
      if (stream->direction != dev->direction) {
        continue;
      }
      if (stream->is_pinned) {
        continue;
      }
      audio_thread_disconnect_stream(audio_thread, stream, dev);
    }
    return 0;
  }

  DL_FOREACH (device_enable_cbs, callback) {
    callback->disabled_cb(dev, callback->cb_data);
  }
  close_dev(dev);

  // Update active node after the last iodev in a group is closed.
  if (!cras_iodev_group_has_open_dev(dev)) {
    // dev may be any iodev in the group, not necessarily the HiFi iodev.
    dev->update_active_node(dev, dev->active_node->idx, 0);
  }
  possibly_clear_non_dsp_aec_echo_ref_dev_alive();
  return 0;
}

// Disable all enabled iodevs except the ones in the given selected dev group.
// This is needed during node switch to maintain the expected CRAS behavior:
// When user selects a new node, all other nodes (either previously selected by
// user or added by cras_iodev_list_add_active_node) are turned off.
static void disable_unselected_devices(enum CRAS_STREAM_DIRECTION direction,
                                       struct cras_iodev* selected_dev_group) {
  struct enabled_dev* edev;

  syslog(LOG_DEBUG, "disable_unselected_devices(dir=%d,sel=%s)", direction,
         selected_dev_group ? selected_dev_group->info.name : "None");

  DL_FOREACH (enabled_devs[direction], edev) {
    // Don't disable fallback iodevs.
    if (is_fallback_dev(edev->dev)) {
      continue;
    }

    // Don't disable iodevs in the user selected group.
    if (cras_iodev_in_same_group(edev->dev, selected_dev_group)) {
      continue;
    }

    // Disable all other iodev while keeping pinned streams.
    disable_device(edev, false);
  }
}

// Disable the iodev group containing the given iodev.
static size_t disable_device_group(struct cras_iodev* dev, bool force) {
  enum CRAS_STREAM_DIRECTION direction = dev->direction;
  struct enabled_dev* edev;
  bool has_other_enabled_device = false;

  syslog(LOG_DEBUG, "disable_device_group(dev=%s,force=%d)", dev->info.name,
         force);

  // Enable fallback if there is no other enabled device.
  DL_FOREACH (enabled_devs[direction], edev) {
    if (!cras_iodev_in_same_group(edev->dev, dev)) {
      has_other_enabled_device = true;
      break;
    }
  }
  if (!has_other_enabled_device) {
    possibly_enable_fallback(dev->direction, false);
  }

  // no-op for already disabled iodevs running pinned streams.
  size_t num_disabled_devs = 0;
  DL_FOREACH (enabled_devs[direction], edev) {
    if (cras_iodev_in_same_group(edev->dev, dev)) {
      disable_device(edev, force);
      num_disabled_devs++;
    }
  }

  return num_disabled_devs;
}

/*
 * Exported Interface.
 */

void cras_iodev_list_init() {
  struct cras_observer_ops observer_ops;

  memset(&observer_ops, 0, sizeof(observer_ops));
  observer_ops.output_volume_changed = sys_vol_change;
  observer_ops.output_mute_changed = sys_mute_change;
  observer_ops.suspend_changed = sys_suspend_change;
  observer_ops.nodes_changed = nodes_changed;
  list_observer = cras_observer_add(&observer_ops, NULL);
  idle_timer = NULL;
  floop_timer = NULL;
  non_dsp_aec_echo_ref_dev_alive = false;
  aec_on_dsp_is_disallowed = false;

  main_log = main_thread_event_log_init();

  // Create the audio stream list for the system.
  stream_list = stream_list_create(
      stream_added_cb, stream_removed_cb, cras_rstream_create,
      cras_rstream_destroy, stream_list_changed_cb, cras_system_state_get_tm());

  /* Add an empty device so there is always something to play to or
   * capture from. */
  fallback_devs[CRAS_STREAM_OUTPUT] =
      empty_iodev_create(CRAS_STREAM_OUTPUT, CRAS_NODE_TYPE_FALLBACK_NORMAL);
  fallback_devs[CRAS_STREAM_INPUT] =
      empty_iodev_create(CRAS_STREAM_INPUT, CRAS_NODE_TYPE_FALLBACK_NORMAL);
  enable_device(fallback_devs[CRAS_STREAM_OUTPUT]);
  enable_device(fallback_devs[CRAS_STREAM_INPUT]);

  empty_hotword_dev =
      empty_iodev_create(CRAS_STREAM_INPUT, CRAS_NODE_TYPE_HOTWORD);

  // Create loopback devices.
  loopdev_post_mix = loopback_iodev_create(LOOPBACK_POST_MIX_PRE_DSP);
  loopdev_post_dsp = loopback_iodev_create(LOOPBACK_POST_DSP);
  loopdev_post_dsp_delayed = loopback_iodev_create(LOOPBACK_POST_DSP_DELAYED);

  audio_thread = audio_thread_create();
  if (!audio_thread) {
    syslog(LOG_ERR, "Fatal: audio thread init");
    exit(-ENOMEM);
  }
  audio_thread_start(audio_thread);

  cras_iodev_list_update_device_list();
}

void cras_iodev_list_deinit() {
  audio_thread_destroy(audio_thread);
  loopback_iodev_destroy(loopdev_post_dsp);
  loopback_iodev_destroy(loopdev_post_mix);
  loopback_iodev_destroy(loopdev_post_dsp_delayed);
  empty_iodev_destroy(empty_hotword_dev);
  empty_iodev_destroy(fallback_devs[CRAS_STREAM_INPUT]);
  empty_iodev_destroy(fallback_devs[CRAS_STREAM_OUTPUT]);
  stream_list_destroy(stream_list);
  main_thread_event_log_deinit(main_log);
  if (list_observer) {
    cras_observer_remove(list_observer);
    list_observer = NULL;
  }
}

int cras_iodev_list_dev_is_enabled(const struct cras_iodev* dev) {
  return dev && dev->is_enabled;
}

void cras_iodev_list_add_active_node(enum CRAS_STREAM_DIRECTION dir,
                                     cras_node_id_t node_id) {
  struct cras_iodev* new_dev = find_dev(dev_index_of(node_id));

  if (!new_dev || new_dev->direction != dir) {
    return;
  }

  MAINLOG(main_log, MAIN_THREAD_ADD_ACTIVE_NODE, new_dev->info.idx, 0, 0);
  syslog(LOG_DEBUG, "cras_iodev_list_add_active_node(new_dev=%s,node=%d)",
         new_dev->info.name, node_index_of(node_id));

  if (cras_iodev_list_dev_is_enabled(new_dev) &&
      new_dev->active_node->idx == node_index_of(node_id)) {
    return;
  }

  /* Possible states of the iodev group containing new_dev:
   * 1. disabled, closed
   * 2. disabled, running pinned streams
   * 3. enabled, closed
   * 4. enabled, running pinned streams
   * 5. enabled, running normal streams
   * 6. enabled, running normal + pinned streams
   *
   * If enabled, it's a node switch within group, so need to flush all streams.
   */
  cras_iodev_list_disable_and_close_dev_group(new_dev);

  // Enable ucm setting of active node.
  new_dev->update_active_node(new_dev, node_index_of(node_id), 1);
  if (enable_device_group(new_dev) == 0) {
    possibly_disable_fallback(new_dev->direction);
  }
  cras_iodev_list_notify_active_node_changed(new_dev->direction);
}

void cras_iodev_list_disable_and_close_dev_group(struct cras_iodev* dev) {
  syslog(LOG_DEBUG, "cras_iodev_list_disable_and_close_dev_group(dev=%s)",
         dev->info.name);

  size_t num_disabled_devs = disable_device_group(dev, true);
  size_t num_closed_devs = 0;
  size_t size;
  struct cras_iodev* const* group = cras_iodev_get_dev_group(dev, &size);

  // Close the dev group if it has pinned stream but is already disabled.
  // TODO(b/297347634) Remove the close_dev after refactoring pinned stream.
  if (!group) {
    group = &dev;
    size = 1;
  }
  for (size_t i = 0; i < size; i++) {
    if (cras_iodev_is_open(group[i])) {
      close_dev(group[i]);
      num_closed_devs++;
    }
  }
  if (num_closed_devs) {
    dev->update_active_node(dev, dev->active_node->idx, 0);
  }

  if (num_disabled_devs) {
    cras_iodev_list_notify_active_node_changed(dev->direction);
  }
}

void cras_iodev_list_suspend_dev(unsigned int dev_idx) {
  struct cras_iodev* dev = find_dev(dev_idx);

  if (!dev) {
    return;
  }

  /* Remove all streams including the pinned streams, and close
   * this iodev. */
  close_dev(dev);
  dev->update_active_node(dev, dev->active_node->idx, 0);
}

void cras_iodev_list_resume_dev(unsigned int dev_idx) {
  struct cras_iodev* dev = find_dev(dev_idx);
  int rc;

  if (!dev) {
    return;
  }

  dev->update_active_node(dev, dev->active_node->idx, 1);
  rc = init_and_attach_streams(dev);
  if (rc == 0) {
    /* If dev initialize succeeded and this is not a pinned device,
     * disable the silent fallback device because it's just
     * unnecessary. */
    if (!cras_iodev_has_pinned_stream(dev)) {
      possibly_disable_fallback(dev->direction);
    }
  } else {
    syslog(LOG_ERR, "Enable dev fail at resume, rc %d", rc);
    schedule_init_device_retry(dev);
  }
}

void cras_iodev_list_set_dev_mute(unsigned int dev_idx) {
  struct cras_iodev* dev;

  dev = find_dev(dev_idx);
  if (!dev) {
    return;
  }

  cras_iodev_set_mute(dev);
}

void cras_iodev_list_rm_active_node(enum CRAS_STREAM_DIRECTION dir,
                                    cras_node_id_t node_id) {
  struct cras_iodev* dev = find_dev(dev_index_of(node_id));

  if (!dev || dev->direction != dir) {
    return;
  }

  syslog(LOG_DEBUG, "cras_iodev_list_rm_active_node(dev=%s,node=%d)",
         dev->info.name, node_index_of(node_id));

  // Disable the unselected iodev group but keep its pinned streams.
  disable_device_group(dev, false);
  cras_iodev_list_notify_active_node_changed(dev->direction);
}

int cras_iodev_list_add_output(struct cras_iodev* output) {
  int rc;

  if (output->direction != CRAS_STREAM_OUTPUT) {
    return -EINVAL;
  }

  rc = add_dev_to_list(output);
  if (rc) {
    return rc;
  }

  MAINLOG(main_log, MAIN_THREAD_ADD_TO_DEV_LIST, output->info.idx,
          CRAS_STREAM_OUTPUT, 0);
  return 0;
}

int cras_iodev_list_add_input(struct cras_iodev* input) {
  int rc;

  if (input->direction != CRAS_STREAM_INPUT) {
    return -EINVAL;
  }

  rc = add_dev_to_list(input);
  if (rc) {
    return rc;
  }

  MAINLOG(main_log, MAIN_THREAD_ADD_TO_DEV_LIST, input->info.idx,
          CRAS_STREAM_INPUT, 0);
  return 0;
}

int cras_iodev_list_rm(struct cras_iodev* dev) {
  int res;

  /* Disable and close the dev group if any device in it is about to be removed.
   * The dev group is enabled/disabled as a whole to ensure:
   * 1. Fallback device is enabled properly. A partially disabled dev group may
   *    drop some streams and block clients.
   * 2. No stream is running if the dev group's node owner is removed.
   */
  cras_iodev_list_disable_and_close_dev_group(dev);
  res = rm_dev_from_list(dev);
  if (res == 0) {
    cras_iodev_list_update_device_list();
  }
  return res;
}

int cras_iodev_list_get_outputs(struct cras_iodev_info** list_out) {
  return get_dev_list(&devs[CRAS_STREAM_OUTPUT], list_out);
}

int cras_iodev_list_get_inputs(struct cras_iodev_info** list_out) {
  return get_dev_list(&devs[CRAS_STREAM_INPUT], list_out);
}

struct cras_iodev* cras_iodev_list_get_first_enabled_iodev(
    enum CRAS_STREAM_DIRECTION direction) {
  struct enabled_dev* edev = enabled_devs[direction];

  return edev ? edev->dev : NULL;
}

struct cras_iodev* cras_iodev_list_get_sco_pcm_iodev(
    enum CRAS_STREAM_DIRECTION direction) {
  struct cras_iodev* dev;
  struct cras_ionode* node;

  DL_FOREACH (devs[direction].iodevs, dev) {
    DL_FOREACH (dev->nodes, node) {
      if (node->btflags == CRAS_BT_FLAG_SCO_OFFLOAD) {
        return dev;
      }
    }
  }

  return NULL;
}

cras_node_id_t cras_iodev_list_get_active_node_id(
    enum CRAS_STREAM_DIRECTION direction) {
  struct enabled_dev* edev = enabled_devs[direction];

  if (!edev || !edev->dev || !edev->dev->active_node) {
    return 0;
  }

  return cras_make_node_id(edev->dev->info.idx, edev->dev->active_node->idx);
}

void cras_iodev_list_update_device_list() {
  struct fill_node_list_auxiliary aux = get_fill_node_list_auxiliary();

  struct cras_server_state* state = cras_system_state_update_begin();
  if (!state) {
    return;
  }

  state->num_output_devs = devs[CRAS_STREAM_OUTPUT].size;
  state->num_input_devs = devs[CRAS_STREAM_INPUT].size;
  fill_dev_list(&devs[CRAS_STREAM_OUTPUT], &state->output_devs[0],
                CRAS_MAX_IODEVS);
  fill_dev_list(&devs[CRAS_STREAM_INPUT], &state->input_devs[0],
                CRAS_MAX_IODEVS);

  state->num_output_nodes =
      fill_node_list(&devs[CRAS_STREAM_OUTPUT], &state->output_nodes[0],
                     CRAS_MAX_IONODES, &aux);
  state->num_input_nodes = fill_node_list(
      &devs[CRAS_STREAM_INPUT], &state->input_nodes[0], CRAS_MAX_IONODES, &aux);

  cras_system_state_update_complete();
}

// Look up the first hotword stream and the device it pins to.
int find_hotword_stream_dev(struct cras_iodev** dev,
                            struct cras_rstream** stream) {
  DL_FOREACH (stream_list_get(stream_list), *stream) {
    if (((*stream)->flags & HOTWORD_STREAM) != HOTWORD_STREAM) {
      continue;
    }

    *dev = find_dev((*stream)->pinned_dev_idx);
    if (*dev == NULL) {
      return -ENOENT;
    }
    break;
  }
  return 0;
}

/* Suspend/resume hotword streams functions are used to provide seamless
 * experience to cras clients when there's hardware limitation about concurrent
 * DSP and normal recording. The empty hotword iodev is used to hold all
 * hotword streams during suspend, so client side will not know about the
 * transition, and can still remove or add streams. At resume, the real hotword
 * device will be initialized and opened again to re-arm the DSP.
 */
int cras_iodev_list_suspend_hotword_streams() {
  struct cras_iodev* hotword_dev;
  struct cras_rstream* stream = NULL;
  int rc;

  rc = find_hotword_stream_dev(&hotword_dev, &stream);
  if (rc) {
    return rc;
  }

  if (stream == NULL) {
    hotword_suspended = 1;
    return 0;
  }
  // Move all existing hotword streams to the empty hotword iodev.
  init_pinned_device(empty_hotword_dev, stream);
  DL_FOREACH (stream_list_get(stream_list), stream) {
    if ((stream->flags & HOTWORD_STREAM) != HOTWORD_STREAM) {
      continue;
    }
    if (stream->pinned_dev_idx != hotword_dev->info.idx) {
      syslog(LOG_ERR, "Failed to suspend hotword stream on dev %u",
             stream->pinned_dev_idx);
      continue;
    }

    audio_thread_disconnect_stream(audio_thread, stream, hotword_dev);
    audio_thread_add_stream(audio_thread, stream, &empty_hotword_dev, 1);
  }
  close_pinned_device(hotword_dev);
  hotword_suspended = 1;
  return 0;
}

int cras_iodev_list_resume_hotword_stream() {
  struct cras_iodev* hotword_dev;
  struct cras_rstream* stream = NULL;
  int rc;

  rc = find_hotword_stream_dev(&hotword_dev, &stream);
  if (rc) {
    return rc;
  }

  if (stream == NULL) {
    hotword_suspended = 0;
    return 0;
  }
  // Move all existing hotword streams to the real hotword iodev.
  init_pinned_device(hotword_dev, stream);
  DL_FOREACH (stream_list_get(stream_list), stream) {
    if ((stream->flags & HOTWORD_STREAM) != HOTWORD_STREAM) {
      continue;
    }
    if (stream->pinned_dev_idx != hotword_dev->info.idx) {
      syslog(LOG_ERR, "Fail to resume hotword stream on dev %u",
             stream->pinned_dev_idx);
      continue;
    }

    audio_thread_disconnect_stream(audio_thread, stream, empty_hotword_dev);
    audio_thread_add_stream(audio_thread, stream, &hotword_dev, 1);
  }
  close_pinned_device(empty_hotword_dev);
  hotword_suspended = 0;
  return 0;
}

char* cras_iodev_list_get_hotword_models(cras_node_id_t node_id) {
  struct cras_iodev* dev = NULL;

  dev = find_dev(dev_index_of(node_id));
  if (!dev || !dev->get_hotword_models ||
      (dev->active_node->type != CRAS_NODE_TYPE_HOTWORD)) {
    return NULL;
  }

  return dev->get_hotword_models(dev);
}

int cras_iodev_list_set_hotword_model(cras_node_id_t node_id,
                                      const char* model_name) {
  int ret;
  struct cras_iodev* dev = find_dev(dev_index_of(node_id));
  if (!dev || !dev->get_hotword_models ||
      (dev->active_node->type != CRAS_NODE_TYPE_HOTWORD)) {
    return -EINVAL;
  }

  ret = dev->set_hotword_model(dev, model_name);
  if (!ret) {
    strncpy(dev->active_node->active_hotword_model, model_name,
            sizeof(dev->active_node->active_hotword_model) - 1);
  }
  return ret;
}

void cras_iodev_list_notify_nodes_changed() {
  cras_observer_notify_nodes();
}

void cras_iodev_list_notify_active_node_changed(
    enum CRAS_STREAM_DIRECTION direction) {
  syslog(LOG_DEBUG, "cras_iodev_list_notify_active_node_changed(dir=%d)",
         direction);
  cras_observer_notify_active_node(
      direction, cras_iodev_list_get_active_node_id(direction));
}

void cras_iodev_list_select_node(enum CRAS_STREAM_DIRECTION direction,
                                 cras_node_id_t node_id) {
  struct cras_iodev* new_dev = NULL;
  int new_node_already_enabled = 0;

  // find the devices for the id.
  new_dev = find_dev(dev_index_of(node_id));

  MAINLOG(main_log, MAIN_THREAD_SELECT_NODE, dev_index_of(node_id), 0, 0);

  /* Do nothing if the direction is mismatched. The new_dev == NULL case
     could happen if node_id is 0 (no selection), or the client tries
     to select a non-existing node (maybe it's unplugged just before
     the client selects it). We will just behave like there is no selected
     node. */
  if (new_dev && new_dev->direction != direction) {
    return;
  }

  /* Determine whether the new device and node are already enabled - if
   * they are, the selection algorithm should avoid disabling the new
   * device. */
  new_node_already_enabled =
      cras_iodev_list_dev_is_enabled(new_dev) &&
      new_dev->active_node->idx == node_index_of(node_id);

  syslog(LOG_DEBUG, "select_node: new_dev=%s, new_node=%d, already_enabled=%d",
         new_dev ? new_dev->info.name : "None", node_index_of(node_id),
         new_node_already_enabled);

  /* Enable fallback device during the transition so client will not be
   * blocked in this duration, which is as long as 300 ms on some boards
   * before new device is opened.
   * Note that the fallback node is not needed if the new node is already
   * enabled - the new node will remain enabled. */
  if (!new_node_already_enabled) {
    possibly_enable_fallback(direction, false);
  }

  /* All iodevs in a group are enabled/disabled together. All nodes on the
   * iodevs in the group are shared and owned by the group. The group is
   * enabled iff a node on one of the group's iodev is selected. */
  disable_unselected_devices(direction, new_dev);

  if (new_dev && !new_node_already_enabled) {
    /* Flush pinned streams when switching nodes on the same group.
     * The new_dev group may not be enabled, so disable_device_group(dev, true)
     * is not enough.
     * TODO(b/266790044) fix double active node change notify. */
    cras_iodev_list_disable_and_close_dev_group(new_dev);

    new_dev->update_active_node(new_dev, node_index_of(node_id), 1);

    /* To reduce the popped noise of active device change, mute
     * new_dev's for RAMP_SWITCH_MUTE_DURATION_SECS s.
     */
    if (direction == CRAS_STREAM_OUTPUT &&
        stream_list_get_num_output(stream_list)) {
      new_dev->initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_SWITCH_MUTE;
    }

    if (enable_device_group(new_dev) == 0) {
      /* Disable fallback device after new iodev group is enabled.
       * Leave the fallback device enabled if any iodev in the group failed
       * to open, or the new_dev == NULL case. */
      possibly_disable_fallback(direction);
    }
  }

  cras_iodev_list_notify_active_node_changed(direction);
}

static int set_node_plugged(struct cras_iodev* iodev,
                            unsigned int node_idx,
                            int plugged) {
  struct cras_ionode* node;

  node = find_node(iodev, node_idx);
  if (!node) {
    return -EINVAL;
  }
  syslog(LOG_DEBUG, "set_node_plugged(dev=%s,node=%d,plugged=%d)",
         iodev->info.name, node_index_of(node_idx), plugged);

  cras_iodev_set_node_plugged(node, plugged);
  return 0;
}

static int set_node_volume(struct cras_iodev* iodev,
                           unsigned int node_idx,
                           int volume) {
  struct cras_ionode* node;

  node = find_node(iodev, node_idx);
  if (!node) {
    syslog(LOG_WARNING, "Cannot find input: node == null:");
    return -EINVAL;
  }

  if (volume < 0 || volume > 100) {
    syslog(LOG_WARNING, "Invalid volume: %d", volume);
    return -EINVAL;
  }

  if (iodev->ramp && cras_iodev_software_volume_needed(iodev) &&
      !cras_system_get_mute()) {
    cras_iodev_start_volume_ramp(iodev, node->volume, volume);
  }

  node->volume = volume;
  if (iodev->set_volume) {
    iodev->set_volume(iodev);
  }
  cras_iodev_list_notify_node_volume(node);
  MAINLOG(main_log, MAIN_THREAD_OUTPUT_NODE_VOLUME, iodev->info.idx, volume, 0);
  return 0;
}

static int set_node_capture_gain(struct cras_iodev* iodev,
                                 unsigned int node_idx,
                                 int value) {
  struct cras_ionode* node;

  node = find_node(iodev, node_idx);
  if (!node) {
    return -EINVAL;
  }

  node->ui_gain_scaler =
      convert_softvol_scaler_from_dB(convert_dBFS_from_input_node_gain(
          value, cras_iodev_is_node_internal_mic(node)));

  cras_iodev_list_notify_node_capture_gain(node, value);
  MAINLOG(main_log, MAIN_THREAD_INPUT_NODE_GAIN, iodev->info.idx, value, 0);
  return 0;
}

static int set_node_display_rotation(struct cras_iodev* iodev,
                                     unsigned int node_idx,
                                     enum CRAS_SCREEN_ROTATION rotation) {
  struct cras_ionode* node;
  int rc;

  if (!iodev->set_display_rotation_for_node) {
    return -EINVAL;
  }
  node = find_node(iodev, node_idx);
  if (!node) {
    return -EINVAL;
  }

  rc = iodev->set_display_rotation_for_node(iodev, node, rotation);
  if (rc) {
    syslog(LOG_ERR, "Failed to set display_rotation on node %s to %d",
           node->name, rotation);
    return rc;
  }
  node->display_rotation = rotation;
  return 0;
}

static int set_node_left_right_swapped(struct cras_iodev* iodev,
                                       unsigned int node_idx,
                                       int left_right_swapped) {
  struct cras_ionode* node;
  int rc;

  if (!iodev->set_swap_mode_for_node) {
    return -EINVAL;
  }
  node = find_node(iodev, node_idx);
  if (!node) {
    return -EINVAL;
  }

  rc = iodev->set_swap_mode_for_node(iodev, node, left_right_swapped);
  if (rc) {
    syslog(LOG_ERR, "Failed to set swap mode on node %s to %d", node->name,
           left_right_swapped);
    return rc;
  }
  node->left_right_swapped = left_right_swapped;
  cras_iodev_list_notify_node_left_right_swapped(node);
  return 0;
}

int cras_iodev_list_set_node_attr(cras_node_id_t node_id,
                                  enum ionode_attr attr,
                                  int value) {
  struct cras_iodev* iodev;
  int rc = 0;

  iodev = find_dev(dev_index_of(node_id));
  if (!iodev) {
    return -EINVAL;
  }

  switch (attr) {
    case IONODE_ATTR_PLUGGED:
      rc = set_node_plugged(iodev, node_index_of(node_id), value);
      break;
    case IONODE_ATTR_VOLUME:
      rc = set_node_volume(iodev, node_index_of(node_id), value);
      break;
    case IONODE_ATTR_CAPTURE_GAIN:
      rc = set_node_capture_gain(iodev, node_index_of(node_id), value);
      break;
    case IONODE_ATTR_DISPLAY_ROTATION:
      rc = set_node_display_rotation(iodev, node_index_of(node_id),
                                     (enum CRAS_SCREEN_ROTATION)value);
      break;
    case IONODE_ATTR_SWAP_LEFT_RIGHT:
      rc = set_node_left_right_swapped(iodev, node_index_of(node_id), value);
      break;
    default:
      return -EINVAL;
  }

  return rc;
}

void cras_iodev_list_notify_node_volume(struct cras_ionode* node) {
  cras_node_id_t id = cras_make_node_id(node->dev->info.idx, node->idx);
  cras_iodev_list_update_device_list();
  cras_observer_notify_output_node_volume(id, node->volume);
}

void cras_iodev_list_notify_node_left_right_swapped(struct cras_ionode* node) {
  cras_node_id_t id = cras_make_node_id(node->dev->info.idx, node->idx);
  cras_iodev_list_update_device_list();
  cras_observer_notify_node_left_right_swapped(id, node->left_right_swapped);
}

void cras_iodev_list_notify_node_capture_gain(struct cras_ionode* node,
                                              int gain) {
  cras_node_id_t id = cras_make_node_id(node->dev->info.idx, node->idx);
  cras_iodev_list_update_device_list();
  cras_observer_notify_input_node_gain(id, gain);
}

void cras_iodev_list_add_test_dev(enum TEST_IODEV_TYPE type) {
  if (type != TEST_IODEV_HOTWORD) {
    return;
  }
  test_iodev_create(CRAS_STREAM_INPUT, type);
}

void cras_iodev_list_test_dev_command(unsigned int iodev_idx,
                                      enum CRAS_TEST_IODEV_CMD command,
                                      unsigned int data_len,
                                      const uint8_t* data) {
  struct cras_iodev* dev = find_dev(iodev_idx);

  if (!dev) {
    return;
  }

  test_iodev_command(dev, command, data_len, data);
}

struct audio_thread* cras_iodev_list_get_audio_thread() {
  return audio_thread;
}

struct stream_list* cras_iodev_list_get_stream_list() {
  return stream_list;
}

int cras_iodev_list_set_device_enabled_callback(
    device_enabled_callback_t enabled_cb,
    device_disabled_callback_t disabled_cb,
    device_removed_callback_t removed_cb,
    void* cb_data) {
  struct device_enabled_cb* callback;

  DL_FOREACH (device_enable_cbs, callback) {
    if (callback->cb_data != cb_data) {
      continue;
    }

    DL_DELETE(device_enable_cbs, callback);
    free(callback);
  }

  if (enabled_cb && disabled_cb) {
    callback = (struct device_enabled_cb*)calloc(1, sizeof(*callback));
    callback->enabled_cb = enabled_cb;
    callback->disabled_cb = disabled_cb;
    callback->removed_cb = removed_cb;
    callback->cb_data = cb_data;
    DL_APPEND(device_enable_cbs, callback);
  }

  return 0;
}

void cras_iodev_list_register_loopback(enum CRAS_LOOPBACK_TYPE loopback_type,
                                       unsigned int output_dev_idx,
                                       loopback_hook_data_t hook_data,
                                       loopback_hook_control_t hook_control,
                                       unsigned int loopback_dev_idx) {
  struct cras_iodev* iodev = find_dev(output_dev_idx);
  if (iodev == NULL) {
    syslog(LOG_ERR, "Output dev %u not found for loopback", output_dev_idx);
    return;
  }

  struct cras_iodev* loopback_dev = find_dev(loopback_dev_idx);
  if (loopback_dev == NULL) {
    syslog(LOG_ERR, "Loopback dev %u not found", loopback_dev_idx);
    return;
  }

  struct cras_loopback* loopback =
      (struct cras_loopback*)calloc(1, sizeof(*loopback));
  if (NULL == loopback) {
    syslog(LOG_ERR, "Not enough memory for loopback");
    return;
  }

  loopback->type = loopback_type;
  loopback->hook_data = hook_data;
  loopback->hook_control = hook_control;
  loopback->cb_data = loopback_dev;

  bool stream_running = cras_iodev_is_open(iodev) &&
                        iodev->state != CRAS_IODEV_STATE_NO_STREAM_RUN;
  if (loopback->hook_control && stream_running) {
    loopback->hook_control(true, loopback->cb_data);
  }

  DL_APPEND(iodev->loopbacks, loopback);
}

void cras_iodev_list_unregister_loopback(enum CRAS_LOOPBACK_TYPE type,
                                         unsigned int output_dev_idx,
                                         unsigned int loopback_dev_idx) {
  struct cras_iodev* iodev = find_dev(output_dev_idx);
  struct cras_iodev* loopback_dev;
  struct cras_loopback* loopback;

  if (iodev == NULL) {
    return;
  }

  loopback_dev = find_dev(loopback_dev_idx);
  if (loopback_dev == NULL) {
    return;
  }

  DL_FOREACH (iodev->loopbacks, loopback) {
    if ((loopback->cb_data == loopback_dev) && (loopback->type == type)) {
      DL_DELETE(iodev->loopbacks, loopback);
      free(loopback);
    }
  }
}

void cras_iodev_list_reset_for_noise_cancellation() {
  struct cras_iodev* dev;
  bool enabled = cras_system_get_noise_cancellation_enabled();

  DL_FOREACH (devs[CRAS_STREAM_INPUT].iodevs, dev) {
    if (!cras_iodev_is_open(dev)) {
      continue;
    }
    if (!dev->active_node) {
      continue;
    }
    enum CRAS_NC_PROVIDER want_provider =
        enabled ? dev->active_node->desired_nc_provider : CRAS_NC_PROVIDER_NONE;
    if (want_provider == dev->active_nc_provider) {
      continue;
    }
    syslog(LOG_INFO, "Re-open %s for noise cancellation: %d -> %d",
           dev->info.name, dev->active_nc_provider, want_provider);
    possibly_enable_fallback(CRAS_STREAM_INPUT, false);
    restart_device_group(dev->info.idx);
    possibly_disable_fallback(CRAS_STREAM_INPUT);
  }
}

/*
 * Removes |rstream| from the attached devices and immediately reconnect
 * it back. This is used to reconfigure the stream apm after events
 * like echo ref changes. Called in main thread.
 */
static int remove_then_reconnect_stream(struct cras_rstream* rstream) {
  struct enabled_dev* edev;
  struct cras_iodev* iodevs[NUM_OPEN_DEVS_MAX];
  unsigned int num_iodevs = 0;
  int rc;

  audio_thread_disconnect_stream(audio_thread, rstream, NULL);

  /* This is in main thread so we are confident the open devices
   * list doesn't change since we disconnect |rstream|.
   * Simply look up the pinned device or all devices in open state
   * and add |rstream| back to them.
   */
  if (rstream->is_pinned) {
    struct cras_iodev* pinned_dev = find_pinned_device(rstream);
    if (!pinned_dev) {
      syslog(LOG_WARNING, "Pinned dev %u not found at reconnect stream",
             rstream->pinned_dev_idx);
      return 0;
    }

    // Init the matched device(s) in the target group.
    size_t group_size;
    struct cras_iodev* const* group =
        cras_iodev_get_dev_group(pinned_dev, &group_size);
    if (!group) {
      group = &pinned_dev;
      group_size = 1;
    }

    for (size_t i = 0; i < group_size; i++) {
      if (!cras_iodev_should_attach_stream(group[i], rstream)) {
        continue;
      }

      /* Although we know |rstream| is pinned on group[i] it could still be in
       * close state due to prior IO errors. Always check and init this iodev
       * before reconnecting |rstream|.
       */
      rc = init_pinned_device(group[i], rstream);
      if (rc) {
        syslog(LOG_WARNING, "Failed to open pinned device at reconnect stream");
      } else {
        iodevs[num_iodevs++] = group[i];
      }
    }
  } else {
    DL_FOREACH (enabled_devs[rstream->direction], edev) {
      if (cras_iodev_is_open(edev->dev)) {
        iodevs[num_iodevs++] = edev->dev;
      }
    }
  }
  if (num_iodevs == 0) {
    return 0;
  }

  syslog(LOG_DEBUG, "Reconnect stream 0x%x%s from %d devices",
         rstream->stream_id, (rstream->is_pinned ? "(pinned)" : ""),
         num_iodevs);

  /* If |rstream| has an stream_apm, remove from those already attached
   * iodevs. This resets the old APM settings used on these iodevs and
   * allows new settings to apply when re-added to open iodevs later.
   */
  for (int i = 0; rstream->stream_apm && (i < num_iodevs); i++) {
    cras_stream_apm_remove(rstream->stream_apm, iodevs[i]);
  }

  return add_stream_to_open_devs(rstream, iodevs, num_iodevs);
}

int cras_iodev_list_set_aec_ref(unsigned int stream_id, unsigned int dev_idx) {
  struct cras_rstream* rstream = NULL;
  struct cras_iodev* echo_ref;
  int rc;

  if (dev_idx == NO_DEVICE) {
    echo_ref = NULL;
  } else {
    echo_ref = find_dev(dev_idx);
    if (echo_ref == NULL) {
      syslog(LOG_WARNING, "Invalid dev_idx %u to set aec ref", dev_idx);
      return 0;
    }
  }

  DL_FOREACH (stream_list_get(stream_list), rstream) {
    if (rstream->stream_id == stream_id) {
      break;
    }
  }
  if (rstream == NULL) {
    syslog(LOG_WARNING, "Stream 0x%.0x not found to set echo ref", stream_id);
    return 0;
  }

  /* Chrome client always call to set AEC ref even CRAS reports not
   * supporting system AEC, so this is a common case.
   */
  if (rstream->stream_apm == NULL) {
    return 0;
  }

  cras_server_metrics_set_aec_ref_device_type(echo_ref);

  rc = cras_stream_apm_set_aec_ref(rstream->stream_apm, echo_ref);
  if (rc) {
    syslog(LOG_WARNING, "Error setting dev %u as AEC ref", dev_idx);
  }

  /* Remove then reconnect so the stream apm can be reconfigured to
   * reflect the change in echo reference. For example, if the echo ref
   * no longer is in AEC use case then stream apm should use different
   * settings.
   */
  remove_then_reconnect_stream(rstream);

  return rc;
}

void cras_iodev_list_reconnect_streams_with_apm() {
  struct cras_rstream* rstream = NULL;
  DL_FOREACH (stream_list_get(stream_list), rstream) {
    if (rstream->stream_apm == NULL) {
      continue;
    }
    remove_then_reconnect_stream(rstream);
  }
}

void cras_iodev_list_reset() {
  struct enabled_dev* edev;
  struct cras_floop_pair* fpair;

  DL_FOREACH (enabled_devs[CRAS_STREAM_OUTPUT], edev) {
    DL_DELETE(enabled_devs[CRAS_STREAM_OUTPUT], edev);
    free(edev);
  }
  enabled_devs[CRAS_STREAM_OUTPUT] = NULL;
  DL_FOREACH (enabled_devs[CRAS_STREAM_INPUT], edev) {
    DL_DELETE(enabled_devs[CRAS_STREAM_INPUT], edev);
    free(edev);
  }
  DL_FOREACH (floop_pair_list, fpair) {
    DL_DELETE(floop_pair_list, fpair);
    cras_iodev_list_disable_floop_pair(fpair);
  }
  enabled_devs[CRAS_STREAM_INPUT] = NULL;
  devs[CRAS_STREAM_OUTPUT].iodevs = NULL;
  devs[CRAS_STREAM_INPUT].iodevs = NULL;
  devs[CRAS_STREAM_OUTPUT].size = 0;
  devs[CRAS_STREAM_INPUT].size = 0;
}

long convert_dBFS_from_input_node_gain(long gain, bool is_internal_mic) {
  long max_gain;
  max_gain = is_internal_mic ? cras_system_get_max_internal_mic_gain()
                             : DEFAULT_MAX_INPUT_NODE_GAIN;

  // Assert value in range 0 - 100.
  if (gain < 0) {
    gain = 0;
  }
  if (gain > 100) {
    gain = 100;
  }
  const long db_scale = (gain > 50) ? max_gain / 50 : 40;
  return (gain - 50) * db_scale;
}

long convert_input_node_gain_from_dBFS(long dBFS, bool is_internal_mic) {
  long max_gain;
  max_gain = is_internal_mic ? cras_system_get_max_internal_mic_gain()
                             : DEFAULT_MAX_INPUT_NODE_GAIN;
  return 50 + dBFS / ((dBFS > 0) ? max_gain / 50 : 40);
}

int cras_iodev_list_request_floop(const struct cras_floop_params* params) {
  if (!cras_feature_enabled(CrOSLateBootAudioFlexibleLoopback)) {
    return -ENOTSUP;
  }

  struct cras_floop_pair* fpair;
  int count = 0;
  DL_FOREACH (floop_pair_list, fpair) {
    if (cras_floop_pair_match_params(fpair, params)) {
      fpair->input.idle_timeout = (struct timespec){0, 0};
      return fpair->input.info.idx;
    }
    count++;
  }

  if (count >= NUM_FLOOP_PAIRS_MAX) {
    return -EAGAIN;
  }

  fpair = cras_floop_pair_create(params);
  if (!fpair) {
    return -ENOMEM;
  }

  DL_APPEND(floop_pair_list, fpair);

  return fpair->input.info.idx;
}

void cras_iodev_list_enable_floop_pair(struct cras_floop_pair* pair) {
  struct cras_rstream* stream;
  DL_FOREACH (stream_list_get(stream_list), stream) {
    if (cras_floop_pair_match_output_stream(pair, stream)) {
      int rc = init_device(&pair->output, stream);
      if (rc != 0) {
        continue;
      }
      struct cras_iodev* devs[] = {&pair->output};
      add_stream_to_open_devs(stream, devs, 1);
    }
  }
}

void cras_iodev_list_disable_floop_pair(struct cras_floop_pair* pair) {
  close_dev(&pair->output);
}

void cras_iodev_list_create_server_vad_stream(int dev_idx) {
  static struct cras_audio_format srv_stream_fmt = {
      SND_PCM_FORMAT_S16_LE,
      48000,
      2,
      {0, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
  int rc = server_stream_create(stream_list, SERVER_STREAM_VAD, dev_idx,
                                &srv_stream_fmt, APM_ECHO_CANCELLATION);
  if (rc) {
    syslog(LOG_ERR, "Fail to create VAD server stream");
  }
}

void cras_iodev_list_destroy_server_vad_stream(int dev_idx) {
  server_stream_destroy(stream_list, SERVER_STREAM_VAD, dev_idx);
}

bool cras_iodev_list_is_utility_stream(const struct cras_rstream* stream) {
  switch (stream->client_type) {
    case CRAS_CLIENT_TYPE_SERVER_STREAM:
      return true;
    default:;  // Do nothing.
  }

  switch (stream->stream_type) {
    case CRAS_STREAM_TYPE_SPEECH_RECOGNITION:
      return true;
    default:;  // Do nothing.
  }

  if (stream->is_pinned) {
    struct cras_iodev* dev = find_dev(stream->pinned_dev_idx);
    if (dev && dev->is_utility_device) {
      return true;
    }
  }

  return false;
}

int cras_iodev_list_fill_dsp_offload_infos(struct cras_dsp_offload_info* infos,
                                           uint32_t max_num_info) {
  if (!infos || !max_num_info) {
    return -EINVAL;
  }

  struct cras_iodev* dev;
  uint32_t num_info = 0;

  DL_FOREACH (devs[CRAS_STREAM_OUTPUT].iodevs, dev) {
    if (!dev->dsp_offload_map) {
      continue;
    }
    infos[num_info].iodev_idx = dev->info.idx;
    infos[num_info].dsp_pipe_id = dev->dsp_offload_map->pipeline_id;
    strlcpy(infos[num_info].dsp_pattern, dev->dsp_offload_map->dsp_pattern,
            sizeof(infos[num_info].dsp_pattern));
    infos[num_info].state = dev->dsp_offload_map->state;
    if (++num_info >= max_num_info) {
      break;
    }
  }
  return num_info;
}

int cras_iodev_list_get_dsp_offload_state(cras_node_id_t node_id) {
  struct cras_iodev* dev = NULL;

  dev = find_dev(dev_index_of(node_id));
  if (!dev || !dev->dsp_offload_map) {
    return DSP_PROC_UNSUPPORTED;
  }

  // -1: NOT STARTED, 0: PROC ON CRAS, 1: PROC ON DSP (offloaded)
  return (int)dev->dsp_offload_map->state;
}
