/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_ALSA_UCM_H_
#define CRAS_SRC_SERVER_CRAS_ALSA_UCM_H_

#include <alsa/asoundlib.h>
#include <limits.h>

#include "cras/src/common/cras_types_internal.h"
#include "cras/src/server/cras_alsa_mixer_name.h"
#include "cras/src/server/cras_alsa_ucm_section.h"
#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JACK_SWITCH_AUTO_DETECT -1

struct cras_use_case_mgr;

// A bit field of the CRAS_USE_CASE flags representing a set of use cases.
typedef uint32_t cras_use_cases_t;
static_assert(CRAS_NUM_USE_CASES <= sizeof(cras_use_cases_t) * CHAR_BIT,
              "cras_use_cases_t bit field too small");

/* Helpers to access UCM configuration for a card if any is provided.
 * This configuration can specify how to enable or disable certain inputs and
 * outputs on the card.
 */

/* Checks if there is a matching ucm configuration for the given name.
 *
 * Args:
 *    name - Name of the card to match against the UCM card list.
 * Returns:
 *    1 if it exists, 0 otherwise.
 */
int ucm_conf_exists(const char* name);

/* Creates a cras_use_case_mgr instance for the given card name if there is a
 * matching ucm configuration.  It there is a matching UCM config, then it will
 * be configured to the default state.
 *
 * Args:
 *    name - Name of the card to match against the UCM card list.
 * Returns:
 *    A pointer to the use case manager if found, otherwise NULL.  The pointer
 *    must later be freed with ucm_destroy().
 */
struct cras_use_case_mgr* ucm_create(const char* name);

/* Destroys a cras_use_case_mgr that was returned from ucm_create.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 */
void ucm_destroy(struct cras_use_case_mgr* mgr);

/* Gets all available use cases for this sound card.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from ucm_create.
 * Returns:
 *    A bit field of CRAS_USE_CASE flags.
 */
cras_use_cases_t ucm_get_avail_use_cases(struct cras_use_case_mgr* mgr);

/* Sets the new use case for the given cras_use_case_mgr. This does not enable
 * the use case verb with snd_use_case_set(_verb). It sets the verb to use when
 * querying with snd_use_case_get*. To enable it, call ucm_enable_use_case after
 * ucm_set_use_case.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from ucm_create.
 *    use_case - The new use case to be set.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_set_use_case(struct cras_use_case_mgr* mgr,
                     enum CRAS_USE_CASE use_case);

/* Enables the use case by setting it current with snd_use_case_set(_verb).
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from ucm_create.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_enable_use_case(struct cras_use_case_mgr* mgr);

/* Checks if modifier for left right swap mode exists in ucm.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 * Returns:
 *    1 if it exists, 0 otherwise.
 */
int ucm_swap_mode_exists(struct cras_use_case_mgr* mgr);

/* Enables or disables swap mode for the given node_name. First checks
 * if the modifier is already enabled or disabled.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 *    enable - Enable device if non-zero.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_enable_swap_mode(struct cras_use_case_mgr* mgr,
                         const char* node_name,
                         int enable);

/* Checks if modifier of echo cancellation in ucm.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 * Returns:
 *    1 if it exists, 0 otherwise.
 */
int ucm_node_echo_cancellation_exists(struct cras_use_case_mgr* mgr);

/* Enables or disables echo cancellation. First checks if the modifier is
 * already enabled or disabled.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 *    enable - Enable device if non-zero.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_enable_node_echo_cancellation(struct cras_use_case_mgr* mgr,
                                      int enable);

/* Checks if modifier of noise suppression in ucm.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 * Returns:
 *    1 if it exists, 0 otherwise.
 */
int ucm_node_noise_suppression_exists(struct cras_use_case_mgr* mgr);

/* Enables or disables noise suppression. First checks if the modifier is
 * already enabled or disabled.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 *    enable - Enable device if non-zero.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_enable_node_noise_suppression(struct cras_use_case_mgr* mgr,
                                      int enable);

/* Checks if modifier of gain control in ucm.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 * Returns:
 *    1 if it exists, 0 otherwise.
 */
