/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_CLIENT_H_
#define CRAS_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/select.h>

#include "cras_iodev_info.h"
#include "cras_types.h"
#include "cras_util.h"

struct cras_client;
struct cras_stream_params;

/* Callback for audio received or transmitted.
 * Args (All pointer will be validi - except user_arg, that's up to the user):
 *    client: The client requesting service.
 *    stream_id - Unique identifier for the stream needing data read/written.
 *    samples - Read or write samples to/form here.
 *    frames - Maximum number of frames to read or write.
 *    sample_time - Playback time for the first sample read/written.
 *    user_arg - Value passed to add_stream;
 * Return:
 *    0 on success, or a negative number if there is a stream-fatal error.
 */
typedef int (*cras_playback_cb_t)(struct cras_client *client,
				  cras_stream_id_t stream_id,
				  uint8_t *samples,
				  size_t frames,
				  const struct timespec *sample_time,
				  void *user_arg);
/* Callback for handling errors. */
typedef int (*cras_error_cb_t)(struct cras_client *client,
			       cras_stream_id_t stream_id,
			       int error,
			       void *user_arg);

/*
 * Client handling.
 */

/* Creates a new client.
 * Args:
 *    client - Filled with a pointer to the new client.
 * Returns:
 *    0 on success (*client is filled with a valid cras_client pointer).
 *    Negative error code on failure(*client will be NULL).
 */
int cras_client_create(struct cras_client **client);

/* Destroys a client.
 * Args:
 *    client - returned from "cras_client_create".
 */
void cras_client_destroy(struct cras_client *client);

/* Connects a client to the running server.
 * Args:
 *    client - pointer returned from "cras_client_create".
 * Returns:
 *    0 on success, or a negative error code on failure (from errno.h).
 */
int cras_client_connect(struct cras_client *client);

/* Begins running a client.
 * Args:
 *    client - the client to start (from cras_client_create).
 * Returns:
 *    0 on success, -EINVAL if the client pointer is NULL, or -ENOMEM if there
 *    isn't enough memory to start the thread.
 */
int cras_client_run_thread(struct cras_client *client);

/* Stops running a client.
 * Args:
 *    client - the client to stop (from cras_client_create).
 * Returns:
 *    0 on success, -EINVAL if the client isn't valid or isn't running.
 */
int cras_client_stop(struct cras_client *client);

/* Returns the current list of output devices.
 * Args:
 *    client - The client to stop (from cras_client_create).
 *    devs - Array that will be filled with device info.
 *    max_devs - Maximum number of devices to put in the array.
 * Returns:
 *    The number of devices available.  This may be more that max_devs passed
 *    in, this indicates that all of the iodev_info wouldn't fit in the provided
 *    array.
 */
int cras_client_get_output_devices(struct cras_client *client,
				   struct cras_iodev_info *devs,
				   size_t max_devs);

/* Returns the current list of input devices.
 * Args:
 *    client - The client to stop (from cras_client_create).
 *    devs - Array that will be filled with device info.
 *    max_devs - Maximum number of devices to put in the array.
 * Returns:
 *    The number of devices available.  This may be more that max_devs passed
 *    in, this indicates that all of the iodev_info wouldn't fit in the provided
 *    array.
 */
int cras_client_get_input_devices(struct cras_client *client,
				  struct cras_iodev_info *devs,
				  size_t max_devs);

/* Returns the current list of clients attached to the server.
 * Args:
 *    client - This client (from cras_client_create).
 *    clients - Array that will be filled with a list of attached clients.
 *    max_clients - Maximum number of clients to put in the array.
 * Returns:
 *    The number of attached clients.  This may be more that max_clients passed
 *    in, this indicates that all of the clients wouldn't fit in the provided
 *    array.
 */
int cras_client_get_attached_clients(struct cras_client *client,
				     struct cras_attached_client_info *clients,
				     size_t max_clients);

/*
 * Stream handling.
 */

