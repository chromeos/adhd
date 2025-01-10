/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Handles various system-level settings.
 *
 * Volume:  The system volume is represented as a value from 0 to 100.  This
 * number will be interpreted by the output device and applied to the hardware.
 * The value will be mapped to dB by the active device as it will know its curve
 * the best.
 */

#ifndef CRAS_SRC_SERVER_CRAS_SYSTEM_STATE_H_
#define CRAS_SRC_SERVER_CRAS_SYSTEM_STATE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cras/src/common/cras_alsa_card_info.h"
#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CRAS_MAX_SYSTEM_VOLUME 100
#define DEFAULT_CAPTURE_GAIN 2000  // 20dB of gain.
/* Default to -6 dBFS as 90% of CrOS boards use microphone with -26dBFS
 * sensitivity under 94dB SPL @ 1kHz and we generally added 20dB gain to it.
 * This is a temporary value that should be refined when the standard process
 * measuring intrinsic sensitivity is built. */
#define DEFAULT_CAPTURE_VOLUME_DBFS -600
// Default to 1--dB of range for playback and capture.
#define DEFAULT_MIN_VOLUME_DBFS -10000
#define DEFAULT_MAX_VOLUME_DBFS 0
#define DEFAULT_MIN_CAPTURE_GAIN -5000
#define DEFAULT_MAX_CAPTURE_GAIN 5000
// The default maximum input node gain that users can set by UI.
#define DEFAULT_MAX_INPUT_NODE_GAIN 2000

struct cras_tm;

/* Initialize system settings.
 *
 * Args:
 *    device_config_dir - Directory for device configs where volume curves live.
 *    shm_name - Name of the shared memory region used to store the state.
 *    rw_shm_fd - FD of the shm region.
 *    ro_shm_fd - FD of the shm region opened RO for sharing with clients.
 *    exp_state - Shared memory region for storing state.
 *    exp_state_size - Size of |exp_state|.
 */
void cras_system_state_init(const char* device_config_dir,
                            const char* shm_name,
                            int rw_shm_fd,
                            int ro_shm_fd,
                            struct cras_server_state* exp_state,
                            size_t exp_state_size);
void cras_system_state_deinit();

// Sets the suffix string to control which UCM config to load.
void cras_system_state_set_internal_ucm_suffix(const char* internal_ucm_suffix);

// Sets the system volume.  Will be applied by the active device.
void cras_system_set_volume(size_t volume);
// Gets the current system volume.
size_t cras_system_get_volume();

// Sets if the system is muted by the user.
void cras_system_set_user_mute(int muted);
// Sets if the system is muted for .
void cras_system_set_mute(int muted);
// Sets if the system muting is locked or not.
void cras_system_set_mute_locked(int locked);
// Gets the current mute state of the system.
int cras_system_get_mute();
// Gets the current user mute state.
int cras_system_get_user_mute();
// Gets the current system mute state.
int cras_system_get_system_mute();
// Gets if the system muting is locked or not.
int cras_system_get_mute_locked();

// Gets the suspend state of audio.
int cras_system_get_suspended();

/* Sets the suspend state of audio.
 * Args:
 *    suspend - True for suspend, false for resume.
 */
void cras_system_set_suspended(int suspend);

// Sets if the system capture path is muted or not.
void cras_system_set_capture_mute(int muted);
// Sets if the system capture path muting is locked or not.
void cras_system_set_capture_mute_locked(int locked);
// Gets the current mute state of the system capture path.
int cras_system_get_capture_mute();
// Gets if the system capture path muting is locked or not.
int cras_system_get_capture_mute_locked();

/* Sets the value in dB of the MAX and MIN volume settings.  This will allow
 * clients to query what range of control is available.  Both arguments are
 * specified as dB * 100.
 * Args:
 *     min - dB value when volume = 1 (0 mutes).
 *     max - dB value when volume = CRAS_MAX_SYSTEM_VOLUME
 */
void cras_system_set_volume_limits(long min, long max);
// Returns the dB value when volume = 1, in dB * 100.
long cras_system_get_min_volume();
// Returns the dB value when volume = CRAS_MAX_SYSTEM_VOLUME, in dB * 100.
long cras_system_get_max_volume();

// Returns the default value of output buffer size in frames.
int cras_system_get_default_output_buffer_size();

// Returns if system aec is supported.
int cras_system_get_aec_supported();

// Returns the system aec group id is available.
int cras_system_get_aec_group_id();

// Returns if system ns is supported.
int cras_system_get_ns_supported();

// Returns if system agc is supported.
int cras_system_get_agc_supported();