int ucm_node_gain_control_exists(struct cras_use_case_mgr* mgr);

/* Enables or disables gain control. First checks if the modifier is
 * already enabled or disabled.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 *    enable - Enable device if non-zero.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_enable_node_gain_control(struct cras_use_case_mgr* mgr, int enable);

/* Checks if modifier of noise cancellation for given node_name exists in ucm.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 * Returns:
 *    1 if it exists, 0 otherwise.
 */
int ucm_node_noise_cancellation_exists(struct cras_use_case_mgr* mgr,
                                       const char* node_name);

/* Enables or disables noise cancellation for the given node_name. First checks
 * if the modifier is already enabled or disabled.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 *    enable - Enable device if non-zero.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_enable_node_noise_cancellation(struct cras_use_case_mgr* mgr,
                                       const char* node_name,
                                       int enable);

/* Checks if modifier of spatial audio in ucm.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 * Returns:
 *    1 if it exists, 0 otherwise.
 */
int ucm_node_spatial_audio_exists(struct cras_use_case_mgr* mgr);

/* Enables or disables spatial audio. First checks if the modifier is
 * already enabled or disabled.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    node_name - The node name.
 *    enable - Enable device if non-zero.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_enable_node_spatial_audio(struct cras_use_case_mgr* mgr, int enable);

/* Enables or disables a UCM device.  First checks if the device is already
 * enabled or disabled.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The ucm device to enable of disable.
 *    enable - Enable device if non-zero.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_set_enabled(struct cras_use_case_mgr* mgr, const char* dev, int enable);

/* Gets the value of given flag name.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    flag_name - The name of the flag.
 * Returns:
 *    A pointer to the allocated string containing the flag value, or
 *    NULL if the flag is not set.
 */
char* ucm_get_flag(struct cras_use_case_mgr* mgr, const char* flag_name);

/* Gets the capture control name which associated with given ucm device.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    ucm_dev - The ucm device to get capture control for.
 * Returns:
 *    A pointer to the allocated string containing the name of the capture
 *    control, or NULL if no capture control is found.
 */
char* ucm_get_cap_control(struct cras_use_case_mgr* mgr, const char* ucm_dev);

/* Gets the new node type name which user wants to override the old one for
 * given ucm device.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    ucm_dev - The ucm device to override node type.
 * Returns:
 *    A pointer to the allocated string containing the new type name,
 *    or NULL if no override_type_name is found.
 */
const char* ucm_get_override_type_name(struct cras_use_case_mgr* mgr,
                                       const char* ucm_dev);

/* Gets the name of the ucm device for the given jack name.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    jack - The name of the jack to search for.
 *    direction - input or output
 * Rreturns:
 *    A pointer to the allocated string containing the name of the device, or
 *    NULL if no device is found.
 */
char* ucm_get_dev_for_jack(struct cras_use_case_mgr* mgr,
                           const char* jack,
                           enum CRAS_STREAM_DIRECTION direction);

/* Gets the name of the ucm device for the given mixer name.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    mixer - The name of the mixer control to search for.
 *    dir - input or output
 * Rreturns:
 *    A pointer to the allocated string containing the name of the device, or
 *    NULL if no device is found.
 */
char* ucm_get_dev_for_mixer(struct cras_use_case_mgr* mgr,
                            const char* mixer,
                            enum CRAS_STREAM_DIRECTION dir);

/* If there is an EDID file variable specified for dev, return it.  The EDID
 * file will be used for HDMI devices so supported audio formats can be checked.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for an EDID file.
 * Returns:
 *    A string containing the name of the edid file on Success (Must be freed
 *    later).  NULL if none found.
 */
const char* ucm_get_edid_file_for_dev(struct cras_use_case_mgr* mgr,
                                      const char* dev);
