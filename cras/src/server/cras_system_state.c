/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_system_state.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <syslog.h>

#include "cras/src/server/config/cras_board_config.h"
#include "cras/src/server/config/cras_device_blocklist.h"
#include "cras/src/server/cras_alert.h"
#include "cras/src/server/cras_alsa_card.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_main_thread_log.h"
#include "cras/src/server/cras_observer.h"
#include "cras/src/server/cras_speak_on_mute_detector.h"
#include "cras/src/server/cras_tm.h"
#include "cras/src/server/rust/include/cras_feature_tier.h"
#include "cras_config.h"
#include "cras_shm.h"
#include "cras_types.h"
#include "cras_util.h"
#include "third_party/utlist/utlist.h"

struct card_list {
  struct cras_alsa_card* card;
  struct card_list *prev, *next;
};

struct name_list {
  char name[NAME_MAX];
  struct name_list *prev, *next;
};

// A struct holding the states of the features.
struct feature_state {
  // Whether the feature is force enabled. It's for
  // testing purpose.
  bool force_sr_bt_enabled;
};

/* The system state. */
struct private_state {
  // The exported system state shared with clients.
  struct cras_server_state* exp_state;
  // Name of posix shm region for exported state.
  char shm_name[NAME_MAX];
  // fd for shm area of system_state struct.
  int shm_fd;
  // fd for shm area of system_state struct, opened read-only.
  // This copy is to dup and pass to clients.
  int shm_fd_ro;
  // Size of the shm area.
  size_t shm_size;
  // Directory of device configs where volume curves live.
  const char* device_config_dir;
  // The suffix to append to internal card name to
  // control which ucm config file to load.
  const char* internal_ucm_suffix;
  struct name_list* ignore_suffix_cards;
  // Blocklist of device the server will ignore.
  struct cras_device_blocklist* device_blocklist;
  // A list of active sound cards in the system.
  struct card_list* cards;
  // Protects the update_count, as audio threads can update the
  // stream count.
  pthread_mutex_t update_lock;
  // The system-wide timer manager.
  struct cras_tm* tm;
  // Select loop callback registration.
  int (*fd_add)(int fd,
                void (*cb)(void* data, int events),
                void* cb_data,
                int events,
                void* select_data);
  void (*fd_rm)(int fd, void* select_data);
  void* select_data;
  // Function to handle adding a task for main thread to execute.
  int (*add_task)(void (*callback)(void* data),
                  void* callback_data,
                  void* task_data);
  // Data to be passed to add_task handler function.
  void* task_data;
  struct cras_audio_thread_snapshot_buffer snapshot_buffer;
  // The thread id of the main thread.
  pthread_t main_thread_tid;
  // The flag to override A2DP packet size set by
  // Blueetoh peer devices to a smaller default value.
  bool bt_fix_a2dp_packet_size;
  // The feature tier. See cras_feature_tier.
  struct cras_feature_tier feature_tier;
  // The feature state. See struct feature_state.
  struct feature_state feature_state;
  // Whether speak on mute detection is
  // enabled.
  bool speak_on_mute_detection_enabled;
};

static struct private_state state;

// The string format is CARD1,CARD2,CARD3. Divide it into a list.
void init_ignore_suffix_cards(char* str) {
  struct name_list* card;
  char* ptr;

  state.ignore_suffix_cards = NULL;

  if (str == NULL) {
    return;
  }

  ptr = strtok(str, ",");
  while (ptr != NULL) {
    card = (struct name_list*)calloc(1, sizeof(*card));
    if (!card) {
      syslog(LOG_ERR, "Failed to call calloc: %d", errno);
      return;
    }
    strncpy(card->name, ptr, NAME_MAX - 1);
    DL_APPEND(state.ignore_suffix_cards, card);
    ptr = strtok(NULL, ",");
  }
}

void deinit_ignore_suffix_cards() {
  struct name_list* card;
  DL_FOREACH (state.ignore_suffix_cards, card) {
    DL_DELETE(state.ignore_suffix_cards, card);
    free(card);
  }
}

