/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <inttypes.h>
#include <string.h>
#include <syslog.h>

#include <webrtc-apm/webrtc_apm.h>

#include "audio_thread.h"
#include "byte_buffer.h"
#include "cras_stream_apm.h"
#include "cras_apm_reverse.h"
#include "cras_audio_area.h"
#include "cras_audio_format.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "dsp_util.h"
#include "dumper.h"
#include "float_buffer.h"
#include "iniparser_wrapper.h"
#include "utlist.h"

#define AEC_CONFIG_NAME "aec.ini"
#define APM_CONFIG_NAME "apm.ini"

/*
 * Structure holding a WebRTC audio processing module and necessary
 * info to process and transfer input buffer from device to stream.
 *
 * Below chart describes the buffer structure inside APM and how an input buffer
 * flows from a device through the APM to stream. APM processes audio buffers in
 * fixed 10ms width, and that's the main reason we need two copies of the
 * buffer:
 * (1) to cache input buffer from device until 10ms size is filled.
 * (2) to store the interleaved buffer, of 10ms size also, after APM processing.
 *
 *  ________   _______     _______________________________
 *  |      |   |     |     |_____________APM ____________|
 *  |input |-> | DSP |---> ||           |    |          || -> stream 1
 *  |device|   |     | |   || float buf | -> | byte buf ||
 *  |______|   |_____| |   ||___________|    |__________||
 *                     |   |_____________________________|
 *                     |   _______________________________
 *                     |-> |             APM 2           | -> stream 2
 *                     |   |_____________________________|
 *                     |                                       ...
 *                     |
 *                     |------------------------------------> stream N
 *
 * Members:
 *    apm_ptr - An APM instance from libwebrtc_audio_processing
 *    idev - Pointer to the input device this APM is associated with.
 *    buffer - Stores the processed/interleaved data ready for stream to read.
 *    fbuffer - Stores the floating pointer buffer from input device waiting
 *        for APM to process.
 *    dev_fmt - The format used by the iodev this APM attaches to.
 *    fmt - The audio data format configured for this APM.
 *    area - The cras_audio_area used for copying processed data to client
 *        stream.
 *    work_queue - A task queue instance created and destroyed by
 *        libwebrtc_apm.
 *    only_symmetric_content_in_render - Flag to indicate whether content has
 *        beenobserved in the left or right channel which is not identical.
 *    blocks_with_nonsymmetric_content_in_render - Counter for the number of
 *        consecutive frames where nonsymmetric content in render has been
 *        observed. Used to avoid triggering on short stereo content.
 *    blocks_with_symmetric_content_in_render - Counter for the number of
 *        consecutive frames where symmetric content in render has been
 *        observed. Used for falling-back to mono processing.
 */
struct cras_apm {
	webrtc_apm apm_ptr;
	struct cras_iodev *idev;
	struct byte_buffer *buffer;
	struct float_buffer *fbuffer;
	struct cras_audio_format dev_fmt;
	struct cras_audio_format fmt;
	struct cras_audio_area *area;
	void *work_queue;
	bool only_symmetric_content_in_render;
	int blocks_with_nonsymmetric_content_in_render;
	int blocks_with_symmetric_content_in_render;
	struct cras_apm *prev, *next;
};

/*
 * Structure to hold cras_apm instances created for a stream. A stream may
 * have more than one cras_apm when multiple input devices are enabled.
 * The most common scenario is the silent input iodev be enabled when
 * CRAS switches active input device.
 *
 * Note that cras_stream_apm is owned and modified in main thread.
 * Access with cautious from audio thread.
 * Members:
 *    effects - The effecets bit map of APM.
 *    apms - List of APMs for stream processing. It is a list because
 *        multiple input devices could be configured by user.
 *    echo_ref - If specified, the pointer to an output iodev which shall be
 *        used as echo ref for this apm. When set to NULL it means to
 *        follow what the default_rmod provides as echo ref.
 */
struct cras_stream_apm {
	uint64_t effects;
	struct cras_apm *apms;
	struct cras_iodev *echo_ref;
};

