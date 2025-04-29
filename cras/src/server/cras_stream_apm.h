/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_STREAM_APM_H_
#define CRAS_SRC_SERVER_CRAS_STREAM_APM_H_

#include "cras/src/common/cras_types_internal.h"
#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cras_audio_area;
struct cras_audio_format;
struct cras_apm;
struct cras_stream_apm;
struct cras_iodev;
struct float_buffer;

// APM uses 10ms per block, so it's 100 blocks per second.
#define APM_NUM_BLOCKS_PER_SECOND 100

// Initialize the stream apm for analyzing output data.
int cras_stream_apm_init(const char* device_config_dir);

// Reloads the aec config. Used for debug and tuning.
void cras_stream_apm_reload_aec_config();

// Deinitialize stream apm to free all allocated resources.
int cras_stream_apm_deinit();

/*
 * Creates an stream apm to hold all APM instances created when a stream
 * attaches to iodev(s). This should be called in main thread.
 *
 * Below diagram explains the life cycle of an APM instance, how are
 * related APIs used, and in which thread should each API be called.
 *
 * Main thread                     Audio thread
 * maintaining stream_apm            maintaining active_apms
 * -----------                     ------------
 * cras_stream_apm_create
 * cras_stream_apm_add_apm    ->   cras_stream_apm_start_apm
 *
 *                                 cras_stream_apm_get_active_apm
 *                                 cras_stream_apm_process
 *                                 cras_stream_apm_get_processed
 *                                 cras_stream_apm_put_processed
 *
 * Upon any event that requires certain apm instances to change settings.
 * Stop and remove the apms, then add apms with new settings and start
 * them. One way to achieved this is by reconnecting the stream.
 *
 * cras_stream_apm_remove_apm <-   cras_stream_apm_stop
 * cras_stream_apm_add_apm    ->   cras_stream_apm_start_apm
 *
 *                                 ...
 *
 * Finally when done with them, clean up by stop and remove all apm
 * instances and destroy the cras_stream_apm object.
 *
 * cras_stream_apm_remove_apm <-   cras_stream_apm_stop
 * cras_stream_apm_destroy
 *
 * Args:
 *    effects - Bit map specifying the enabled effects on this stream.
 */
struct cras_stream_apm* cras_stream_apm_create(uint64_t effects);

/*
 * Creates a cras_apm associated to given idev and adds it to the stream.
 * If there already exists an APM instance linked to idev, we assume
 * the open format is unchanged so just return it. This should be called
 * in main thread.
 * Args:
 *    stream - The stream apm holding APM instances.
 *    idev - Pointer to the input iodev to add new APM for.
 *    dev_fmt - Format of the device associated with this cras_apm.
 *    stream_fmt - Format of the stream associated with this cras_apm.
 */
struct cras_apm* cras_stream_apm_add(
    struct cras_stream_apm* stream,
    struct cras_iodev* idev,
    const struct cras_audio_format* dev_fmt,
    const struct cras_audio_format* stream_fmt);

/*
 * Gets the active APM instance that is associated to given stream and dev pair.
 * This should be called in audio thread.
 * Args:
 *    stream - The stream_apm holding APM instances.
 *    idev - The iodev as key to look up associated APM.
 */
struct cras_apm* cras_stream_apm_get_active(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev);

/*
 * Starts the APM instance in the stream that is associated with idev by
 * adding it to the active APM list in audio thread.
 */
void cras_stream_apm_start(struct cras_stream_apm* stream,
                           const struct cras_iodev* idev);

/*
 * Stops the APM instance in the stream that is associated with idev by
 * removing it from the active APM list in audio thread.
 */
void cras_stream_apm_stop(struct cras_stream_apm* stream,
                          struct cras_iodev* idev);

/*
 * Gets the effects bit map of the stream APM, specified by the stream.
 * Args:
 *    stream - The stream apm holding APM instances.
 */
uint64_t cras_stream_apm_get_effects(struct cras_stream_apm* stream);

// Removes all cras_apm from stream and destroys it.
int cras_stream_apm_destroy(struct cras_stream_apm* stream);

/*
 * Removes an APM from the stream, expected to be used when an iodev is no
 * longer open for the client stream holding the stream APM. This should
 * be called in main thread.
 * Args:
 *    stream - The stream APM holding APM instances.
 *    idev - Device pointer used to look up which apm to remove.
 */
