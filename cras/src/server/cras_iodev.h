/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * cras_iodev represents playback or capture devices on the system.  Each iodev
 * will attach to a thread to render or capture audio.  For playback, this
 * thread will gather audio from the streams that are attached to the device and
 * render the samples it gets to the iodev.  For capture the process is
 * reversed, the samples are pulled from the device and passed on to the
 * attached streams.
 */
#ifndef CRAS_IODEV_H_
#define CRAS_IODEV_H_

#include "cras_dsp.h"
#include "cras_iodev_info.h"
#include "cras_messages.h"

struct cras_rstream;
struct cras_audio_format;
struct audio_thread;

/* An input or output device, that can have audio routed to/from it.
 * set_volume - Function to call if the system volume changes.
 * set_mute - Function to call if the system mute state changes.
 * set_capture_gain - Function to call if the system capture_gain changes.
 * set_capture_mute - Function to call if the system capture mute state changes.
 * open_dev - Opens the device.
 * close_dev - Closes the device if it is open.
 * is_open - Checks if the device has been openned.
 * update_supported_formats - Refresh supported frame rates and channel counts.
 * set_as_default - Function to call when this device is set as system default.
 * frames_queued - The number of frames in the audio buffer.
 * delay_frames - The delay of the next sample in frames.
 * get_buffer - Returns a buffer to read/write to/from.
 * put_buffer - Marks a buffer from get_buffer as read/written.
 * dev_running - Checks if the device is playing or recording.
 * format - The audio format being rendered or captured.
 * info - Unique identifier for this device (index and name).
 * direction - Input or Output.
 * supported_rates - Array of sample rates supported by device 0-terminated.
 * supported_channel_counts - List of number of channels supported by device.
 * buffer_size - Size of the audio buffer in frames.
 * used_size - Number of frames that are used for audio.
 * cb_threshold - Level below which to call back to the client (in frames).
 * dsp_context - The context used for dsp processing on the audio data.
 * thread - The audio thread using this device, NULL if none.
 */
struct cras_iodev {
	void (*set_volume)(struct cras_iodev *iodev);
	void (*set_mute)(struct cras_iodev *iodev);
	void (*set_capture_gain)(struct cras_iodev *iodev);
	void (*set_capture_mute)(struct cras_iodev *iodev);
	int (*open_dev)(struct cras_iodev *iodev);
	int (*close_dev)(struct cras_iodev *iodev);
	int (*is_open)(const struct cras_iodev *iodev);
	int (*update_supported_formats)(struct cras_iodev *iodev);
	void (*set_as_default)(struct cras_iodev *iodev);
	int (*frames_queued)(const struct cras_iodev *iodev);
	int (*delay_frames)(const struct cras_iodev *iodev);
	int (*get_buffer)(struct cras_iodev *iodev,
			  uint8_t **dst,
			  unsigned *frames);
	int (*put_buffer)(struct cras_iodev *iodev, unsigned nwritten);
	int (*dev_running)(const struct cras_iodev *iodev);
	struct cras_audio_format *format;
	struct cras_iodev_info info;
	enum CRAS_STREAM_DIRECTION direction;
	size_t *supported_rates;
	size_t *supported_channel_counts;
	snd_pcm_uframes_t buffer_size;
	snd_pcm_uframes_t used_size;
	snd_pcm_uframes_t cb_threshold;
	struct cras_dsp_context *dsp_context;
	struct audio_thread *thread;
	struct cras_iodev *prev, *next;
};

/*
 * Utility functions to be used by iodev implementations.
 */

/* Initializes the cras_iodev structure.
 * Args:
 *    iodev - The device to initialize.
 *    thread_function - The function to run for playback/capture threads.
 *    thread_arg - Passed to thread_function when it is run.
 * Returns:
 *    0 on success or negative error on failure.
 */
int cras_iodev_init(struct cras_iodev *iodev,
		    void *(*thread_function)(void *arg),
		    void *thread_data);

/* Un-initializes a cras_iodev structure that was setup by cras_iodev_init().
 * Args:
 *    iodev - The device to initialize.
 */
void cras_iodev_deinit(struct cras_iodev *iodev);

/* Sets up the iodev for the given format if possible.  If the iodev can't
 * handle the requested format, it will modify the fmt parameter to inform the
 * caller of the actual format. It also allocates a dsp context for the iodev.
 * Args:
 *    iodev - the iodev you want the format for.
 *    fmt - pass in the desired format, is filled with the actual
 *      format on return.
 */
