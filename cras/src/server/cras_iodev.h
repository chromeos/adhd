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
struct cras_iodev;

/* Holds an output/input node for this device.  An ionode is a control that
 * can be switched on and off such as headphones or speakers.
 * Members:
 *    dev - iodev which this node belongs to.
 *    idx - ionode index.
 *    plugged - true if the device is plugged.
 *    plugged_time - If plugged is true, this is the time it was attached.
 *    priority - higher is better.
 *    volume - per-node volume (0-100)
 *    capture_gain - per-node capture gain/attenuation (in 100*dBFS)
 *    type - Type displayed to the user.
 *    name - Name displayed to the user.
 */
struct cras_ionode {
	struct cras_iodev *dev;
	uint32_t idx;
	int plugged;
	struct timeval plugged_time;
	unsigned priority;
	unsigned int volume;
	long capture_gain;
	enum CRAS_NODE_TYPE type;
	char name[CRAS_NODE_NAME_BUFFER_SIZE];
	struct cras_ionode *prev, *next;
};

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
 * dev_running - Checks if the device is playing or recording, return 1 if it's
 *     running, return 0 if not.
 * update_active_node - Update the active node using the selected/plugged state.
 * update_channel_layout - Update the channel layout base on set iodev->format,
 *     expect the best available layout be filled to iodev->format.
 * format - The audio format being rendered or captured.
 * info - Unique identifier for this device (index and name).
 * nodes - The output or input nodes available for this device.
 * active_node - The current node being used for playback or capture.
 * direction - Input or Output.
 * supported_rates - Array of sample rates supported by device 0-terminated.
 * supported_channel_counts - List of number of channels supported by device.
 * buffer_size - Size of the audio buffer in frames.
 * used_size - Number of frames that are used for audio.
 * cb_threshold - Level below which to call back to the client (in frames).
 * min_buffer_level - Extra frames to keep queued in addition to requested.
 * dsp_context - The context used for dsp processing on the audio data.
 * dsp_name - The "dsp_name" dsp variable specified in the ucm config.
 * thread - The audio thread using this device, NULL if none.
 * software_volume_needed - True if volume control is not supported by hardware.
 * software_volume_scaler - The scaler used for software volume mixing. Should
 *     be 1.0 by default.
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
	void (*update_active_node)(struct cras_iodev *iodev);
	int (*update_channel_layout)(struct cras_iodev *iodev);
	struct cras_audio_format *format;
	struct cras_iodev_info info;
	struct cras_ionode *nodes;
	struct cras_ionode *active_node;
	enum CRAS_STREAM_DIRECTION direction;
	size_t *supported_rates;
	size_t *supported_channel_counts;
	snd_pcm_uframes_t buffer_size;
	snd_pcm_uframes_t used_size;
	snd_pcm_uframes_t cb_threshold;
	unsigned int min_buffer_level;
	struct cras_dsp_context *dsp_context;
	const char *dsp_name;
	struct audio_thread *thread;
	int software_volume_needed;
	struct cras_iodev *prev, *next;
	float software_volume_scaler;
};

/*
 * Utility functions to be used by iodev implementations.
 */

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

/* Clear the format previously set for this iodev.
 *
 * Args:
 *    iodev - the iodev you want to free the format.
 */
void cras_iodev_free_format(struct cras_iodev *iodev);

/* Free the dsp context for this iodev
 *
 * Args:
 *    iodev - the iodev you want to free the dsp context.
 */
void cras_iodev_free_dsp(struct cras_iodev *iodev);

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
				       struct cras_timespec *ts);

/* Sets the time that the first sample in the buffer was captured at the ADC.
 * Args:
 *    frame_rate - in Hz.
 *    frames - Delay specified in frames.
 *    ts - Filled with the time that the next sample was captured.
 */
void cras_iodev_set_capture_timestamp(size_t frame_rate,
				      size_t frames,
				      struct cras_timespec *ts);