/* Setup stream configuration parameters.
 * Args:
 *    direction - playback(CRAS_STREAM_OUTPUT) or capture(CRAS_STREAM_INPUT).
 *    buffer_frames - total number of audio frames to buffer (dictates latency).
 *    cb_threshold - For playback, call back for more data when the buffer
 *        reaches this level. For capture, this is ignored (Audio callback will
 *        be called when buffer_frames have been captured).
 *    min_cb_level - For playback, the minimum amout of frames that must be able
 *        to be written before calling back for more data (useful if you are
 *        processing audio in blocks of a certain size(e.g. 512 or 1024 frames).
 *        Ignored for capture streams.
 *    stream_type - media or talk (currently only support "default").
 *    flags - None currently used.
 *    user_data - Pointer that will be passed to the callback.
 *    aud_cb - Called when audio is needed(playback) or ready(capture). Allowed
 *        return EOF to indicate that the stream should terminate.
 *    err_cb - Called when there is an error with the stream.
 *    format - The format of the audio stream.  Specifies bits per sample,
 *        number of channels, and sample rate.
 */
struct cras_stream_params *cras_client_stream_params_create(
		enum CRAS_STREAM_DIRECTION direction,
		size_t buffer_frames,
		size_t cb_threshold,
		size_t min_cb_level,
		enum CRAS_STREAM_TYPE stream_type,
		uint32_t flags,
		void *user_data,
		cras_playback_cb_t aud_cb,
		cras_error_cb_t err_cb,
		struct cras_audio_format *format);

/* Destroy stream params created with cras_client_stream_params_create. */
void cras_client_stream_params_destroy(struct cras_stream_params *params);

/* Creates a new stream and return the stream id or < 0 on error.
 * Args:
 *    client - The client to add the stream to (from cras_client_create).
 *    stream_id_out - On success will be filled with the new stream id.
 *        Guaranteed to be set before any callbacks are made.
 *    config - The cras_stream_params struct specifying the parameters for the
 *        stream.
 * Returns:
 *    0 on success, negative error code on failure (from errno.h).
 */
int cras_client_add_stream(struct cras_client *client,
			   cras_stream_id_t *stream_id_out,
			   struct cras_stream_params *config);

/* Removes a currently playing/capturing stream.
 * Args:
 *    client - Client to remove the stream (returned from cras_client_create).
 *    stream_id - ID returned from add_stream to identify the stream to remove.
 * Returns:
 *    0 on success negative error code on failure (from errno.h).
 */
int cras_client_rm_stream(struct cras_client *client,
			  cras_stream_id_t stream_id);

/* Sets the volume scaling factor for the given stream.
 * Args:
 *    client - Client owning the stream.
 *    stream_id - ID returned from add_stream.
 *    volume_scaler - 0.0-1.0 the new value to scale this stream by.
 */
int cras_client_set_stream_volume(struct cras_client *client,
				  cras_stream_id_t stream_id,
				  float volume_scaler);

/* Moves stream type to a different input or output.
 * Args:
 *    client - The client connected to the server with client_connect.
 *    stream_type - The type of stream to move.
 *    iodev - The index of the device to move the stream to.
 * Returns:
 *    0 if the message was sent to the server successfully.  A negative error
 *    code if there was a communication error (from errno.h).
 */
int cras_client_switch_iodev(struct cras_client *client,
			     enum CRAS_STREAM_TYPE stream_type,
			     int iodev);

/*
 * System level functions.
 */

/* Sets the volume of the system.  Volume here ranges from 0 to 100, and will be
 * translated to dB based on the output-specific volume curve.
 * Args:
 *    client - Client owning the stream.
 *    volume - 0-100 the new volume index.
 * Returns:
 *    0 for success, -EPIPE if there is an I/O error talking to the server, or
 *    -EINVAL if 'client' is invalid.
 */
int cras_client_set_system_volume(struct cras_client *client, size_t volume);

/* Sets the capture gain of the system. Gain is specified in dBFS * 100.  For
 * example 5dB of gain would be specified with an argument of 500, while -10
 * would be specified with -1000.
 * Args:
 *    client - Client owning the stream.
 *    gain - The gain in dBFS * 100.
 * Returns:
 *    0 for success, -EPIPE if there is an I/O error talking to the server, or
 *    -EINVAL if 'client' is invalid.
 */
int cras_client_set_system_capture_gain(struct cras_client *client, long gain);

/* Sets the mute state of the system.
 * Args:
 *    client - Client owning the stream.
 *    mute - 0 is un-mute, 1 is muted.
 * Returns:
 *    0 for success, -EPIPE if there is an I/O error talking to the server, or
 *    -EINVAL if 'client' is invalid.
 */
int cras_client_set_system_mute(struct cras_client *client, int mute);

