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

struct buffer_share;
struct cras_rstream;
struct cras_audio_area;
struct cras_audio_format;
struct audio_thread;
struct cras_iodev;
struct rate_estimator;

/* Holds an output/input node for this device.  An ionode is a control that
 * can be switched on and off such as headphones or speakers.
 * Members:
 *    dev - iodev which this node belongs to.
 *    idx - ionode index.
 *    plugged - true if the device is plugged.
 *    plugged_time - If plugged is true, this is the time it was attached.
 *    volume - per-node volume (0-100)
 *    capture_gain - per-node capture gain/attenuation (in 100*dBFS)
 *    left_right_swapped - If left and right output channels are swapped.
 *    type - Type displayed to the user.
 *    name - Name displayed to the user.
 *    softvol_scalers - pointer to software volume scalers.
 */
struct cras_ionode {
	struct cras_iodev *dev;
	uint32_t idx;
	int plugged;
	struct timeval plugged_time;
	unsigned int volume;
	long capture_gain;
	int left_right_swapped;
	enum CRAS_NODE_TYPE type;
	char name[CRAS_NODE_NAME_BUFFER_SIZE];
	float *softvol_scalers;
	struct cras_ionode *prev, *next;
};

/* An input or output device, that can have audio routed to/from it.
 * set_volume - Function to call if the system volume changes.
 * set_mute - Function to call if the system mute state changes.
 * set_capture_gain - Function to call if the system capture_gain changes.
 * set_capture_mute - Function to call if the system capture mute state changes.
 * set_swap_mode_for_node - Function to call to set swap mode for the node.
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
 * format - The audio format being rendered or captured to hardware.
 * ext_format - The audio format that is visible to the rest of the system.
 *     This can be different than the hardware if the device dsp changes it.
 * rate_est - Rate estimator to estimate the actual device rate.
 * area - Information about how the samples are stored.
 * info - Unique identifier for this device (index and name).
 * nodes - The output or input nodes available for this device.
 * active_node - The current node being used for playback or capture.
 * direction - Input or Output.
 * supported_rates - Array of sample rates supported by device 0-terminated.
 * supported_channel_counts - List of number of channels supported by device.
 * buffer_size - Size of the audio buffer in frames.
 * min_buffer_level - Extra frames to keep queued in addition to requested.
 * dsp_context - The context used for dsp processing on the audio data.
 * dsp_name - The "dsp_name" dsp variable specified in the ucm config.
 * is_active - True if this iodev is set as active, false otherwise.
 * software_volume_needed - True if volume control is not supported by hardware.
 * is_draining - Indicate the device is playing the remaining audio in the
 *     hardware buffer.
 * extra_silent_frames - In draining, the number of silent frames filled in to
 *     prevent buffer underrun.
 * streams - List of audio streams serviced by dev.
 * min_cb_level - min callback level of any stream attached.
 * max_cb_level - max callback level of any stream attached.
 * buf_state - If multiple streams are writing to this device, then this
 *     keeps track of how much each stream has written.
 */
struct cras_iodev {
	void (*set_volume)(struct cras_iodev *iodev);
	void (*set_mute)(struct cras_iodev *iodev);
	void (*set_capture_gain)(struct cras_iodev *iodev);
	void (*set_capture_mute)(struct cras_iodev *iodev);
	int (*set_swap_mode_for_node)(struct cras_iodev *iodev,
				      struct cras_ionode *node,
				      int enable);
	int (*open_dev)(struct cras_iodev *iodev);
	int (*close_dev)(struct cras_iodev *iodev);
	int (*is_open)(const struct cras_iodev *iodev);
	int (*update_supported_formats)(struct cras_iodev *iodev);
	void (*set_as_default)(struct cras_iodev *iodev);
	int (*frames_queued)(const struct cras_iodev *iodev);
	int (*delay_frames)(const struct cras_iodev *iodev);
	int (*get_buffer)(struct cras_iodev *iodev,
			  struct cras_audio_area **area,
			  unsigned *frames);
	int (*put_buffer)(struct cras_iodev *iodev, unsigned nwritten);
	int (*dev_running)(const struct cras_iodev *iodev);
	void (*update_active_node)(struct cras_iodev *iodev);
	int (*update_channel_layout)(struct cras_iodev *iodev);
	struct cras_audio_format *format;
	struct cras_audio_format *ext_format;
	struct rate_estimator *rate_est;
	struct cras_audio_area *area;
	struct cras_iodev_info info;
	struct cras_ionode *nodes;
	struct cras_ionode *active_node;
	enum CRAS_STREAM_DIRECTION direction;
	size_t *supported_rates;
	size_t *supported_channel_counts;
	snd_pcm_uframes_t buffer_size;
	unsigned int min_buffer_level;
	struct cras_dsp_context *dsp_context;
	const char *dsp_name;
	int is_active;
	int software_volume_needed;
	int is_draining;
	size_t extra_silent_frames;
	struct dev_stream *streams;
	unsigned int min_cb_level;
	unsigned int max_cb_level;
	struct buffer_share *buf_state;
	struct cras_iodev *prev, *next;
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

/* Initializes the audio area for this iodev.
 * Args:
 *    iodev - the iodev to init audio area
 *    num_channels - the total number of channels
 */
void cras_iodev_init_audio_area(struct cras_iodev *iodev,
				int num_channels);

/* Frees the audio area for this iodev.
 * Args:
 *    iodev - the iodev to free audio area
 */
void cras_iodev_free_audio_area(struct cras_iodev *iodev);

/* Free resources allocated for this iodev.
 *
 * Args:
 *    iodev - the iodev you want to free the resources for.
 */
void cras_iodev_free_resources(struct cras_iodev *iodev);

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

/* Adds a node to the iodev's node list. */
void cras_iodev_add_node(struct cras_iodev *iodev, struct cras_ionode *node);

/* Removes a node from iodev's node list. */
void cras_iodev_rm_node(struct cras_iodev *iodev, struct cras_ionode *node);

/* Assign a node to be the active node of the device */
void cras_iodev_set_active_node(struct cras_iodev *iodev,
				struct cras_ionode *node);

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