/* ELD control ID may not match device id.
 * Get the ELD control ID specified in UCM for the given device.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for ELD control id.
 * Returns:
 *    ELD control id for device.
 *    -ENOENT if not value not set.
 */
int ucm_get_eld_control_id_for_dev(struct cras_use_case_mgr* mgr,
                                   const char* dev);

/* Gets the dsp name which is associated with the given ucm device.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to get dsp name for.
 * Returns:
 *    A pointer to the allocated string containing the dsp name, or NULL if no
 *    dsp name is found.
 */
const char* ucm_get_dsp_name_for_dev(struct cras_use_case_mgr* mgr,
                                     const char* dev);

/* Gets the minimum buffer level for an output.  This level will add latency to
 * all streams playing on the output, but can be used to work around an
 * unreliable dma residue.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    level - The pointer to the returned value.
 *
 */
int ucm_get_min_buffer_level(struct cras_use_case_mgr* mgr,
                             unsigned int* level);

/* Gets the latency offset in ms for an output. This latency is not added but
 * only reported to the streams. This is to take into account latency not due to
 * device buffers.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to get the latency offset from.
 *    latency_offset_ms - The pointer to the returned value.
 * Returns:
 *    0 on success, other error codes on failure.
 */
int ucm_get_latency_offset_ms(struct cras_use_case_mgr* mgr,
                              const char* dev,
                              int* latency_offset_ms);

/* Gets the flag for enabling software volume.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 * Returns:
 *    1 on enable software volume.
 *    0 on disable software volume.
 *    -ENOENT if not value not set.
 *    -EINVAL on invalid value.
 */
int ucm_get_use_software_volume(struct cras_use_case_mgr* mgr);

/* Gets the value for default node gain.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for default node gain.
 *    gain - The pointer to the returned value.
 * Returns:
 *    0 on success, other error codes on failure.
 */
int ucm_get_default_node_gain(struct cras_use_case_mgr* mgr,
                              const char* dev,
                              long* gain);

/* Gets the value for intrinsic sensitivity.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to query for intrinsic volume.
 *    sensitivity - The pointer to the returned value.
 * Returns:
 *    0 on success, other error codes on failure.
 */
int ucm_get_intrinsic_sensitivity(struct cras_use_case_mgr* mgr,
                                  const char* dev,
                                  long* sensitivity);

/* Gets the flag if an input device can preempt hotword recording.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for preempt hotword flag.
 * Returns:
 *    Non-zero value means input can preempt hotword recording, otherwise
 *    return zero.
 */
int ucm_get_preempt_hotword(struct cras_use_case_mgr* mgr, const char* dev);

/* Gets the ALSA device index on the card for given UCM dev.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The UCM device to check for ALSA device index.
 *    direction - playback(CRAS_STREAM_OUTPUT) or capture(CRAS_STREAM_INPUT).
 * Returns:
 *    Non-negative integer for the ALSA device index on the card, -1 if not
 *    found. The ALSA device index is parsed from the PCM name which is
 *    formatted as "hw:<some-name>,<idx>".
 */
int ucm_get_alsa_dev_idx_for_dev(struct cras_use_case_mgr* mgr,
                                 const char* dev,
                                 enum CRAS_STREAM_DIRECTION direction);

/* Gets the node name of the echo reference device on the card.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check echo reference for.
 * Returns:
 *    String containing the node name of the echo reference to this
 *    dev, caller is responsible to free it later. NULL if echo reference
 *    doesn't exist.
 */
const char* ucm_get_echo_reference_dev_name_for_dev(
    struct cras_use_case_mgr* mgr,
    const char* dev);

/* Gets the sample rate at which to run this device.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for sample rate.
 *    direction - playback(CRAS_STREAM_OUTPUT) or capture(CRAS_STREAM_INPUT).
 * Returns:
 *    The sample rate if specified, or negative error if not.
 */
int ucm_get_sample_rate_for_dev(struct cras_use_case_mgr* mgr,
                                const char* dev,
                                enum CRAS_STREAM_DIRECTION direction);