// Returns if system aec on dsp is supported
int cras_system_aec_on_dsp_supported();

// Returns if system ns on dsp is supported
int cras_system_ns_on_dsp_supported();

// Returns if system agc on dsp is supported
int cras_system_agc_on_dsp_supported();

// Sets the flag to enable or disable bluetooth wideband speech feature.
void cras_system_set_bt_wbs_enabled(bool enabled);

// Gets the elable flag of bluetooth wideband speech feature.
bool cras_system_get_bt_wbs_enabled();

// Note: bt_hfp_offload_finch_applied is leveraged for a blocking flag, see
// cras_system_state.c#line246

// Sets the flag to apply BT HFP offload enabling by Finch or not.
void cras_system_set_bt_hfp_offload_finch_applied(bool applied);

// Gets the flag of whether to determine BT HFP offload enabling by Finch.
bool cras_system_get_bt_hfp_offload_finch_applied();

// Sets the supported statement of BT HFP offload.
void cras_system_set_bt_hfp_offload_supported(bool supported);

// Returns if BT HFP offload is supported.
bool cras_system_get_bt_hfp_offload_supported();

/*
 * Returns if Bluetooth WBS mic should be deprioritized for selecting
 * as default audio input option.
 */
bool cras_system_get_deprioritize_bt_wbs_mic();

// Sets the flag to enable or disable Bluetooth fixed A2DP packet size.
void cras_system_set_bt_fix_a2dp_packet_size_enabled(bool enabled);

// Gets the flag of Bluetooth fixed A2DP packet size.
bool cras_system_get_bt_fix_a2dp_packet_size_enabled();

// Returns if Noise Cancellation is supported.
bool cras_system_get_noise_cancellation_supported();

// Returns if style transfer is supported.
bool cras_system_get_style_transfer_supported();

// Sets the flag to enable or disable ewma power report.
void cras_system_set_ewma_power_report_enabled(bool enabled);

// Returns if the current active nodes support sidetone.
bool cras_system_get_sidetone_supported();

/*
 * Sets the flag to enable or disable sidetone.
 * Returns true if success.
 */
bool cras_system_set_sidetone_enabled(bool enabled);

// Returns whether or not sidetone is enabled.
bool cras_system_get_sidetone_enabled();

// Sets the flag to bypass block/unblock Noise Cancellation mechanism.
void cras_system_set_bypass_block_noise_cancellation(bool bypass);

// Sets the force a2dp advanced codecs enable flag for testing purpose.
void cras_system_set_force_a2dp_advanced_codecs_enabled(bool enabled);

// Returns if a2dp advanced codecs should be force enabled.
bool cras_system_get_force_a2dp_advanced_codecs_enabled();

// Sets the force hfp swb enable flag for testing purpose.
void cras_system_set_force_hfp_swb_enabled(bool enabled);

// Returns if hfp swb should be force enabled.
bool cras_system_get_force_hfp_swb_enabled();

// Sets the sr bt enable flag to enable or disable.
void cras_system_set_sr_bt_enabled(bool enabled);

// Returns if sr_bt is enabled.
bool cras_system_get_sr_bt_enabled();

// Returns if sr_bt is supported or not. See feature_tier.rs.
bool cras_system_get_sr_bt_supported();

// Sets the force sr bt enable flag to enable or disable for testing purpose.
void cras_system_set_force_sr_bt_enabled(bool enabled);

// Returns if sr_bt should be force enabled or not.
bool cras_system_get_force_sr_bt_enabled();

// Checks if the card ignores the ucm suffix.
bool cras_system_check_ignore_ucm_suffix(const char* card_name);

// Returns true if hotword detection is paused at system suspend.
bool cras_system_get_hotword_pause_at_suspend();

// Sets whether to pause hotword detection at system suspend.
void cras_system_set_hotword_pause_at_suspend(bool pause);

// Returns if HW echo ref should be disabled.
bool cras_system_get_hw_echo_ref_disabled();

// Returns the maximum internal mic gain.
int cras_system_get_max_internal_mic_gain();

// Returns the maximum internal speaker channels.
int cras_system_get_max_internal_speaker_channels();

// Returns the maximum headphone channels.
int cras_system_get_max_headphone_channels();
// Returns the whether we should apply default volume curve on all usb audio
// device.
int cras_system_get_using_default_volume_curve_for_usb_audio_device();

// Returns the maximum headphone channels.
int cras_system_get_output_proc_hats();

// Set new rotation and update all observers
void cras_system_set_display_rotation(
    enum CRAS_SCREEN_ROTATION display_rotation);