/* Sets the capture mute state of the system.  Recordings will be muted when
 * this is set.
 * Args:
 *    client - Client owning the stream.
 *    mute - 0 is un-mute, 1 is muted.
 * Returns:
 *    0 for success, -EPIPE if there is an I/O error talking to the server, or
 *    -EINVAL if 'client' is invalid.
 */
int cras_client_set_system_capture_mute(struct cras_client *client, int mute);

/* Gets the current system volume.
 * Args:
 *    client - The client from cras_client_create.
 * Returns:
 *    The current system volume between 0 and 100.
 */
size_t cras_client_get_system_volume(struct cras_client *client);

/* Gets the current system capture gain.
 * Args:
 *    client - The client from cras_client_create.
 * Returns:
 *    The current system capture volume in dB * 100.
 */
long cras_client_get_system_capture_gain(struct cras_client *client);

/* Gets the current system mute state.
 * Args:
 *    client - The client from cras_client_create.
 * Returns:
 *    0 if not muted, 1 if it is.
 */
int cras_client_get_system_muted(struct cras_client *client);

/* Gets the current system captue mute state.
 * Args:
 *    client - The client from cras_client_create.
 * Returns:
 *    0 if capture is not muted, 1 if it is.
 */
int cras_client_get_system_capture_muted(struct cras_client *client);

/* Gets the current minimum system volume.
 * Args:
 *    client - The client from cras_client_create.
 * Returns:
 *    The minimum value for the current output device in dBFS * 100.  This is
 *    the level of attenuation at volume == 1.
 */
long cras_client_get_system_min_volume(struct cras_client *client);

/* Gets the current maximum system volume.
 * Args:
 *    client - The client from cras_client_create.
 * Returns:
 *    The maximum value for the current output device in dBFS * 100.  This is
 *    the level of attenuation at volume == 100.
 */
long cras_client_get_system_max_volume(struct cras_client *client);

/* Gets the current minimum system capture gain.
 * Args:
 *    client - The client from cras_client_create.
 * Returns:
 *    The minimum capture gain for the current input device in dBFS * 100.
 */
long cras_client_get_system_min_capture_gain(struct cras_client *client);

/* Gets the current maximum system capture gain.
 * Args:
 *    client - The client from cras_client_create.
 * Returns:
 *    The maximum capture gain for the current input device in dBFS * 100.
 */
long cras_client_get_system_max_capture_gain(struct cras_client *client);

/*
 * Utility functions.
 */

/* Returns the number of bytes in an audio frame for a stream.
 * Args:
 *    format - The format of the audio stream.  Specifies bits per sample,
 *        number of channels, and sample rate.
 * Returns:
 *   Positive number of bytes in a frame, or a negative error code if fmt is
 *   NULL.
 */
int cras_client_format_bytes_per_frame(struct cras_audio_format *fmt);

/* Deprecated. */
int cras_client_bytes_per_frame(struct cras_client *client,
				cras_stream_id_t stream_id);

/* For playback streams, calculates the latency of the next sample written.
 * Only valid when called from the audio callback function for the stream
 * (aud_cb).
 * Args:
 *    client - The client the stream is attached to(from cras_client_create).
 *    stream_id - Returned from add_stream to identify which stream.
 *    sample_time - The sample time stamp passed in to aud_cb.
 *    delay - Out parameter will be filled with the latency.
 * Returns:
 *    0 on success, -EINVAL if delay is NULL.
 */
int cras_client_calc_playback_latency(const struct timespec *sample_time,
				      struct timespec *delay);

/* For capture returns the latency of the next frame to be read from the buffer
 * (based on when it was captured).  Only valid when called from the audio
 * callback function for the stream (aud_cb).
 * Args:
 *    sample_time - The sample time stamp passed in to aud_cb.
 *    delay - Out parameter will be filled with the latency.
 * Returns:
 *    0 on success, -EINVAL if delay is NULL.
 */
int cras_client_calc_capture_latency(const struct timespec *sample_time,
				     struct timespec *delay);

/* Deprecated TODO(dgreid) remove. */
int cras_client_calc_latency(const struct cras_client *client,
			     cras_stream_id_t stream_id,
			     const struct timespec *sample_time,
			     struct timespec *delay);


#ifdef __cplusplus
}
#endif

#endif /* CRAS_CLIENT_H_ */