/*
 * Wrappers of APM instances that are active, which means it is associated
 * to a dev/stream pair in audio thread and ready for processing.
 * The existance of an |active_apm| is the key to treat a |cras_apm| is alive
 * and can be used for processing.
 * Members:
 *    apm - The APM for audio data processing.
 *    stream - The associated |cras_stream_apm| instance. It is ensured by
 *        the objects life cycle that whenever an |active_apm| is valid
 *        in audio thread, it's safe to access its |stream| member.
 */
struct active_apm {
	struct cras_apm *apm;
	struct cras_stream_apm *stream;
	struct active_apm *prev, *next;
} * active_apms;

/* Commands from main thread to be handled in audio thread. */
enum APM_THREAD_CMD {
	APM_REVERSE_DEV_CHANGED,
	APM_SET_AEC_REF,
};

/* Message to send command to audio thread. */
struct apm_message {
	enum APM_THREAD_CMD cmd;
};

/* Socket pair to send message from main thread to audio thread. */
static int to_thread_fds[2] = { -1, -1 };

static const char *aec_config_dir = NULL;
static char ini_name[MAX_INI_NAME_LENGTH + 1];
static dictionary *aec_ini = NULL;
static dictionary *apm_ini = NULL;

/* Mono front center format used to configure the process outout end of
 * APM to work around an issue that APM might pick the 1st channel of
 * input, process and then writes to all output channels.
 *
 * The exact condition to trigger this:
 * (1) More than one channel in input
 * (2) More than one channel in output
 * (3) multi_channel_capture is false
 *
 * We're not ready to turn on multi_channel_capture so the best option is
 * to address (2). This is an acceptable fix because it makes APM's
 * behavior align with browser APM.
 */
static struct cras_audio_format mono_channel = { 0, // unused
						 0, // unused
						 1, // mono, front center
						 { -1, -1, -1, -1, 0, -1, -1,
						   -1, -1, -1, -1 } };

static void apm_destroy(struct cras_apm **apm)
{
	if (*apm == NULL)
		return;
	byte_buffer_destroy(&(*apm)->buffer);
	float_buffer_destroy(&(*apm)->fbuffer);
	cras_audio_area_destroy((*apm)->area);

	/* Any unfinished AEC dump handle will be closed. */
	webrtc_apm_destroy((*apm)->apm_ptr);
	free(*apm);
	*apm = NULL;
}

struct cras_stream_apm *cras_stream_apm_create(uint64_t effects)
{
	struct cras_stream_apm *stream;

	if (effects == 0)
		return NULL;

	stream = (struct cras_stream_apm *)calloc(1, sizeof(*stream));
	if (stream == NULL) {
		syslog(LOG_ERR, "No memory in creating stream apm");
		return NULL;
	}
	stream->effects = effects;
	stream->apms = NULL;
	stream->echo_ref = NULL;

	return stream;
}

static struct active_apm *get_active_apm(struct cras_stream_apm *stream,
					 const struct cras_iodev *idev)
{
	struct active_apm *active;

	DL_FOREACH (active_apms, active) {
		if ((active->apm->idev == idev) && (active->stream == stream))
			return active;
	}
	return NULL;
}

struct cras_apm *cras_stream_apm_get_active(struct cras_stream_apm *stream,
					    const struct cras_iodev *idev)
{
	struct active_apm *active = get_active_apm(stream, idev);
	return active ? active->apm : NULL;
}

uint64_t cras_stream_apm_get_effects(struct cras_stream_apm *stream)
{
	if (stream == NULL)
		return 0;
	else
		return stream->effects;
}

void cras_stream_apm_remove(struct cras_stream_apm *stream,
			    const struct cras_iodev *idev)
{
	struct cras_apm *apm;

	DL_FOREACH (stream->apms, apm) {
		if (apm->idev == idev) {
			DL_DELETE(stream->apms, apm);
			apm_destroy(&apm);
		}
	}
}

/*
 * For playout, Chromium generally upmixes mono audio content to stereo before
 * passing the signal to CrAS. To avoid that APM in CrAS treats these as proper
 * stereo signals, this method detects when the content in the first two
 * channels is non-symmetric. That detection allows APM to treat stereo signal
 * as upmixed mono.
 */