/* Gets the channel count at which to run this device.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for channel count.
 *    direction - playback(CRAS_STREAM_OUTPUT) or capture(CRAS_STREAM_INPUT).
 *    channels - The pointer to the returned channel count.
 * Returns:
 *    0 on success, other error codes on failure.
 */
int ucm_get_channels_for_dev(struct cras_use_case_mgr* mgr,
                             const char* dev,
                             enum CRAS_STREAM_DIRECTION direction,
                             size_t* channels);
/* Gets the playback volume steps at which to run this device. Only used for USB
 * card.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for channel count.
 *    playback_number_of_volume_steps - The pointer to the returned playback
 * volume steps. Returns: 0 on success, other error codes on failure.
 */
int ucm_get_playback_number_of_volume_steps_for_dev(
    struct cras_use_case_mgr* mgr,
    const char* dev,
    int32_t* playback_number_of_volume_steps);

/* Gets the playback channel map for this device.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for playback channel map.
 *    channel_layout - The channel layout to fill.
 */
int ucm_get_playback_chmap_for_dev(struct cras_use_case_mgr* mgr,
                                   const char* dev,
                                   int8_t* channel_layout);

/* Gets the capture channel map for this device.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for capture channel map.
 *    channel_layout - The channel layout to fill.
 */
int ucm_get_capture_chmap_for_dev(struct cras_use_case_mgr* mgr,
                                  const char* dev,
                                  int8_t* channel_layout);

/* Gets the mixer names for the coupled mixer controls of this device
 * on the card.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for coupled mixer controls.
 * Returns:
 *    A list of cras_alsa_control.
 */
struct mixer_name* ucm_get_coupled_mixer_names(struct cras_use_case_mgr* mgr,
                                               const char* dev);

/* Gets a list of UCM sections
 *
 * The data includes the represented devices and their controls.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer return from alsa_ucm_create.
 *
 * Returns:
 *    A list of ucm_section or NULL. Free it with ucm_section_free_list().
 */
struct ucm_section* ucm_get_sections(struct cras_use_case_mgr* mgr);

/* Gets the list of supported hotword model names.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 * Returns:
 *    String containing comma separated model names, e.g 'en,fr,zh'. Needs
 *    to be freed by caller.
 */
char* ucm_get_hotword_models(struct cras_use_case_mgr* mgr);

/* Sets the desired hotword model.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    model - locale for model
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_set_hotword_model(struct cras_use_case_mgr* mgr, const char* model);

/* Enable previously set hotword modifier
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int ucm_enable_hotword_model(struct cras_use_case_mgr* mgr);

/* Disable all hotword model modifiers
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 */
void ucm_disable_all_hotword_models(struct cras_use_case_mgr* mgr);

/* Checks if this card has fully specified UCM config.
 *
 * Args:
 *   mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 * Returns:
 *   1 if this UCM uses fully specified UCM config. 0 otherwise.
 */
int ucm_has_fully_specified_ucm_flag(struct cras_use_case_mgr* mgr);

/* Gets the playback mixer name of this device on the card.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for device name
 * Returns:
 *    A pointer to the allocated string containing the mixer name, or NULL
 *    if no device name is found.
 */
const char* ucm_get_playback_mixer_elem_for_dev(struct cras_use_case_mgr* mgr,
                                                const char* dev);

/* Gets the capture mixer name of this device on the card.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for device name
 * Returns:
 *    A pointer to the allocated string containing the mixer name, or NULL
 *    if no device name is found.
 */
const char* ucm_get_capture_mixer_elem_for_dev(struct cras_use_case_mgr* mgr,
                                               const char* dev);