int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt);

/* Clear the format previously set for this iodev. It also release the
 * dsp context for this iodev.
 *
 * Args:
 *    iodev - the iodev you want to free the format.
 */
void cras_iodev_free_format(struct cras_iodev *iodev);

/* Adds a stream to the iodev.
 * Args:
 *    iodev - The device to add the stream to.
 *    stream - The stream to add.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int cras_iodev_append_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream);

/* Removes a stream from the iodev.
 * Args:
 *    iodev - The device to remove the stream from.
 *    stream - The stream previously added with cras_iodev_append_stream().
 * Returns:
 *    0 on success or negative error code on failure.
 */
int cras_iodev_delete_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream);

/* Fill timespec ts with the time to sleep based on the number of frames and
 * frame rate.
 * Args:
 *    frames - Number of frames in buffer..
 *    frame_rate - 44100, 48000, etc.
 *    ts - Filled with the time to sleep for.
 */
void cras_iodev_fill_time_from_frames(size_t frames,
				      size_t frame_rate,
				      struct timespec *ts);

/* Sets the timestamp for when the next sample will be rendered.  Determined by
 * combining the current time with the playback latency specified in frames.
 * Args:
 *    frame_rate - in Hz.
 *    frames - Delay specified in frames.
 *    ts - Filled with the time that the next sample will be played.
 */
void cras_iodev_set_playback_timestamp(size_t frame_rate,
				       size_t frames,
				       struct timespec *ts);

/* Sets the time that the first sample in the buffer was captured at the ADC.
 * Args:
 *    frame_rate - in Hz.
 *    frames - Delay specified in frames.
 *    ts - Filled with the time that the next sample was captured.
 */
void cras_iodev_set_capture_timestamp(size_t frame_rate,
				      size_t frames,
				      struct timespec *ts);

/* Configures when to wake up, the minimum amount free before refilling, and
 * other params that are independent of the hw configuration.
 * Args:
 *    buffer_size - Size of the audio buffer in frames.
 *    cb_threshold - Level below which to call back to the client (in frames).
 */
void cras_iodev_config_params(struct cras_iodev *iodev,
			      unsigned int buffer_size,
			      unsigned int cb_threshold);

/* Handles a plug event happening on this iodev.
 * Args:
 *    iodev - device on which a plug event was detected.
 *    plugged - true if the device was plugged, false for unplugged.
 */
void cras_iodev_plug_event(struct cras_iodev *iodev, int plugged);

/* Checks if the device is known to be plugged.  This is set when a jack event
 * is received from an ALSA jack or a GPIO.
 */
static inline int cras_iodev_is_plugged_in(const struct cras_iodev *iodev)
{
	return iodev->info.plugged;
}

/* Returns the last time that an iodev had a plug attached event. */
static inline struct timeval cras_iodev_last_plugged_time(
		struct cras_iodev *iodev)
{
	return iodev->info.plugged_time;
}

/* Returns true if a was plugged more recently than b. */
static inline int cras_iodev_plugged_more_recently(const struct cras_iodev *a,
						   const struct cras_iodev *b)
{
	if (!a->info.plugged)
		return 0;
	if (!b->info.plugged)
		return 1;
	return (a->info.plugged_time.tv_sec > b->info.plugged_time.tv_sec ||
		(a->info.plugged_time.tv_sec == b->info.plugged_time.tv_sec &&
		 a->info.plugged_time.tv_usec > b->info.plugged_time.tv_usec));
}

/* Gets a count of how many frames until the next time the thread should wake
 * up to service the buffer.
 * Args:
 *    dev - device to calculate sleep frames for.
 *    curr_level - current buffer level.
 * Returns:
 *    A positive number of frames to wait until waking up.
 */
static inline unsigned int cras_iodev_sleep_frames(const struct cras_iodev *dev,
						   unsigned int curr_level)
{
	int to_sleep;

	if (dev->direction == CRAS_STREAM_OUTPUT)
		to_sleep = curr_level - dev->cb_threshold;
	else
		to_sleep = dev->cb_threshold - curr_level;

	if (to_sleep < 0)
		return 0;

	return to_sleep;
}

#endif /* CRAS_IODEV_H_ */