int left_and_right_channels_are_symmetric(int num_channels, int rate,
					  float *const *data)
{
	if (num_channels <= 1) {
		return true;
	}

	const int frame_length = rate / APM_NUM_BLOCKS_PER_SECOND;
	return (0 == memcmp(data[0], data[1], frame_length * sizeof(float)));
}

/*
 * WebRTC APM handles no more than stereo + keyboard mic channels.
 * Ignore keyboard mic feature for now because that requires processing on
 * mixed buffer from two input devices. Based on that we should modify the best
 * channel layout for APM use.
 * Args:
 *    apm_fmt - Pointer to a format struct already filled with the value of
 *        the open device format. Its content may be modified for APM use.
 */
static void get_best_channels(struct cras_audio_format *apm_fmt)
{
	int ch;
	int8_t layout[CRAS_CH_MAX];

	/* Using the format from dev_fmt is dangerous because input device
	 * could have wild configurations like unuse the 1st channel and
	 * connects 2nd channel to the only mic. Data in the first channel
	 * is what APM cares about so always construct a new channel layout
	 * containing subset of original channels that matches either FL, FR,
	 * or FC.
	 * TODO(hychao): extend the logic when we have a stream that wants
	 * to record channels like RR(rear right).
	 */
	for (ch = 0; ch < CRAS_CH_MAX; ch++)
		layout[ch] = -1;

	apm_fmt->num_channels = 0;
	if (apm_fmt->channel_layout[CRAS_CH_FL] != -1)
		layout[CRAS_CH_FL] = apm_fmt->num_channels++;
	if (apm_fmt->channel_layout[CRAS_CH_FR] != -1)
		layout[CRAS_CH_FR] = apm_fmt->num_channels++;
	if (apm_fmt->channel_layout[CRAS_CH_FC] != -1)
		layout[CRAS_CH_FC] = apm_fmt->num_channels++;

	for (ch = 0; ch < CRAS_CH_MAX; ch++)
		apm_fmt->channel_layout[ch] = layout[ch];
}

struct cras_apm *cras_stream_apm_add(struct cras_stream_apm *stream,
				     struct cras_iodev *idev,
				     const struct cras_audio_format *dev_fmt)
{
	struct cras_apm *apm;

	DL_FOREACH (stream->apms, apm)
		if (apm->idev == idev)
			return apm;

	// TODO(hychao): Remove the check when we enable more effects.
	if (!((stream->effects & APM_ECHO_CANCELLATION) ||
	      (stream->effects & APM_NOISE_SUPRESSION) ||
	      (stream->effects & APM_GAIN_CONTROL)))
		return NULL;

	apm = (struct cras_apm *)calloc(1, sizeof(*apm));

	/* Configures APM to the format used by input device. If the channel
	 * count is larger than stereo, use the standard channel count/layout
	 * in APM. */
	apm->dev_fmt = *dev_fmt;
	apm->fmt = *dev_fmt;
	get_best_channels(&apm->fmt);

	/* Reset detection of proper stereo */
	apm->only_symmetric_content_in_render = true;
	apm->blocks_with_nonsymmetric_content_in_render = 0;
	apm->blocks_with_symmetric_content_in_render = 0;

	/* Determine whether to enforce effects to be on (regardless of settings
	 * in the apm.ini file). */
	unsigned int enforce_aec_on = 0;
	if (stream->effects & APM_ECHO_CANCELLATION) {
		enforce_aec_on = 1;
	}
	unsigned int enforce_ns_on = 0;
	if (stream->effects & APM_NOISE_SUPRESSION) {
		enforce_ns_on = 1;
	}
	unsigned int enforce_agc_on = 0;
	if (stream->effects & APM_GAIN_CONTROL) {
		enforce_agc_on = 1;
	}

	/*
	 * |aec_ini| and |apm_ini| are tuned specifically for the typical aec
	 * use case, i.e when both audio input and output are internal devices.
	 * Check for that before we use these settings, or just pass NULL so
	 * the default generic settings are used.
	 */
	const bool is_aec_use_case =
		cras_iodev_is_aec_use_case(idev->active_node) &&
		cras_apm_reverse_is_aec_use_case(stream->echo_ref);

	dictionary *aec_ini_use = is_aec_use_case ? aec_ini : NULL;
	dictionary *apm_ini_use = is_aec_use_case ? apm_ini : NULL;

	apm->apm_ptr = webrtc_apm_create_with_enforced_effects(
		apm->fmt.num_channels, apm->fmt.frame_rate, aec_ini_use,
		apm_ini_use, enforce_aec_on, enforce_ns_on, enforce_agc_on);
	if (apm->apm_ptr == NULL) {
		syslog(LOG_ERR,
		       "Fail to create webrtc apm for ch %zu"
		       " rate %zu effect %" PRIu64,
		       dev_fmt->num_channels, dev_fmt->frame_rate,
		       stream->effects);
		free(apm);
		return NULL;
	}

	apm->idev = idev;
	apm->work_queue = NULL;

	/* WebRTC APM wants 1/100 second equivalence of data(a block) to
	 * process. Allocate buffer based on how many frames are in this block.
	 */
	const int frame_length =
		apm->fmt.frame_rate / APM_NUM_BLOCKS_PER_SECOND;
	apm->buffer = byte_buffer_create(frame_length *
					 cras_get_format_bytes(&apm->fmt));
	apm->fbuffer = float_buffer_create(frame_length, apm->fmt.num_channels);
	apm->area = cras_audio_area_create(apm->fmt.num_channels);

	/* TODO(hychao):remove mono_channel once we're ready for multi
	 * channel capture process. */
	cras_audio_area_config_channels(apm->area, &mono_channel);

	DL_APPEND(stream->apms, apm);

	return apm;
}