void cras_stream_apm_remove(struct cras_stream_apm* stream,
                            const struct cras_iodev* idev);

/* Passes audio data from hardware for cras_apm to process.
 * Args:
 *    apm - The cras_apm instance.
 *    input - Float buffer from device for apm to process.
 *    offset - Offset in |input| to note the data position to start
 *        reading.
 *    preprocessing_gain_scalar - Gain to apply before processing.
 */
int cras_stream_apm_process(struct cras_apm* apm,
                            struct float_buffer* input,
                            unsigned int offset,
                            float preprocessing_gain_scalar);

/* Gets the APM processed data in the form of audio area.
 * Args:
 *    apm - The cras_apm instance that owns the audio area pointer and
 *        processed data.
 * Returns:
 *    The audio area used to read processed data. No need to free
 *    by caller.
 */
struct cras_audio_area* cras_stream_apm_get_processed(struct cras_apm* apm);

/* Tells |apm| that |frames| of processed data has been used, so |apm|
 * can allocate space to read more from input device.
 * Args:
 *    apm - The cras_apm instance owns the processed data.
 *    frames - The number in frames of processed data to mark as used.
 */
void cras_stream_apm_put_processed(struct cras_apm* apm, unsigned int frames);

/* Gets the format of the actual data processed by webrtc-apm library.
 * Args:
 *    apm - The cras_apm instance holding audio data and format info.
 */
struct cras_audio_format* cras_stream_apm_get_format(struct cras_apm* apm);

/*
 * Gets if this apm instance is using tuned settings.
 */
bool cras_stream_apm_get_use_tuned_settings(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev);

/* Sets debug recording to start or stop.
 * Args:
 *    stream - Stream contains the apm instance to start/stop debug recording.
 *    idev - Use as key to look up specific apm to do aec dump.
 *    start - True to set debug recording start, otherwise stop.
 *    fd - File descriptor to aec dump destination.
 */
void cras_stream_apm_set_aec_dump(struct cras_stream_apm* stream,
                                  const struct cras_iodev* idev,
                                  int start,
                                  int fd);

/* Sets an iodev as echo ref for a stream with AEC effect.
 * Args:
 *    stream - Stream apm containing the apm instances with AEC effect.
 *    echo_ref - pointer to an iodev assigned as echo ref for |stream|.
 * Returns:
 *    0 if success, otherwise error code.
 */
int cras_stream_apm_set_aec_ref(struct cras_stream_apm* stream,
                                struct cras_iodev* echo_ref);

/* Called from main thread to notify audio thread the target stream for
 * voice activity detection has changed.
 * Args:
 *    vad_target - The new voice activity detection target. NULL disables VAD
 *                 on all APMs.
 */
void cras_stream_apm_notify_vad_target_changed(
    struct cras_stream_apm* vad_target);

// Initializes the handler of cras_stream_apm_message in the main thread.
int cras_stream_apm_message_handler_init();

// Returns whether the stream_apm can be used for voice activity detection.
// Returns false if stream is NULL.
bool cras_stream_apm_vad_available(struct cras_stream_apm* stream);

// Returns true if the stream effects requires the use of APM.
bool cras_apm_needed_for_effects(uint64_t effects, bool cras_processor_needed);

// Sets whether DSP input effects are blocked directly.
// Exposed for testing only. Normal code should use
// cras_stream_apm_notify_dsp_input_effects_blocked.
void apm_thread_set_dsp_input_effects_blocked(bool blocked);

// Sets whether DSP input effects are blocked.
void cras_stream_apm_notify_dsp_input_effects_blocked(bool blocked);

// Returns the number of running AP NC.
unsigned cras_apm_state_get_num_nc();

// Returns the last time AP NC was closed.
struct timespec cras_apm_state_get_last_nc_closed();

struct cras_stream_apm_state {
  CRAS_STREAM_ACTIVE_AP_EFFECT active_ap_effects;
  uint64_t webrtc_apm_forward_blocks_processed;
  uint64_t webrtc_apm_reverse_blocks_processed;
};

// Returns the state of the APM.
struct cras_stream_apm_state cras_stream_apm_get_state(
    struct cras_stream_apm* stream);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_STREAM_APM_H_