enum CRAS_SCREEN_ROTATION cras_system_get_display_rotation();

/* Adds a card at the given index to the system.  When a new card is found
 * (through a udev event notification) this will add the card to the system,
 * causing its devices to become available for playback/capture.
 * Args:
 *    alsa_card_info - Info about the alsa card (Index, type, etc.).
 * Returns:
 *    0 on success, negative error on failure (Can't create or card already
 *    exists).
 */
int cras_system_add_alsa_card(struct cras_alsa_card_info* alsa_card_info);

/* Removes a card.  When a device is removed this will do the cleanup.  Device
 * at index must have been added using cras_system_add_alsa_card().
 * Args:
 *    alsa_card_index - Index ALSA uses to refer to the card.  The X in "hw:X".
 * Returns:
 *    0 on success, negative error on failure (Can't destroy or card doesn't
 *    exist).
 */
int cras_system_remove_alsa_card(size_t alsa_card_index);

/* Checks if an alsa card has been added to the system.
 * Args:
 *    alsa_card_index - Index ALSA uses to refer to the card.  The X in "hw:X".
 * Returns:
 *    1 if the card has already been added, 0 if not.
 */
int cras_system_alsa_card_exists(unsigned alsa_card_index);

/* Poll interface provides a way of getting a callback when 'select'
 * returns for a given file descriptor.
 */

/* Register the function to use to inform the select loop about new
 * file descriptors and callbacks.
 * Args:
 *    add - The function to call when a new fd is added.
 *    rm - The function to call when a new fd is removed.
 *    select_data - Additional value passed back to add and remove.
 * Returns:
 *    0 on success, or -EBUSY if there is already a registered handler.
 */
int cras_system_set_select_handler(int (*add)(int fd,
                                              void (*callback)(void* data,
                                                               int revents),
                                              void* callback_data,
                                              int events,
                                              void* select_data),
                                   void (*rm)(int fd, void* select_data),
                                   void* select_data);

/* Adds the fd and callback pair.  When select indicates that fd is readable,
 * the callback will be called.
 * Args:
 *    fd - The file descriptor to pass to select(2).
 *    callback - The callback to call when fd is ready.
 *    callback_data - Value passed back to the callback.
 *    events - The events to poll for.
 * Returns:
 *    0 on success or a negative error code on failure.
 */
int cras_system_add_select_fd(int fd,
                              void (*callback)(void* data, int revents),
                              void* callback_data,
                              int events);

/* Removes the fd from the list of fds that are passed to select.
 * Args:
 *    fd - The file descriptor to remove from the list.
 */
void cras_system_rm_select_fd(int fd);

/*
 * Register the function to use to add a task for main thread to execute.
 * Args:
 *    add_task - The function to call when new task is added.
 *    task_data - Additional data to pass back to add_task.
 * Returns:
 *    0 on success, or -EEXIST if there's already a registered handler.
 */
int cras_system_set_add_task_handler(int (*add_task)(void (*cb)(void* data),
                                                     void* callback_data,
                                                     void* task_data),
                                     void* task_data);

/*
 * Adds a task callback and data pair, to be executed in the next main thread
 * loop without additional wait time.
 * Args:
 *    callback - The function to execute.
 *    callback_data - The data to be passed to callback when executed.
 * Returns:
 *    0 on success, or -EINVAL if there's no handler for adding task.
 */
int cras_system_add_task(void (*callback)(void* data), void* callback_data);

/* Signals that an audio input or output stream has been added to the system.
 * This allows the count of active streams can be used to notice when the audio
 * subsystem is idle.
 * Args:
 *   direction - Directions of audio streams.
 *   client_type - CRAS_CLIENT_TYPE of the audio stream.
 *   effects - effects applied on the audio stream.
 */
void cras_system_state_stream_added(enum CRAS_STREAM_DIRECTION direction,
                                    enum CRAS_CLIENT_TYPE client_type,
                                    uint64_t effects);

/* Signals that an audio input or output stream has been removed from the
 * system.  This allows the count of active streams can be used to notice when
 * the audio subsystem is idle.
 * Args:
 *   direction - Directions of audio stream.
 *   client_type - CRAS_CLIENT_TYPE of the audio stream.
 *   effects - effects applied on the audio stream.
 */
void cras_system_state_stream_removed(enum CRAS_STREAM_DIRECTION direction,
                                      enum CRAS_CLIENT_TYPE client_type,
                                      uint64_t effects);

// Returns the number of active playback and capture streams.
unsigned cras_system_state_get_active_streams();

