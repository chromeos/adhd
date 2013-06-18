/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef AUDIO_THREAD_H_
#define AUDIO_THREAD_H_

#include <pthread.h>
#include <stdint.h>

struct cras_iodev;

/* Errors that can be returned from add_stream. */
enum error_type_from_audio_thread_h {
	AUDIO_THREAD_ERROR_OTHER = -1,
	AUDIO_THREAD_OUTPUT_DEV_ERROR = -2,
	AUDIO_THREAD_INPUT_DEV_ERROR = -3,
};

/* Linked list of streams of audio from/to a client. */
struct cras_io_stream {
	struct cras_rstream *stream;
	int fd; /* cached here due to frequent access */
	int mixed; /* Was this stream mixed already? */
	struct cras_io_stream *prev, *next;
};

/* Hold communication pipes and pthread info for a thread used to play or record
 * audio.  This maps 1 to 1 with IO devices.
 *    odev - The output device to attach this thread to, NULL if none.
 *    idev - The input device to attach this thread to, NULL if none.
 *    to_thread_fds - Send a message from main to running thread.
 *    to_main_fds - Send a message to main from running thread.
 *    tid - Thread ID of the running playback/capture thread.
 *    started - Non-zero if the thread has started successfully.
 *    sleep_correction_frames - Number of frames to adjust sleep time by.  This
 *      is adjusted based on sleeping too long or short so that the sleep
 *      interval tracks toward the targeted number of frames.
 *    streams - List of audio streams serviced by this thread.
 */
struct audio_thread {
	struct cras_iodev *output_dev;
	struct cras_iodev *input_dev;
	int to_thread_fds[2];
	int to_main_fds[2];
	pthread_t tid;
	int started;
	int sleep_correction_frames;
	struct cras_io_stream *streams;
};

/* Messages that can be sent from the main context to the audio thread. */
enum AUDIO_THREAD_COMMAND {
	AUDIO_THREAD_ADD_STREAM,
	AUDIO_THREAD_RM_STREAM,
	AUDIO_THREAD_RM_ALL_STREAMS,
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
struct audio_thread *audio_thread_create();

/* Sets the device to be used for output.
 * Args:
 *    thread - The thread to add the device to.
 *    odev - The output device to use.
 */
void audio_thread_set_output_dev(struct audio_thread *thread,
				 struct cras_iodev *odev);

/* Sets the device to be used for input.
 * Args:
 *    thread - The thread to add the device to.
 *    idev - The input device to use.
 */
void audio_thread_set_input_dev(struct audio_thread *thread,
				struct cras_iodev *idev);

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
 *    zero on success, negative error from the AUDIO_THREAD enum above when an
 *    the thread can't be added.
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

/* Remove all streams from a thread.  Used when streams should be re-attached
 * after a device switch.
 * Args:
 *    thread - a pointer to the audio thread.
 */
void audio_thread_remove_streams(struct audio_thread *thread);

#endif /* AUDIO_THREAD_H_ */