/*
 * Exported Interface.
 */

void cras_system_state_init(const char* device_config_dir,
                            const char* shm_name,
                            int rw_shm_fd,
                            int ro_shm_fd,
                            struct cras_server_state* exp_state,
                            size_t exp_state_size,
                            const char* board_name,
                            const char* cpu_model_name) {
  struct cras_board_config board_config;
  int rc;

  assert(sizeof(*exp_state) == exp_state_size);
  state.shm_size = sizeof(*exp_state);

  strncpy(state.shm_name, shm_name, sizeof(state.shm_name));
  state.shm_name[sizeof(state.shm_name) - 1] = '\0';
  state.shm_fd = rw_shm_fd;
  state.shm_fd_ro = ro_shm_fd;

  // Read board config.
  memset(&board_config, 0, sizeof(board_config));
  cras_board_config_get(device_config_dir, &board_config);

  // Initial system state.
  exp_state->state_version = CRAS_SERVER_STATE_VERSION;
  exp_state->volume = CRAS_MAX_SYSTEM_VOLUME;
  exp_state->mute = 0;
  exp_state->mute_locked = 0;
  exp_state->suspended = 0;
  exp_state->capture_mute = 0;
  exp_state->capture_mute_locked = 0;
  exp_state->min_volume_dBFS = DEFAULT_MIN_VOLUME_DBFS;
  exp_state->max_volume_dBFS = DEFAULT_MAX_VOLUME_DBFS;
  exp_state->num_streams_attached = 0;
  exp_state->default_output_buffer_size =
      board_config.default_output_buffer_size;
  exp_state->aec_supported = board_config.aec_supported;
  exp_state->aec_group_id = board_config.aec_group_id;
  exp_state->ns_supported = board_config.ns_supported;
  exp_state->agc_supported = board_config.agc_supported;
  exp_state->aec_on_dsp_supported = board_config.aec_on_dsp_supported;
  exp_state->ns_on_dsp_supported = board_config.ns_on_dsp_supported;
  exp_state->agc_on_dsp_supported = board_config.agc_on_dsp_supported;
  exp_state->bt_wbs_enabled = board_config.bt_wbs_enabled;
  exp_state->bt_hfp_offload_finch_applied =
      board_config.bt_hfp_offload_finch_applied;
  exp_state->deprioritize_bt_wbs_mic = board_config.deprioritize_bt_wbs_mic;
  // Disable Noise Cancellation as default.
  exp_state->noise_cancellation_enabled = 0;
  exp_state->dsp_noise_cancellation_supported = board_config.nc_supported;
  exp_state->bypass_block_noise_cancellation = 0;
  exp_state->hotword_pause_at_suspend = board_config.hotword_pause_at_suspend;
  exp_state->hw_echo_ref_disabled = board_config.hw_echo_ref_disabled;
  exp_state->max_internal_mic_gain = board_config.max_internal_mic_gain;
  exp_state->max_internal_speaker_channels =
      board_config.max_internal_speaker_channels;
  exp_state->max_headphone_channels = board_config.max_headphone_channels;
  exp_state->num_non_chrome_output_streams = 0;
  exp_state->nc_standalone_mode = board_config.nc_standalone_mode;

  if ((rc = pthread_mutex_init(&state.update_lock, 0) != 0)) {
    syslog(LOG_ERR, "Fatal: system state mutex init");
    exit(rc);
  }

  state.exp_state = exp_state;

  /* Directory for volume curve configs.
   * Note that device_config_dir does not affect device blocklist.
   * Device blocklist is common to all boards so we do not need
   * to change device blocklist at run time. */
  state.device_config_dir = device_config_dir;
  state.internal_ucm_suffix = NULL;
  init_ignore_suffix_cards(board_config.ucm_ignore_suffix);
  free(board_config.ucm_ignore_suffix);

  state.tm = cras_tm_init();
  if (!state.tm) {
    syslog(LOG_ERR, "Fatal: system state timer init");
    exit(-ENOMEM);
  }

  // Read config file for blocklisted devices.
  state.device_blocklist = cras_device_blocklist_create(CRAS_CONFIG_FILE_DIR);

  // Initialize snapshot buffer memory
  memset(&state.snapshot_buffer, 0,
         sizeof(struct cras_audio_thread_snapshot_buffer));

  // Save thread id of the main thread.
  state.main_thread_tid = pthread_self();

  state.bt_fix_a2dp_packet_size = false;

  cras_feature_tier_init(&state.feature_tier, board_name, cpu_model_name);
}

