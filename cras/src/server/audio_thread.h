/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_AUDIO_THREAD_H_
#define CRAS_SRC_SERVER_AUDIO_THREAD_H_

#include <pthread.h>
#include <stdint.h>

#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/dev_io.h"
#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct buffer_share;
struct cras_fmt_conv;
struct cras_iodev;
struct cras_rstream;
struct dev_stream;

/* Hold communication pipes and pthread info for the thread used to play or
 * record audio.
 */
struct audio_thread {
  // Send a message from main to running thread.
  int to_thread_fds[2];
  // Send a synchronous response to main from running thread.
  int to_main_fds[2];
  // Thread ID of the running playback/capture thread.
  pthread_t tid;
  // Non-zero if the thread has started successfully.
  int started;
  // Non-zero if the thread is suspended.
  int suspended;
  // Lists of open input and output devices.
  struct open_dev* open_devs[CRAS_NUM_DIRECTIONS];
  // What FDs wake up this thread.
  struct pollfd* pollfds;
  // Number of available poll fds.
  size_t pollfds_size;
  // Number of currently registered poll fds.
  size_t num_pollfds;
  // Format converter used to remix output channels.
  struct cras_fmt_conv* remix_converter;
};

/*
 * Enum to specify how a registered event callback be triggered.
 */
enum AUDIO_THREAD_EVENTS_CB_TRIGGER {
  // Callback will not be triggered.
  TRIGGER_NONE,
  // Triggered by poll given fd and revent.
  TRIGGER_POLL,
  // Triggered every time when audio thread wakes up.
  TRIGGER_WAKEUP,
};

/* Callback function to be handled in main loop in audio thread.
 * Args:
 *    data - The data for callback function.
 *    revent - The returned event from ppoll().
 */
typedef int (*thread_callback)(void* data, int revent);

/* Creates an audio thread.
 * Returns:
 *    A pointer to the newly created audio thread.  It must be freed by calling
 *    audio_thread_destroy().  Returns NULL on error.
 */
struct audio_thread* audio_thread_create();

/* Adds an open device.
 * Args:
 *    thread - The thread to add open device to.
 *    dev - The open device to add.
 */
int audio_thread_add_open_dev(struct audio_thread* thread,
                              struct cras_iodev* dev);

/* Removes an open device.
 * Args:
 *    thread - The thread to remove open device from.
 *    dev - The open device to remove.
 */
int audio_thread_rm_open_dev(struct audio_thread* thread,
                             enum CRAS_STREAM_DIRECTION dir,
                             unsigned int dev_idx);

/* Checks if dev is open and used by audio thread.
 * Args:
 *    thread - The thread accessing open devs.
 *    dev - The device to check if it has already been open.
 */
int audio_thread_is_dev_open(struct audio_thread* thread,
                             struct cras_iodev* dev);

/* Adds a thread_callback to audio thread for requested events. By default
 * the callback trigger is set to TRIGGER_POLL.
 * Args:
 *    fd - The file descriptor to be polled for the callback.
 *      The callback will be called when any of requested events matched.
 *    cb - The callback function.
 *    data - The data for the callback function.
 *    events - The requested events to ppoll().
 */
void audio_thread_add_events_callback(int fd,
                                      thread_callback cb,
                                      void* data,
                                      int events);

/* Removes an thread_callback from audio thread.
 * Args:
 *    fd - The file descriptor of the previous added callback.
 */
void audio_thread_rm_callback(int fd);

/* Removes a thread_callback from main thread.
 * Args:
 *     thread - The thread to remove callback from.
 *     fd - The file descriptor of the previous added callback.
 */
int audio_thread_rm_callback_sync(struct audio_thread* thread, int fd);

/* Configures the callback associated with fd when it should be triggered.
 * Args:
 *    fd - The file descriptor associate to the callback.
 *    trigger - Specifies how the callback should be triggered.
 */
void audio_thread_config_events_callback(
    int fd,
    enum AUDIO_THREAD_EVENTS_CB_TRIGGER trigger);

/* Starts a thread created with audio_thread_create.
 * Args:
 *    thread - The thread to start.
 * Returns:
 *    0 on success, return code from pthread_crate on failure.
 */
int audio_thread_start(struct audio_thread* thread);

// Frees an audio thread created with audio_thread_create().
void audio_thread_destroy(struct audio_thread* thread);

// Returns the shm fd for the ATlog.
int audio_thread_event_log_shm_fd();

/* Add streams to the thread. After this call, the ownership of the streams will
 * be passed to the audio thread. Audio thread is responsible to release the
 * streams' resources.
 * Args:
 *    thread - a pointer to the audio thread.
 *    streams - an array of streams to add.
 *    num_streams - nunber of streams in the array `streams`.
 *    devs - an array of devices to attach stream.
 *    num_devs - number of devices in the array pointed by devs.
 * Returns:
 *    zero on success, negative error from the AUDIO_THREAD enum above when an
 *    the thread can't be added.
 */
int audio_thread_add_streams(struct audio_thread* thread,
                             struct cras_rstream** streams,
                             unsigned int num_streams,
                             struct cras_iodev** devs,
                             unsigned int num_devs);

/* Begin draining a stream and check the draining status.
 * Args:
 *    thread - a pointer to the audio thread.
 *    stream - the stream to drain/remove.
 * Returns:
 *    zero if the stream is drained and can be deleted.  If the stream is not
 *    completely drained, then the number of milliseconds until is is drained
 *    are returned.
 */
int audio_thread_drain_stream(struct audio_thread* thread,
                              struct cras_rstream* stream);

/* Disconnect a stream from the client.
 * Args:
 *    thread - a pointer to the audio thread.
 *    stream - the stream to be disconnected.
 *    iodev - the device to disconnect from.
 * Returns:
 *    0 on success, negative if error.
 */
int audio_thread_disconnect_stream(struct audio_thread* thread,
                                   struct cras_rstream* stream,
                                   struct cras_iodev* iodev);

// Dumps information about all active streams to syslog.
int audio_thread_dump_thread_info(struct audio_thread* thread,
                                  struct audio_debug_info* info);

/* Starts or stops the aec dump task.
 * Args:
 *    thread - pointer to the audio thread.
 *    stream_id - id of the target stream for aec dump.
 *    start - True to start the aec dump, false to stop.
 *    fd - File to store aec dump result.
 */
int audio_thread_set_aec_dump(struct audio_thread* thread,
                              cras_stream_id_t stream_id,
                              unsigned int start,
                              int fd);

/* Configures the global converter for output remixing. Called by main
 * thread. */
int audio_thread_config_global_remix(struct audio_thread* thread,
                                     unsigned int num_channels,
                                     const float* coefficient);

/* Start ramping on a device.
 *
 * Ramping is started/updated in audio thread. This function lets the main
 * thread request that the audio thread start ramping.
 *
 * Args:
 *   thread - a pointer to the audio thread.
 *   dev_idx - Index of the the device to start ramping.
 *   request - Check the docstrings of CRAS_IODEV_RAMP_REQUEST.
 * Returns:
 *    0 on success, negative if error.
 */
int audio_thread_dev_start_ramp(struct audio_thread* thread,
                                unsigned int dev_idx,
                                enum CRAS_IODEV_RAMP_REQUEST request);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_AUDIO_THREAD_H_