void cras_stream_apm_start(struct cras_stream_apm *stream,
			   const struct cras_iodev *idev)
{
	struct active_apm *active;
	struct cras_apm *apm;

	if (stream == NULL)
		return;

	/* Check if this apm has already been started. */
	apm = cras_stream_apm_get_active(stream, idev);
	if (apm)
		return;

	DL_SEARCH_SCALAR(stream->apms, apm, idev, idev);
	if (apm == NULL)
		return;

	active = (struct active_apm *)calloc(1, sizeof(*active));
	if (active == NULL) {
		syslog(LOG_ERR, "No memory to start apm.");
		return;
	}
	active->apm = apm;
	active->stream = stream;
	DL_APPEND(active_apms, active);

	cras_apm_reverse_state_update();
}

void cras_stream_apm_stop(struct cras_stream_apm *stream,
			  struct cras_iodev *idev)
{
	struct active_apm *active;

	if (stream == NULL)
		return;

	active = get_active_apm(stream, idev);
	if (active) {
		DL_DELETE(active_apms, active);
		free(active);
	}

	cras_apm_reverse_state_update();
}

int cras_stream_apm_destroy(struct cras_stream_apm *stream)
{
	struct cras_apm *apm;

	/* Unlink any linked echo ref. */
	cras_apm_reverse_link_echo_ref(stream, NULL);

	DL_FOREACH (stream->apms, apm) {
		DL_DELETE(stream->apms, apm);
		apm_destroy(&apm);
	}
	free(stream);

	return 0;
}