void cras_system_state_deinit() {
  // Free any resources used.  This prevents unit tests from leaking.

  cras_device_blocklist_destroy(state.device_blocklist);

  cras_tm_deinit(state.tm);

  if (state.exp_state) {
    munmap(state.exp_state, state.shm_size);
    cras_shm_close_unlink(state.shm_name, state.shm_fd);
    if (state.shm_fd_ro != state.shm_fd) {
      close(state.shm_fd_ro);
    }
  }

  deinit_ignore_suffix_cards();
  pthread_mutex_destroy(&state.update_lock);
}

void cras_system_state_set_internal_ucm_suffix(
    const char* internal_ucm_suffix) {
  state.internal_ucm_suffix = internal_ucm_suffix;
}

void cras_system_set_volume(size_t volume) {
  if (volume > CRAS_MAX_SYSTEM_VOLUME) {
    syslog(LOG_DEBUG, "system volume set out of range %zu", volume);
  }

  state.exp_state->volume = MIN(volume, CRAS_MAX_SYSTEM_VOLUME);
  cras_observer_notify_output_volume(state.exp_state->volume);
}

size_t cras_system_get_volume() {
  return state.exp_state->volume;
}

void cras_system_notify_mute(void) {
  cras_observer_notify_output_mute(state.exp_state->mute,
                                   state.exp_state->user_mute,
                                   state.exp_state->mute_locked);
}

void cras_system_set_user_mute(int mute) {
  int current_mute = cras_system_get_mute();

  if (state.exp_state->user_mute == !!mute) {
    return;
  }

  state.exp_state->user_mute = !!mute;

  if (current_mute == (mute || state.exp_state->mute)) {
    return;
  }

  cras_system_notify_mute();
}

void cras_system_set_mute(int mute) {
  int current_mute = cras_system_get_mute();

  if (state.exp_state->mute_locked) {
    return;
  }

  if (state.exp_state->mute == !!mute) {
    return;
  }

  state.exp_state->mute = !!mute;

  if (current_mute == (mute || state.exp_state->user_mute)) {
    return;
  }

  cras_system_notify_mute();
}

void cras_system_set_mute_locked(int locked) {
  if (state.exp_state->mute_locked == !!locked) {
    return;
  }

  state.exp_state->mute_locked = !!locked;
}

int cras_system_get_mute() {
  return state.exp_state->mute || state.exp_state->user_mute;
}

int cras_system_get_user_mute() {
  return state.exp_state->user_mute;
}

int cras_system_get_system_mute() {
  return state.exp_state->mute;
}

int cras_system_get_mute_locked() {
  return state.exp_state->mute_locked;
}

void cras_system_notify_capture_mute(void) {
  cras_observer_notify_capture_mute(state.exp_state->capture_mute,
                                    state.exp_state->capture_mute_locked);
}

void cras_system_set_capture_mute(int mute) {
  if (state.exp_state->capture_mute_locked) {
    return;
  }

  state.exp_state->capture_mute = !!mute;
  cras_system_notify_capture_mute();
}

void cras_system_set_capture_mute_locked(int locked) {
  state.exp_state->capture_mute_locked = !!locked;
  cras_system_notify_capture_mute();
}

int cras_system_get_capture_mute() {
  return state.exp_state->capture_mute;
}

int cras_system_get_capture_mute_locked() {
  return state.exp_state->capture_mute_locked;
}

int cras_system_get_suspended() {
  return state.exp_state->suspended;
}

void cras_system_set_suspended(int suspended) {
  state.exp_state->suspended = suspended;
  cras_observer_notify_suspend_changed(suspended);
  cras_alert_process_all_pending_alerts();
}