	return iodev->active_node->type == CRAS_NODE_TYPE_HDMI ||
	       iodev->active_node->type == CRAS_NODE_TYPE_USB;
}

/* Gets the software volume scaler of the iodev. The scaler should only be
 * applied if the device needs software volume. */
float cras_iodev_get_software_volume_scaler(struct cras_iodev *iodev);

/* Indicate that a stream has been added from the device. */
int cras_iodev_add_stream(struct cras_iodev *iodev,
			  struct dev_stream *stream);

/* Indicate that a stream has been removed from the device. */
int cras_iodev_rm_stream(struct cras_iodev *iodev,
			 const struct dev_stream *stream);

/* Get the offset of this stream into the dev's buffer. */
unsigned int cras_iodev_stream_offset(struct cras_iodev *iodev,
				      struct dev_stream *stream);

/* Get the maximum offset of any stream into the dev's buffer. */
unsigned int cras_iodev_max_stream_offset(const struct cras_iodev *iodev);

/* Tell the device how many frames the given stream wrote. */
void cras_iodev_stream_written(struct cras_iodev *iodev,
			       struct dev_stream *stream,
			       unsigned int nwritten);

/* All streams have written what they can, update the write pointers and return
 * the amount that has been filled by all streams and can be comitted to the
 * device.
 */
unsigned int cras_iodev_all_streams_written(struct cras_iodev *iodev);

/* Open an iodev, does setup and invokes the open_dev callback. */
int cras_iodev_open(struct cras_iodev *iodev);

/* Open an iodev, does teardown and invokes the close_dev callback. */
int cras_iodev_close(struct cras_iodev *iodev);

/* Marks a buffer from get_buffer as read. */
int cras_iodev_put_input_buffer(struct cras_iodev *iodev, unsigned int nframes);

/* Marks a buffer from get_buffer as written. */
int cras_iodev_put_output_buffer(struct cras_iodev *iodev, uint8_t *frames,
				 unsigned int nframes);

/* Returns a buffer to read from.
 * Args:
 *    iodev - The device.
 *    area - Filled with a pointer to the audio to read/write.
 *    frames - Filled with the number of frames that can be read/written.
 */
int cras_iodev_get_input_buffer(struct cras_iodev *iodev,
				struct cras_audio_area **area,
				unsigned *frames);

/* Returns a buffer to read from.
 * Args:
 *    iodev - The device.
 *    area - Filled with a pointer to the audio to read/write.
 *    frames - Filled with the number of frames that can be read/written.
 */
int cras_iodev_get_output_buffer(struct cras_iodev *iodev,
				 struct cras_audio_area **area,
				 unsigned *frames);

/* Update the estimated sample rate of the device. */
int cras_iodev_update_rate(struct cras_iodev *iodev, unsigned int level);

/* Resets the rate estimator of the device. */
int cras_iodev_reset_rate_estimator(const struct cras_iodev *iodev);

/* Returns the rate of estimated frame rate and the claimed frame rate of
 * the device. */
double cras_iodev_get_est_rate_ratio(const struct cras_iodev *iodev);

/* Get the delay from DSP processing in frames. */
int cras_iodev_get_dsp_delay(const struct cras_iodev *iodev);

/* Get the delay for input/output in frames. */
static inline int cras_iodev_delay_frames(const struct cras_iodev *iodev)
{
	return iodev->delay_frames(iodev) + cras_iodev_get_dsp_delay(iodev);
}
#endif /* CRAS_IODEV_H_ */