/* Returns the number of active streams with given direction.
 * Args:
 *   direction - Directions of audio stream.
 */
unsigned cras_system_state_get_active_streams_by_direction(
    enum CRAS_STREAM_DIRECTION direction);

/* Returns the number of input streams with permission per CRAS_CLIENT_TYPE.
 *
 * Returns:
 *   num_input_streams - An array with length = CRAS_NUM_CLIENT_TYPE and each
 *                        element is the number of the current input
 *                        streams with permission in each client type.
 */
void cras_system_state_get_input_streams_with_permission(
    uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE]);

/* Fills ts with the time the last stream was removed from the system, the time
 * the stream count went to zero.
 */
void cras_system_state_get_last_stream_active_time(struct cras_timespec* ts);

/* Returns output devices information.
 * Args:
 *    devs - returns the array of output devices information.
 * Returns:
 *    number of output devices.
 */
int cras_system_state_get_output_devs(const struct cras_iodev_info** devs);

/* Returns input devices information.
 * Args:
 *    devs - returns the array of input devices information.
 * Returns:
 *    number of input devices.
 */
int cras_system_state_get_input_devs(const struct cras_iodev_info** devs);

/* Returns output nodes information.
 * Args:
 *    nodes - returns the array of output nodes information.
 * Returns:
 *    number of output nodes.
 */
int cras_system_state_get_output_nodes(const struct cras_ionode_info** nodes);

/* Returns input nodes information.
 * Args:
 *    nodes - returns the array of input nodes information.
 * Returns:
 *    number of input nodes.
 */
int cras_system_state_get_input_nodes(const struct cras_ionode_info** nodes);

// Return the active input and output nodes' types as a string.
const char* cras_system_state_get_active_node_types();

/*
 * Sets the non-empty audio status.
 */
void cras_system_state_set_non_empty_status(int non_empty);

/*
 * Returns the non-empty audio status.
 */
int cras_system_state_get_non_empty_status();

/*
 * Returns the active output node.
 * There could be no active output node.
 */
void get_active_output_node(struct cras_ionode_info* node);

/*
 * Returns the active input node.
 * There could be no active input node.
 */
void get_active_input_node(struct cras_ionode_info* node);

/* Returns a pointer to the current system state that is shared with clients.
 * This also 'locks' the structure by incrementing the update count to an odd
 * value.
 */
struct cras_server_state* cras_system_state_update_begin();

/* Unlocks the system state structure that was updated after calling
 * cras_system_state_update_begin by again incrementing the update count.
 */
void cras_system_state_update_complete();

/* Gets a pointer to the system state without locking it.  Only used for debug
 * log.  Don't add calls to this function. */
struct cras_server_state* cras_system_state_get_no_lock();

// Returns the shm fd for the server_state structure.
key_t cras_sys_state_shm_fd();

// Returns the timer manager.
struct cras_tm* cras_system_state_get_tm();

/*
 * Add snapshot to snapshot buffer in system state
 */
void cras_system_state_add_snapshot(struct cras_audio_thread_snapshot*);

/*
 * Dump snapshots from system state to shared memory with client
 */
void cras_system_state_dump_snapshots();

/*
 * Returns true if in the main thread.
 */
int cras_system_state_in_main_thread();

/*
 * Returns true if any internal audio cards that handle speaker, dmic or
 * headsets are detected.
 */
bool cras_system_state_internal_cards_detected();

// Enable/disable speak on mute detection
void cras_system_state_set_speak_on_mute_detection(bool enabled);

// Get whether speak on mute detection is enabled
bool cras_system_state_get_speak_on_mute_detection_enabled();

// Get number of non-Chrome output streams
int cras_system_state_num_non_chrome_output_streams();

// Sets the flag to enable or disable Force Respect UI Gains.
void cras_system_set_force_respect_ui_gains_enabled(bool enabled);

// Gets the flag of Force Respect UI Gains.
bool cras_system_get_force_respect_ui_gains_enabled();

// Gets the number of stream ignoring UI gains.
int cras_system_get_num_stream_ignore_ui_gains();

// Returns the latency offset that should be added for speaker output.
int cras_system_get_speaker_output_latency_offset_ms();

// Returns true AP NC is supported on bluetooth devices.
// This does not consider feature flags.
bool cras_system_get_ap_nc_supported_on_bluetooth();

// Gets DSP offload map string obtained from board config.
const char* cras_system_get_dsp_offload_map_str();

// Get number of ARC streams
int cras_system_state_num_arc_streams();

// Get the ChromeOS board name.
const char* cras_system_get_board_name();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_SYSTEM_STATE_H_