void cras_system_set_volume_limits(long min, long max) {
  state.exp_state->min_volume_dBFS = min;
  state.exp_state->max_volume_dBFS = max;
}

long cras_system_get_min_volume() {
  return state.exp_state->min_volume_dBFS;
}

long cras_system_get_max_volume() {
  return state.exp_state->max_volume_dBFS;
}

int cras_system_get_default_output_buffer_size() {
  return state.exp_state->default_output_buffer_size;
}

int cras_system_get_aec_supported() {
  return state.exp_state->aec_supported;
}

int cras_system_get_aec_group_id() {
  return state.exp_state->aec_group_id;
}

int cras_system_get_ns_supported() {
  return state.exp_state->ns_supported;
}

int cras_system_get_agc_supported() {
  return state.exp_state->agc_supported;
}

int cras_system_aec_on_dsp_supported() {
  return state.exp_state->aec_on_dsp_supported;
}

int cras_system_ns_on_dsp_supported() {
  return state.exp_state->ns_on_dsp_supported;
}

int cras_system_agc_on_dsp_supported() {
  return state.exp_state->agc_on_dsp_supported;
}

void cras_system_set_bt_wbs_enabled(bool enabled) {
  state.exp_state->bt_wbs_enabled = enabled;
}

bool cras_system_get_bt_wbs_enabled() {
  return !!state.exp_state->bt_wbs_enabled;
}

bool cras_system_get_bt_hfp_offload_finch_applied() {
  return !!state.exp_state->bt_hfp_offload_finch_applied;
}

bool cras_system_get_deprioritize_bt_wbs_mic() {
  return !!state.exp_state->deprioritize_bt_wbs_mic;
}

void cras_system_set_bt_fix_a2dp_packet_size_enabled(bool enabled) {
  state.bt_fix_a2dp_packet_size = enabled;
}

bool cras_system_get_bt_fix_a2dp_packet_size_enabled() {
  return state.bt_fix_a2dp_packet_size;
}

void cras_system_set_noise_cancellation_enabled(bool enabled) {
  // When the flag is toggled, propagate to all iodevs immediately.
  if (cras_system_get_noise_cancellation_enabled() != enabled) {
    MAINLOG(main_log, MAIN_THREAD_NOISE_CANCELLATION, enabled, 0, 0);
    syslog(LOG_DEBUG, "Noise Cancellation is %s",
           enabled ? "enabled" : "disabled");
    state.exp_state->noise_cancellation_enabled = enabled;
    cras_iodev_list_reset_for_noise_cancellation();
  }
}

bool cras_system_get_noise_cancellation_enabled() {
  return !!state.exp_state->noise_cancellation_enabled;
}

bool cras_system_get_noise_cancellation_supported() {
  return cras_system_get_dsp_noise_cancellation_supported() ||
         cras_system_get_ap_noise_cancellation_supported();
}

bool cras_system_get_dsp_noise_cancellation_supported() {
  return !!state.exp_state->dsp_noise_cancellation_supported;
}

bool cras_system_get_ap_noise_cancellation_supported() {
  // TODO(b/271383461): Ask cras_feature_tier.
  return true;
}

void cras_system_set_bypass_block_noise_cancellation(bool bypass) {
  syslog(LOG_DEBUG, "Set bypass_block_noise_cancellation to %s",
         bypass ? "true" : "false");
  state.exp_state->bypass_block_noise_cancellation = bypass;

  // Update nodes info immediately to adopt bypass status.
  cras_iodev_list_update_device_list();
  cras_iodev_list_notify_nodes_changed();
}

bool cras_system_get_bypass_block_noise_cancellation() {
  return !!state.exp_state->bypass_block_noise_cancellation;
}

bool cras_system_get_sr_bt_supported() {
  return state.feature_tier.sr_bt_supported;
}

void cras_system_set_force_sr_bt_enabled(bool enabled) {
  state.feature_state.force_sr_bt_enabled = enabled;
}