/* See comments for process_reverse_t */
static int process_reverse(struct float_buffer *fbuf, unsigned int frame_rate,
			   const struct cras_iodev *echo_ref)
{
	struct active_apm *active;
	int ret;
	float *const *rp;
	unsigned int unused;

	/* Caller side ensures fbuf is full and hasn't been read at all. */
	rp = float_buffer_read_pointer(fbuf, 0, &unused);

	DL_FOREACH (active_apms, active) {
		if (!(active->stream->effects & APM_ECHO_CANCELLATION))
			continue;

		/* Client could assign specific echo ref to an APM. If the
		 * running echo_ref doesn't match then do nothing. */
		if (active->stream->echo_ref &&
		    (active->stream->echo_ref != echo_ref))
			continue;

		if (active->apm->only_symmetric_content_in_render) {
			bool symmetric_content =
				left_and_right_channels_are_symmetric(
					fbuf->num_channels, frame_rate, rp);

			int non_sym_frames =
				active->apm
					->blocks_with_nonsymmetric_content_in_render;
			int sym_frames =
				active->apm
					->blocks_with_symmetric_content_in_render;

			/* Count number of consecutive frames with symmetric
                           and non-symmetric content. */
			non_sym_frames =
				symmetric_content ? 0 : non_sym_frames + 1;
			sym_frames = symmetric_content ? sym_frames + 1 : 0;

			if (non_sym_frames > 2 * APM_NUM_BLOCKS_PER_SECOND) {
				/* Only flag render content to be non-symmetric if it has
                           been non-symmetric for at least 2 seconds. */

				active->apm->only_symmetric_content_in_render =
					false;
			} else if (sym_frames >
				   5 * 60 * APM_NUM_BLOCKS_PER_SECOND) {
				/* Fall-back to consider render content as symmetric if it has
                             been symmetric for 5 minutes. */
				active->apm->only_symmetric_content_in_render =
					false;
			}

			active->apm->blocks_with_nonsymmetric_content_in_render =
				non_sym_frames;
			active->apm->blocks_with_symmetric_content_in_render =
				sym_frames;
		}
		int num_unique_channels =
			active->apm->only_symmetric_content_in_render ?
				1 :
				fbuf->num_channels;

		ret = webrtc_apm_process_reverse_stream_f(active->apm->apm_ptr,
							  num_unique_channels,
							  frame_rate, rp);
		if (ret) {
			syslog(LOG_ERR, "APM process reverse err");
			return ret;
		}
	}
	return 0;
}

/*
 * When APM reverse module has state changes, this callback function is called
 * to ask stream APMs if there's need to process data on the reverse side.
 * This is expected to be called from cras_apm_reverse_state_update() in
 * audio thread so it's safe to access |active_apms|.
 * Args:
 *     default_reverse - True means |echo_ref| is the default reverse module
 *         provided by the system default audio output device.
 *     echo_ref - The device to check if reverse data processing is needed.
 * Returns:
 *     If |echo_ref| should be processed as reverse data for a subset of
 *     active apms.
 */
static int process_reverse_needed(bool default_reverse,
				  const struct cras_iodev *echo_ref)
{
	struct active_apm *active;

	DL_FOREACH (active_apms, active) {
		/* No processing need when APM doesn't ask for AEC. */
		if (!(active->stream->effects & APM_ECHO_CANCELLATION))
			continue;
		/* APM with NULL echo_ref means it tracks default. */
		if (default_reverse && (active->stream->echo_ref == NULL))
			return 1;
		/* APM asked to track given echo_ref specifically.  */
		if (echo_ref && (active->stream->echo_ref == echo_ref))
			return 1;
	}
	return 0;
}

static void get_aec_ini(const char *config_dir)
{
	snprintf(ini_name, MAX_INI_NAME_LENGTH, "%s/%s", config_dir,
		 AEC_CONFIG_NAME);
	ini_name[MAX_INI_NAME_LENGTH] = '\0';

	if (aec_ini) {
		iniparser_freedict(aec_ini);
		aec_ini = NULL;
	}
	aec_ini = iniparser_load_wrapper(ini_name);
	if (aec_ini == NULL)
		syslog(LOG_INFO, "No aec ini file %s", ini_name);
}

static void get_apm_ini(const char *config_dir)
{
	snprintf(ini_name, MAX_INI_NAME_LENGTH, "%s/%s", config_dir,
		 APM_CONFIG_NAME);
	ini_name[MAX_INI_NAME_LENGTH] = '\0';

	if (apm_ini) {
		iniparser_freedict(apm_ini);
		apm_ini = NULL;
	}
	apm_ini = iniparser_load_wrapper(ini_name);
	if (apm_ini == NULL)
		syslog(LOG_INFO, "No apm ini file %s", ini_name);
}

static int send_apm_message(enum APM_THREAD_CMD cmd)
{
	struct apm_message msg;
	msg.cmd = cmd;
	return write(to_thread_fds[1], &msg, sizeof(msg));
}

/* Triggered in main thread when devices state has changed in APM
 * reverse modules. */
