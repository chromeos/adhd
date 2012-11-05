/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef AUDIO_THREAD_H_
#define AUDIO_THREAD_H_

#include <pthread.h>
#include <stdint.h>

struct cras_iodev;

/* Linked list of streams of audio from/to a client. */
struct cras_io_stream {
	struct cras_rstream *stream;
	int fd; /* cached here due to frequent access */
	struct cras_audio_shm *shm; /* ditto on caching */
	int mixed; /* Was this stream mixed already? */
	struct cras_io_stream *prev, *next;
};

/* Hold communication pipes and pthread info for a thread used to play or record
 * audio.  This maps 1 to 1 with IO devices.
 *    iodev - The iodev to attach this thread to.
 *    to_thread_fds - Send a message from main to running thread.
 *    to_main_fds - Send a message to main from running thread.
 *    tid - Thread ID of the running playback/capture thread.
 *    started - Non-zero if the thread has started successfully.
 *    sleep_correction_frames - Number of frames to adjust sleep time by.  This
 *      is adjusted based on sleeping too long or short so that the sleep
 *      interval tracks toward the targeted number of frames.
 *    audio_cb - Callback to fill or read samples (depends on direction).
 *      ts will be filled with the time the system can sleep before again
 *      servicing the callback.
 *    streams - List of audio streams serviced by this thread.
 */
struct audio_thread {
	struct cras_iodev *iodev;
	int to_thread_fds[2];
	int to_main_fds[2];
	pthread_t tid;
	int started;
	int sleep_correction_frames;
	int (*audio_cb)(struct audio_thread *thread, struct timespec *ts);
	struct cras_io_stream *streams;
};

/* Messages that can be sent from the main context to the audio thread. */
enum AUDIO_THREAD_COMMAND {
	AUDIO_THREAD_ADD_STREAM,
	AUDIO_THREAD_RM_STREAM,
	AUDIO_THREAD_STOP,
};

struct audio_thread_msg {
	size_t length;
	enum AUDIO_THREAD_COMMAND id;
};

struct audio_thread_add_rm_stream_msg {
	struct audio_thread_msg header;
	struct cras_rstream *stream;
};

/* Creates an audio thread.
 * Args:
 *    iodev - The iodev to attach this thread to.
 * Returns:
 *    A pointer to the newly create audio thread.  It has been allocated from
 *    the heap and must be freed by calling audio_thread_destroy().  NULL on
 *    error.
 */
struct audio_thread *audio_thread_create(struct cras_iodev *iodev);

/* Starts a thread created with audio_thread_create.
 * Args:
 *    thread - The thread to start.
 * Returns:
 *    0 on success, return code from pthread_crate on failure.
 */
int audio_thread_start(struct audio_thread *thread);

/* Frees an audio thread created with audio_thread_create(). */
void audio_thread_destroy(struct audio_thread *thread);

/* Add a stream to the thread.
 * Args:
 *    thread - a pointer to the audio thread.
 *    stream - the new stream to add.
 * Returns:
 *    zero on success, negative error otherwise.
 */
int audio_thread_add_stream(struct audio_thread *thread,
			    struct cras_rstream *stream);

/* Remove a stream from the thread.
 * Args:
 *    thread - a pointer to the audio thread.
 *    stream - the new stream to remove.
 * Returns:
 *    The number of streams remaining if successful, negative if error.
 */
int audio_thread_rm_stream(struct audio_thread *thread,
			   struct cras_rstream *stream);

/* Remove all streams from the thread.
 * Args:
 *    thread - a pointer to the audio thread.
 */
void audio_thread_rm_all_streams(struct audio_thread *thread);

/* Write a message to the playback thread and wait for an ack, This keeps these
 * operations synchronous for the main server thread.  For instance when the
 * RM_STREAM message is sent, the stream can be deleted after the function
 * returns.  Making this synchronous also allows the thread to return an error
 * code that can be handled by the caller.
 * Args:
 *    thread - thread to receive message.
 *    msg - The message to send.
 * Returns:
 *    A return code from the message handler in the thread.
 */
int audio_thread_post_message(struct audio_thread *thread,
			      struct audio_thread_msg *msg);

/* Sends a response (error code) from the audio thread to the main thread.
 * Indicates that the last message sent to the audio thread has been handled
 * with an error code of rc.
 * Args:
 *    thread - thread responding to command.
 *    rc - Result code to send back to the main thread.
 * Returns:
 *    The number of bytes written to the main thread.
 */
int audio_thread_send_response(struct audio_thread *thread, int rc);

/* Reads a command from the main thread.  Called from the playback/capture
 * thread.  This will read the next available command from the main thread and
 * put it in buf.
 * Args:
 *    thread - thread reading the command.
 *    buf - Message is stored here on return.
 *    max_len - maximum length of message to put into buf.
 * Returns:
 *    0 on success, negative error code on failure.
 */
int audio_thread_read_command(struct audio_thread *thread,
			      uint8_t *buf,
			      size_t max_len);

/* For playback, fill the audio buffer when needed, for capture, pull out
 * samples when they are ready.
 * This thread will attempt to run at a high priority to allow for low latency
 * streams.  This thread sleeps while the device plays back or captures audio,
 * it will wake up as little as it can while avoiding xruns.  It can also be
 * woken by sending it a message using the "audio_thread_post_message" function.
 */
void *audio_io_thread(void *arg);

#endif /* AUDIO_THREAD_H_ */