bool cras_system_get_force_sr_bt_enabled() {
  return state.feature_state.force_sr_bt_enabled;
}

bool cras_system_check_ignore_ucm_suffix(const char* card_name) {
  /* Check the general case:
   *   ALSA Loopback card "Loopback"
   *   ALSA Dummy card "Dummy"
   */
  if (!strcmp("Loopback", card_name) || !strcmp("Dummy", card_name)) {
    return true;
  }

  // Check board-specific ignore ucm suffix cards.
  struct name_list* card;
  DL_FOREACH (state.ignore_suffix_cards, card) {
    if (!strcmp(card->name, card_name)) {
      return true;
    }
  }
  return false;
}

bool cras_system_get_hotword_pause_at_suspend() {
  return !!state.exp_state->hotword_pause_at_suspend;
}

void cras_system_set_hotword_pause_at_suspend(bool pause) {
  state.exp_state->hotword_pause_at_suspend = pause;
}

bool cras_system_get_hw_echo_ref_disabled() {
  return state.exp_state->hw_echo_ref_disabled;
}

int cras_system_get_max_internal_mic_gain() {
  return state.exp_state->max_internal_mic_gain;
}

int cras_system_get_max_internal_speaker_channels() {
  return state.exp_state->max_internal_speaker_channels;
}

int cras_system_get_max_headphone_channels() {
  return state.exp_state->max_headphone_channels;
}

int cras_system_add_alsa_card(struct cras_alsa_card_info* alsa_card_info) {
  struct card_list* card;
  struct cras_alsa_card* alsa_card;
  unsigned card_index;

  if (alsa_card_info == NULL) {
    return -EINVAL;
  }

  card_index = alsa_card_info->card_index;

  DL_FOREACH (state.cards, card) {
    if (card_index == cras_alsa_card_get_index(card->card)) {
      return -EEXIST;
    }
  }
  alsa_card =
      cras_alsa_card_create(alsa_card_info, state.device_config_dir,
                            state.device_blocklist, state.internal_ucm_suffix);
  if (alsa_card == NULL) {
    return -ENOMEM;
  }
  card = calloc(1, sizeof(*card));
  if (card == NULL) {
    return -ENOMEM;
  }
  card->card = alsa_card;
  DL_APPEND(state.cards, card);
  return 0;
}

int cras_system_remove_alsa_card(size_t alsa_card_index) {
  struct card_list* card;

  DL_FOREACH (state.cards, card) {
    if (alsa_card_index == cras_alsa_card_get_index(card->card)) {
      break;
    }
  }
  if (card == NULL) {
    return -EINVAL;
  }
  DL_DELETE(state.cards, card);
  cras_alsa_card_destroy(card->card);
  free(card);
  return 0;
}

int cras_system_alsa_card_exists(unsigned alsa_card_index) {
  struct card_list* card;

  DL_FOREACH (state.cards, card) {
    if (alsa_card_index == cras_alsa_card_get_index(card->card)) {
      return 1;
    }
  }
  return 0;
}

int cras_system_set_select_handler(int (*add)(int fd,
                                              void (*callback)(void* data,
                                                               int events),
                                              void* callback_data,
                                              int events,
                                              void* select_data),
                                   void (*rm)(int fd, void* select_data),
                                   void* select_data) {
  if (state.fd_add != NULL || state.fd_rm != NULL) {
    return -EEXIST;
  }
  state.fd_add = add;
  state.fd_rm = rm;
  state.select_data = select_data;
  return 0;
}

int cras_system_add_select_fd(int fd,
                              void (*callback)(void* data, int revents),
                              void* callback_data,
                              int events) {
  if (state.fd_add == NULL) {
    return -EINVAL;
  }
  return state.fd_add(fd, callback, callback_data, events, state.select_data);
}

int cras_system_set_add_task_handler(int (*add_task)(void (*cb)(void* data),
                                                     void* callback_data,
                                                     void* task_data),
                                     void* task_data) {
  if (state.add_task != NULL) {
    return -EEXIST;
  }

  state.add_task = add_task;
  state.task_data = task_data;
  return 0;
}