/* Configures when to wake up, the minimum amount free before refilling, and
 * other params that are independent of the hw configuration.
 * Args:
 *    buffer_size - Size of the audio buffer in frames.
 *    cb_threshold - Level below which to call back to the client (in frames).
 */
void cras_iodev_config_params(struct cras_iodev *iodev,
			      unsigned int buffer_size,
			      unsigned int cb_threshold);

/* Update the "dsp_name" dsp variable. This may cause the dsp pipeline to be
 * reloaded.
 * Args:
 *    iodev - device which the state changes.
 */
void cras_iodev_update_dsp(struct cras_iodev *iodev);

/* Handles a plug event happening on this node.
 * Args:
 *    node - ionode on which a plug event was detected.
 *    plugged - true if the device was plugged, false for unplugged.
 */
void cras_ionode_plug_event(struct cras_ionode *node, int plugged);

/* Returns true if node a is preferred over node b. */
int cras_ionode_better(struct cras_ionode *a, struct cras_ionode *b);

/* Sets an attribute of an ionode on a device.
 * Args:
 *    ionode - ionode whose attribute we want to change.
 *    attr - the attribute we want to change.
 *    value - the value we want to set.
 */
int cras_iodev_set_node_attr(struct cras_ionode *ionode,
			     enum ionode_attr attr, int value);

/* Find the node with highest priority that is plugged in. */
struct cras_ionode *cras_iodev_get_best_node(const struct cras_iodev *iodev);

/* Adds a node to the iodev's node list. */
void cras_iodev_add_node(struct cras_iodev *iodev, struct cras_ionode *node);

/* Removes a node from iodev's node list. */
void cras_iodev_rm_node(struct cras_iodev *iodev, struct cras_ionode *node);

/* Assign a node to be the active node of the device */
void cras_iodev_set_active_node(struct cras_iodev *iodev,
				struct cras_ionode *node);

/* Sets the software volume scaler of the iodev. */
void cras_iodev_set_software_volume(struct cras_iodev *iodev,
				    float volume_scaler);

/* Gets a count of how many frames until the next time the thread should wake
 * up to service the buffer.
 * Args:
 *    dev - device to calculate sleep frames for.
 *    sleep_level - level when device should be polled.
 *    curr_level - current buffer level.
 * Returns:
 *    A positive number of frames to wait until waking up.
 */
static inline unsigned int cras_iodev_sleep_frames(const struct cras_iodev *dev,
						   unsigned int sleep_level,
						   unsigned int curr_level)
{
	int to_sleep;

	if (dev->direction == CRAS_STREAM_OUTPUT)
		to_sleep = curr_level - sleep_level;
	else
		to_sleep = sleep_level - curr_level;

	if (to_sleep < 0)
		return 0;

	return to_sleep;
}

/* Adjust the system volume based on the volume of the given node. */
static inline unsigned int cras_iodev_adjust_node_volume(
		const struct cras_ionode *node,
		unsigned int system_volume)
{
	unsigned int node_vol_offset = 100 - node->volume;

	if (system_volume > node_vol_offset)
		return system_volume - node_vol_offset;
	else
		return 0;
}

/* Get the volume scaler for the active node. */
static inline unsigned int cras_iodev_adjust_active_node_volume(
		struct cras_iodev *iodev, unsigned int system_volume)
{
	if (!iodev->active_node)
		return system_volume;

	return cras_iodev_adjust_node_volume(iodev->active_node, system_volume);
}

/* Returns true if the active node of the iodev needs software volume. */
static inline int cras_iodev_software_volume_needed(
		const struct cras_iodev *iodev)
{
	if (iodev->software_volume_needed)
		return 1;

	if (!iodev->active_node)
		return 0;

	return iodev->active_node->type == CRAS_NODE_TYPE_HDMI;
}

#endif /* CRAS_IODEV_H_ */