static void on_output_devices_changed()
{
	int rc;
	/* Send a message to audio thread because we need to access
	 * |active_apms|. */
	rc = send_apm_message(APM_REVERSE_DEV_CHANGED);
	if (rc < 0)
		syslog(LOG_ERR, "Error sending output devices changed message");
}

/* Receives commands and handles them in audio thread. */
static int apm_thread_callback(void *arg, int revents)
{
	struct apm_message msg;
	int rc;

	if (revents & (POLLERR | POLLHUP)) {
		syslog(LOG_ERR, "Error polling APM message sockect");
		goto read_write_err;
	}

	if (revents & POLLIN) {
		rc = read(to_thread_fds[0], &msg, sizeof(msg));
		if (rc <= 0) {
			syslog(LOG_ERR, "Read APM message error");
			goto read_write_err;
		}
	}

	switch (msg.cmd) {
	case APM_REVERSE_DEV_CHANGED:
	case APM_SET_AEC_REF:
		cras_apm_reverse_state_update();
		break;
	default:
		break;
	}
	return 0;

read_write_err:
	audio_thread_rm_callback(to_thread_fds[0]);
	return 0;
}

int cras_stream_apm_init(const char *device_config_dir)
{
	static const char *cras_apm_metrics_prefix = "Cras.";
	int rc;

	aec_config_dir = device_config_dir;
	get_aec_ini(aec_config_dir);
	get_apm_ini(aec_config_dir);
	webrtc_apm_init_metrics(cras_apm_metrics_prefix);

	rc = pipe(to_thread_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to pipe");
		return rc;
	}

	audio_thread_add_events_callback(to_thread_fds[0], apm_thread_callback,
					 NULL, POLLIN | POLLERR | POLLHUP);

	return cras_apm_reverse_init(process_reverse, process_reverse_needed,
				     on_output_devices_changed);
}

void cras_stream_apm_reload_aec_config()
{
	if (NULL == aec_config_dir)
		return;

	get_aec_ini(aec_config_dir);
	get_apm_ini(aec_config_dir);

	/* Dump the config content at reload only, for debug. */
	webrtc_apm_dump_configs(apm_ini, aec_ini);
}

int cras_stream_apm_deinit()
{
	cras_apm_reverse_deinit();
	audio_thread_rm_callback_sync(cras_iodev_list_get_audio_thread(),
				      to_thread_fds[0]);
	if (to_thread_fds[0] != -1) {
		close(to_thread_fds[0]);
		close(to_thread_fds[1]);
	}
	return 0;
}

int cras_stream_apm_process(struct cras_apm *apm, struct float_buffer *input,
			    unsigned int offset)
{
	unsigned int writable, nframes, nread;
	int ch, i, j, ret;
	float *const *wp;
	float *const *rp;

	nread = float_buffer_level(input);
	if (nread < offset) {
		syslog(LOG_ERR, "Process offset exceeds read level");
		return -EINVAL;
	}

	writable = float_buffer_writable(apm->fbuffer);
	writable = MIN(nread - offset, writable);

	nframes = writable;
	while (nframes) {
		nread = nframes;
		wp = float_buffer_write_pointer(apm->fbuffer);
		rp = float_buffer_read_pointer(input, offset, &nread);

		for (i = 0; i < apm->fbuffer->num_channels; i++) {
			/* Look up the channel position and copy from
			 * the correct index of |input| buffer.
			 */
			for (ch = 0; ch < CRAS_CH_MAX; ch++)
				if (apm->fmt.channel_layout[ch] == i)
					break;
			if (ch == CRAS_CH_MAX)
				continue;

			j = apm->dev_fmt.channel_layout[ch];
			if (j == -1)
				continue;

			memcpy(wp[i], rp[j], nread * sizeof(float));
		}

		nframes -= nread;
		offset += nread;

		float_buffer_written(apm->fbuffer, nread);
	}

	/* process and move to int buffer */
	if ((float_buffer_writable(apm->fbuffer) == 0) &&
	    (buf_queued(apm->buffer) == 0)) {
		nread = float_buffer_level(apm->fbuffer);
		rp = float_buffer_read_pointer(apm->fbuffer, 0, &nread);
		ret = webrtc_apm_process_stream_f(apm->apm_ptr,
						  apm->fmt.num_channels,
						  apm->fmt.frame_rate, rp);
		if (ret) {
			syslog(LOG_ERR, "APM process stream f err");
			return ret;
		}

		/* We configure APM for N-ch input to 1-ch output processing
		 * and that has the side effect that the rest of channels are
		 * filled with the unprocessed content from hardware mic.
		 * Overwrite it with the processed data from first channel to
		 * avoid leaking it later.
		 * TODO(hychao): remove this when we're ready for multi channel
		 * capture process.
		 */
		for (ch = 1; ch < apm->fbuffer->num_channels; ch++)
			memcpy(rp[ch], rp[0], nread * sizeof(float));

		dsp_util_interleave(rp, buf_write_pointer(apm->buffer),
				    apm->fbuffer->num_channels, apm->fmt.format,
				    nread);
		buf_increment_write(apm->buffer,
				    nread * cras_get_format_bytes(&apm->fmt));
		float_buffer_reset(apm->fbuffer);
	}

	return writable;
}