int cras_system_add_task(void (*callback)(void* data), void* callback_data) {
  if (state.add_task == NULL) {
    return -EINVAL;
  }

  return state.add_task(callback, callback_data, state.task_data);
}

void cras_system_rm_select_fd(int fd) {
  if (state.fd_rm != NULL) {
    state.fd_rm(fd, state.select_data);
  }
}

void cras_system_state_stream_added(enum CRAS_STREAM_DIRECTION direction,
                                    enum CRAS_CLIENT_TYPE client_type) {
  struct cras_server_state* s;

  s = cras_system_state_update_begin();
  if (!s) {
    return;
  }

  s->num_active_streams[direction]++;
  s->num_streams_attached++;
  if (direction == CRAS_STREAM_INPUT) {
    s->num_input_streams_with_permission[client_type]++;
    cras_observer_notify_input_streams_with_permission(
        s->num_input_streams_with_permission);
  }

  if (direction == CRAS_STREAM_OUTPUT &&
      client_type != CRAS_CLIENT_TYPE_CHROME &&
      client_type != CRAS_CLIENT_TYPE_LACROS) {
    s->num_non_chrome_output_streams++;
    cras_observer_notify_num_non_chrome_output_streams(
        s->num_non_chrome_output_streams);
  }

  cras_system_state_update_complete();
  cras_observer_notify_num_active_streams(direction,
                                          s->num_active_streams[direction]);
}

void cras_system_state_stream_removed(enum CRAS_STREAM_DIRECTION direction,
                                      enum CRAS_CLIENT_TYPE client_type) {
  struct cras_server_state* s;
  unsigned i, sum;

  s = cras_system_state_update_begin();
  if (!s) {
    return;
  }

  sum = 0;
  for (i = 0; i < CRAS_NUM_DIRECTIONS; i++) {
    sum += s->num_active_streams[i];
  }

  // Set the last active time when removing the final stream.
  if (sum == 1) {
    cras_clock_gettime(CLOCK_MONOTONIC_RAW, &s->last_active_stream_time);
  }
  s->num_active_streams[direction]--;
  if (direction == CRAS_STREAM_INPUT) {
    s->num_input_streams_with_permission[client_type]--;
    cras_observer_notify_input_streams_with_permission(
        s->num_input_streams_with_permission);
  }

  if (direction == CRAS_STREAM_OUTPUT &&
      client_type != CRAS_CLIENT_TYPE_CHROME &&
      client_type != CRAS_CLIENT_TYPE_LACROS) {
    s->num_non_chrome_output_streams--;
    cras_observer_notify_num_non_chrome_output_streams(
        s->num_non_chrome_output_streams);
  }

  cras_system_state_update_complete();
  cras_observer_notify_num_active_streams(direction,
                                          s->num_active_streams[direction]);
}

unsigned cras_system_state_get_active_streams() {
  unsigned i, sum;
  sum = 0;
  for (i = 0; i < CRAS_NUM_DIRECTIONS; i++) {
    sum += state.exp_state->num_active_streams[i];
  }
  return sum;
}

unsigned cras_system_state_get_active_streams_by_direction(
    enum CRAS_STREAM_DIRECTION direction) {
  return state.exp_state->num_active_streams[direction];
}

void cras_system_state_get_input_streams_with_permission(
    uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE]) {
  unsigned type;
  for (type = 0; type < CRAS_NUM_CLIENT_TYPE; ++type) {
    num_input_streams[type] =
        state.exp_state->num_input_streams_with_permission[type];
  }
}

void cras_system_state_get_last_stream_active_time(struct cras_timespec* ts) {
  *ts = state.exp_state->last_active_stream_time;
}

int cras_system_state_get_output_devs(const struct cras_iodev_info** devs) {
  *devs = state.exp_state->output_devs;
  return state.exp_state->num_output_devs;
}

int cras_system_state_get_input_devs(const struct cras_iodev_info** devs) {
  *devs = state.exp_state->input_devs;
  return state.exp_state->num_input_devs;
}

