/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * cras_iodev represents playback or capture devices on the system.  Each iodev
 * will spawn a thread it will use to render or capture audio.  For playback,
 * this thread will gather audio from the streams that are attached to the
 * device and render the samples it gets to the hardware device.  For capture
 * the process is reversed, the samples are pulled from hardware and passed on
 * to the attached streams.
 */
#ifndef CRAS_IODEV_H_
#define CRAS_IODEV_H_

#include "cras_iodev_info.h"
#include "cras_messages.h"

struct cras_rstream;
struct cras_audio_format;

/* Linked list of streams of audio from/to a client. */
struct cras_io_stream {
	struct cras_rstream *stream;
	int fd; /* cached here to to frequent access */
	struct cras_audio_shm_area *shm; /* ditto on caching */
	int mixed; /* Was this stream mixed already? */
	struct cras_io_stream *prev, *next;
};

/* An input or output device, that can have audio routed to/from it.
 * add_stream - Function to call when adding a stream.
 * rm_stream - Function to call when removing a stream.
 * format - The audio format being rendered or captured.
 * info - Unique identifier for this device (index and name).
 * streams - List of streams attached to device.
 * direction - Input or Output.
 * supported_rates - Array of sample rates supported by device 0-terminated.
 * supported_channel_counts - List of number of channels supported by device.
 * buffer_size - Size of the ALSA buffer in frames.
 * used_size - Number of frames that are used for audio.
 * cb_threshold - Level below which to call back to the client (in frames).
 * to_thread_fds - Send a message from main to running thread.
 * to_main_fds - Send a message to main from running thread.
 * tid - Thread ID of the running playback/capture thread.
 */
struct cras_iodev {
	int (*add_stream)(struct cras_iodev *iodev,
			  struct cras_rstream *stream);
	int (*rm_stream)(struct cras_iodev *iodev,
			 struct cras_rstream *stream);
	struct cras_audio_format *format;
	struct cras_io_stream *streams;
	struct cras_iodev_info info;
	enum CRAS_STREAM_DIRECTION direction;
	size_t *supported_rates;
	size_t *supported_channel_counts;
	snd_pcm_uframes_t buffer_size;
	snd_pcm_uframes_t used_size;
	snd_pcm_uframes_t cb_threshold;
	int to_thread_fds[2];
	int to_main_fds[2];
	pthread_t tid;
	struct cras_iodev *prev, *next;
};

/* Define messages that can be sent from the main context to the device's
 * playback thread. */
enum CRAS_IODEV_COMMAND {
	CRAS_IODEV_ADD_STREAM,
	CRAS_IODEV_RM_STREAM,
	CRAS_IODEV_STOP,
};

struct cras_iodev_msg {
	size_t length;
	enum CRAS_IODEV_COMMAND id;
};

struct cras_iodev_add_rm_stream_msg {
	struct cras_iodev_msg header;
	struct cras_rstream *stream;
};

/*
 * Utility functions to be used by iodev implementations.
 */

/* Initializes the cras_iodev structure.
 * Args:
 *    iodev - The device to initialize.
 *    direction - input or output.
 *    thread_function - The function to run for playback/capture threads.
 *    thread_arg - Passed to thread_function when it is run.
 * Returns:
 *    0 on success or negative error on failure.
 */
int cras_iodev_init(struct cras_iodev *iodev,
		    enum CRAS_STREAM_DIRECTION direction,
		    void *(*thread_function)(void *arg),
		    void *thread_data);

/* Un-initializes a cras_iodev structure that was setup by cras_iodev_init().
 * Args:
 *    iodev - The device to initialize.
 */
void cras_iodev_deinit(struct cras_iodev *iodev);

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

/* Checks if there are any streams playing on this device.
 * Args:
 *    iodev - iodev to check for active streams.
 * Returns:
 *    non-zero if the device has streams attached.
 */
static inline int cras_iodev_streams_attached(const struct cras_iodev *iodev)
{
	return iodev->streams != NULL;
}

/* Write a message to the playback thread and wait for an ack, This keeps these
 * operations synchronous for the main server thread.  For instance when the
 * RM_STREAM message is sent, the stream can be deleted after the function
 * returns.  Making this synchronous also allows the thread to return an error
 * code that can be handled by the caller.
 * Args:
 *    iodev - iodev to check for active streams.
 *    msg - The message to send.
 * Returns:
 *    A return code from the message handler in the thread.
 */
int cras_iodev_post_message_to_playback_thread(struct cras_iodev *iodev,
					       struct cras_iodev_msg *msg);

/* Fill timespec ts with the time to sleep based on the number of frames and
 * frame rate.  Threshold is how many frames should be left when the timer
 * expires.
 * Args:
 *    frames - Number of frames in buffer..
 *    cb_threshold - Number of frames that should be left when time expires.
 *    frame_rate - 44100, 48000, etc.
 *    ts - Filled with the time to sleep for.
 */
void cras_iodev_fill_time_from_frames(size_t frames,
				      size_t cb_threshold,
				      size_t frame_rate,
				      struct timespec *ts);

/* Sends a response (error code) from the playback/capture thread to the main
 * thread.  Indicates that the last message sent to the playback/capture thread
 * has been handled with an error code of (rc).
 * Args:
 *    iodev - iodev to check for active streams.
 *    rc - Result code to send back to the main thread.
 * Returns:
 *    The number of bytes written to the main thread.
 */
int cras_iodev_send_command_response(struct cras_iodev *iodev, int rc);

/* Reads a command from the main thread.  Called from the playback/capture
 * thread.  This will read the next available command from the main thread and
 * put it in buf.
 * Args:
 *    iodev - iodev to check for active streams.
 *    buf - Message is stored here on return.
 *    max_len - maximum length of message to put into buf.
 * Returns:
 *    0 on success, negative error code on failure.
 */
int cras_iodev_read_thread_command(struct cras_iodev *iodev,
				   uint8_t *buf,
				   size_t max_len);

/* Returns the fd to pass to select when waiting for a new message from the main
 * thread.  Called from the playback/capture thread.
 * Args:
 *    iodev - iodev to check for active streams.
 * Returns:
 *    The file descriptor to poll.
 */
int cras_iodev_get_thread_poll_fd(const struct cras_iodev *iodev);

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
 * other params that are independent of alsa configuration. */
void cras_iodev_config_params_for_streams(struct cras_iodev *iodev);

#endif /* CRAS_IODEV_H_ */