struct cras_audio_area *cras_stream_apm_get_processed(struct cras_apm *apm)
{
	uint8_t *buf_ptr;

	buf_ptr = buf_read_pointer_size(apm->buffer, &apm->area->frames);
	apm->area->frames /= cras_get_format_bytes(&apm->fmt);
	cras_audio_area_config_buf_pointers(apm->area, &apm->fmt, buf_ptr);
	return apm->area;
}

void cras_stream_apm_put_processed(struct cras_apm *apm, unsigned int frames)
{
	buf_increment_read(apm->buffer,
			   frames * cras_get_format_bytes(&apm->fmt));
}

struct cras_audio_format *cras_stream_apm_get_format(struct cras_apm *apm)
{
	return &apm->fmt;
}

bool cras_stream_apm_get_use_tuned_settings(struct cras_stream_apm *stream,
					    const struct cras_iodev *idev)
{
	struct active_apm *active = get_active_apm(stream, idev);
	if (active == NULL)
		return false;

	/* If input and output devices in AEC use case, plus that a
	 * tuned setting is provided. */
	return cras_iodev_is_aec_use_case(idev->active_node) &&
	       cras_apm_reverse_is_aec_use_case(stream->echo_ref) &&
	       (aec_ini || apm_ini);
}

void cras_stream_apm_set_aec_dump(struct cras_stream_apm *stream,
				  const struct cras_iodev *idev, int start,
				  int fd)
{
	struct cras_apm *apm;
	char file_name[256];
	int rc;
	FILE *handle;

	DL_SEARCH_SCALAR(stream->apms, apm, idev, idev);
	if (apm == NULL)
		return;

	if (start) {
		handle = fdopen(fd, "w");
		if (handle == NULL) {
			syslog(LOG_ERR, "Create dump handle fail, errno %d",
			       errno);
			return;
		}
		/* webrtc apm will own the FILE handle and close it. */
		rc = webrtc_apm_aec_dump(apm->apm_ptr, &apm->work_queue, start,
					 handle);
		if (rc)
			syslog(LOG_ERR, "Fail to dump debug file %s, rc %d",
			       file_name, rc);
	} else {
		rc = webrtc_apm_aec_dump(apm->apm_ptr, &apm->work_queue, 0,
					 NULL);
		if (rc)
			syslog(LOG_ERR, "Failed to stop apm debug, rc %d", rc);
	}
}

int cras_stream_apm_set_aec_ref(struct cras_stream_apm *stream,
				struct cras_iodev *echo_ref)
{
	int rc;

	/* Do nothing if this is a duplicate call from client. */
	if (stream->echo_ref == echo_ref)
		return 0;

	stream->echo_ref = echo_ref;

	rc = cras_apm_reverse_link_echo_ref(stream, stream->echo_ref);
	if (rc) {
		syslog(LOG_ERR, "Failed to add echo ref for set aec ref call");
		return rc;
	}

	rc = send_apm_message(APM_SET_AEC_REF);
	if (rc < 0) {
		syslog(LOG_ERR, "Error sending set aec ref message.");
		return rc;
	}
	return 0;
}