/* Gets the mixer names for the main volume controls on the card.
 *
 * The main volume controls in the list are considered in series.
 * If 3 controls are specified, MainVolumeNames "A,B,C", with dB ranges
 * A=-10dB~0dB, B=-20dB~0dB, C=-30dB~0dB, then applying -35dB overall volume
 * sets A=-10dB, B=-20dB, C=-5dB.
 * The volume control affects all output on this card, e.g.
 * speaker and headphone.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 * Returns:
 *    names - A list of mixer_name.
 */
struct mixer_name* ucm_get_main_volume_names(struct cras_use_case_mgr* mgr);

/* The callback to be provided with a reference to the section name.
 *
 * Args:
 *    section_name: The name of a SectionDevice in UCM.
 *    arg - Argument to pass to this callback.
 */
typedef void (*ucm_list_section_devices_callback)(const char* section_name,
                                                  void* arg);

/* Invokes the provided callback once for each section with matched device name.
 *
 * Iterate through each SectionDevice in UCM of this card. Invoke callback if
 * "PlaybackPCM" for output or "CapturePCM" for input of the section matches
 * the specified device_name.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    device_name - A string for device name of format "card_name:device_index".
 *    cb - Function to call for each section.
 *    cb_arg - Argument to pass to cb.
 * Returns:
 *    Number of sections listed.
 */
int ucm_list_section_devices_by_device_name(
    struct cras_use_case_mgr* mgr,
    enum CRAS_STREAM_DIRECTION direction,
    const char* device_name,
    ucm_list_section_devices_callback cb,
    void* cb_arg);

/* Gets the jack name of this device on the card.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for jack name.
 * Returns:
 *    A pointer to the allocated string containing the jack name, or NULL
 *    if no jack name is found.
 */
const char* ucm_get_jack_name_for_dev(struct cras_use_case_mgr* mgr,
                                      const char* dev);

/* Gets the jack type of this device on the card.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for jack type.
 * Returns:
 *    A pointer to the allocated string containing the jack type, or NULL
 *    if no jack type is found or the found jack type is invalid. The valid
 *    jack types are "hctl" or "gpio".
 */
const char* ucm_get_jack_type_for_dev(struct cras_use_case_mgr* mgr,
                                      const char* dev);

/* Gets the jack dev of this device on the card.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for jack name.
 * Returns:
 *    A pointer to the allocated string containing the input jack name, or NULL
 *    if no jack name is found.
 */
const char* ucm_get_jack_dev_for_dev(struct cras_use_case_mgr* mgr,
                                     const char* dev);

/* Gets the jack control of this device on the card.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check for jack type.
 * Returns:
 *    A pointer to the allocated string containing the alsa jack name, or NULL
 *    if no jack type is found or the found jack type is invalid.
 */
const char* ucm_get_jack_control_for_dev(struct cras_use_case_mgr* mgr,
                                         const char* dev);

/* Gets the jack switch number for this device.
 * Some sound cards can detect multiple types of connections into the
 * audio jack - for example distinguish between line-out and headphones
 * by measuring the impedance on the other end. In that case we want each
 * jack to have it's own I/O node so that each can have it's own volume
 * settings. This allows us to specify the jack used more exactly.
 * Valid values are defined in /usr/include/linux/input.h.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check.
 * Returns:
 *    A value >= 0 when the switch is defined, or -1 otherwise.
 */
int ucm_get_jack_switch_for_dev(struct cras_use_case_mgr* mgr, const char* dev);

/* Gets the DMA period time in microseconds for the given device.
 *
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 *    dev - The device to check.
 * Returns:
 *    A value > 0, or 0 if no period is defined.
 */
unsigned int ucm_get_dma_period_for_dev(struct cras_use_case_mgr* mgr,
                                        const char* dev);

/* Gets the flag of optimization for no stream state.
 * This flag enables no_stream ops in alsa_io.
 * Args:
 *    mgr - The cras_use_case_mgr pointer returned from alsa_ucm_create.
 * Returns:
 *    1 if the flag is enabled. 0 otherwise.
 */
unsigned int ucm_get_optimize_no_stream_flag(struct cras_use_case_mgr* mgr);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_ALSA_UCM_H_