int cras_system_state_get_output_nodes(const struct cras_ionode_info** nodes) {
  *nodes = state.exp_state->output_nodes;
  return state.exp_state->num_output_nodes;
}

int cras_system_state_get_input_nodes(const struct cras_ionode_info** nodes) {
  *nodes = state.exp_state->input_nodes;
  return state.exp_state->num_input_nodes;
}

const char* cras_system_state_get_active_node_types() {
  char *input_type = "NONE", *output_type = "NONE";
  int i;

  for (i = 0; i < state.exp_state->num_input_nodes; i++) {
    if (state.exp_state->input_nodes[i].active) {
      input_type = state.exp_state->input_nodes[i].type;
    }
  }
  for (i = 0; i < state.exp_state->num_output_nodes; i++) {
    if (state.exp_state->output_nodes[i].active) {
      output_type = state.exp_state->output_nodes[i].type;
    }
  }

  snprintf(state.exp_state->active_node_type_pair,
           sizeof(state.exp_state->active_node_type_pair), "%s_%s", input_type,
           output_type);

  return state.exp_state->active_node_type_pair;
}

void cras_system_state_set_non_empty_status(int non_empty) {
  state.exp_state->non_empty_status = non_empty;
}

int cras_system_state_get_non_empty_status() {
  return state.exp_state->non_empty_status;
}

struct cras_server_state* cras_system_state_update_begin() {
  if (pthread_mutex_lock(&state.update_lock)) {
    syslog(LOG_ERR, "Failed to lock stream mutex");
    return NULL;
  }

  __sync_fetch_and_add(&state.exp_state->update_count, 1);
  return state.exp_state;
}

void cras_system_state_update_complete() {
  __sync_fetch_and_add(&state.exp_state->update_count, 1);
  pthread_mutex_unlock(&state.update_lock);
}

struct cras_server_state* cras_system_state_get_no_lock() {
  return state.exp_state;
}

key_t cras_sys_state_shm_fd() {
  return state.shm_fd_ro;
}

struct cras_tm* cras_system_state_get_tm() {
  return state.tm;
}

void cras_system_state_dump_snapshots() {
  memcpy(&state.exp_state->snapshot_buffer, &state.snapshot_buffer,
         sizeof(struct cras_audio_thread_snapshot_buffer));
}

void cras_system_state_add_snapshot(
    struct cras_audio_thread_snapshot* snapshot) {
  state.snapshot_buffer.snapshots[state.snapshot_buffer.pos++] = (*snapshot);
  state.snapshot_buffer.pos %= CRAS_MAX_AUDIO_THREAD_SNAPSHOTS;
}

int cras_system_state_in_main_thread() {
  return pthread_self() == state.main_thread_tid;
}

bool cras_system_state_internal_cards_detected() {
  struct card_list* card;

  DL_FOREACH (state.cards, card) {
    if (cras_alsa_card_get_type(card->card) == ALSA_CARD_TYPE_INTERNAL) {
      return true;
    }
  }
  return false;
}

void cras_system_state_set_speak_on_mute_detection(bool enabled) {
  state.speak_on_mute_detection_enabled = enabled;
  cras_speak_on_mute_detector_enable(enabled);
}

bool cras_system_state_get_speak_on_mute_detection_enabled() {
  return state.speak_on_mute_detection_enabled;
}

int cras_system_state_num_non_chrome_output_streams() {
  return state.exp_state->num_non_chrome_output_streams;
}

bool cras_system_get_noise_cancellation_standalone_mode() {
  return !!state.exp_state->nc_standalone_mode;
}

void cras_system_set_force_respect_ui_gains_enabled(bool enabled) {
  if (cras_system_get_force_respect_ui_gains_enabled() != enabled) {
    MAINLOG(main_log, MAIN_THREAD_FORCE_RESPECT_UI_GAINS, enabled, 0, 0);
    state.exp_state->force_respect_ui_gains = enabled;
  }
}

bool cras_system_get_force_respect_ui_gains_enabled() {
  return !!state.exp_state->force_respect_ui_gains;
}
