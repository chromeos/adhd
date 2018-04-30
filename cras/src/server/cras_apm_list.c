/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include <syslog.h>

#include <webrtc-apm/webrtc_apm.h>

#include "byte_buffer.h"
#include "cras_apm_list.h"
#include "cras_audio_area.h"
#include "cras_audio_format.h"
#include "dsp_util.h"
#include "float_buffer.h"
#include "utlist.h"


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
 *    dev_ptr - Pointer to the device this APM is associated with.
 *    buffer - Stores the processed/interleaved data ready for stream to read.
 *    fbuffer - Stores the floating pointer buffer from input device waiting
 *        for APM to process.
 *    fmt - The audio data format configured for this APM.
 *    area - The cras_audio_area used for copying processed data to client
 *        stream.
 */
struct cras_apm {
	webrtc_apm apm_ptr;
	void *dev_ptr;
	struct byte_buffer *buffer;
	struct float_buffer *fbuffer;
	struct cras_audio_format fmt;
	struct cras_audio_area *area;
	struct cras_apm *prev, *next;
};

/*
 * Lists of cras_apm instances created for a stream. A stream may
 * have more than one cras_apm when multiple input devices are
 * enabled. The most common scenario is the silent input iodev be
 * enabled when CRAS switches active input device.
 */
struct cras_apm_list {
	void *stream_ptr;
	uint64_t effects;
	struct cras_apm *apms;
	struct cras_apm_list *prev, *next;
};

static struct cras_apm_list *apm_list = NULL;

static void apm_destroy(struct cras_apm **apm)
{
	if (*apm == NULL)
		return;
	byte_buffer_destroy(&(*apm)->buffer);
	float_buffer_destroy(&(*apm)->fbuffer);
	cras_audio_area_destroy((*apm)->area);
	webrtc_apm_destroy((*apm)->apm_ptr);
	free(*apm);
	*apm = NULL;
}

struct cras_apm_list *cras_apm_list_create(void *stream_ptr,
					   uint64_t effects)
{
	struct cras_apm_list *list;

	if (effects == 0)
		return NULL;

	DL_SEARCH_SCALAR(apm_list, list, stream_ptr, stream_ptr);
	if (list)
		return list;

	list = (struct cras_apm_list *)calloc(1, sizeof(*list));
	list->stream_ptr = stream_ptr;
	list->effects = effects;
	list->apms = NULL;
	DL_APPEND(apm_list, list);

	return list;
}

struct cras_apm *cras_apm_list_get(struct cras_apm_list *list, void *dev_ptr)
{
	struct cras_apm *apm;

	if (list == NULL)
		return NULL;

	DL_FOREACH(list->apms, apm) {
		if (apm->dev_ptr == dev_ptr)
			return apm;
	}
	return NULL;
}

uint64_t cras_apm_list_get_effects(struct cras_apm_list *list)
{
	if (list == NULL)
		return 0;
	else
		return list->effects;
}

void cras_apm_list_remove(struct cras_apm_list *list, void *dev_ptr)
{
	struct cras_apm *apm;

	DL_FOREACH(list->apms, apm) {
		if (apm->dev_ptr == dev_ptr ) {
			DL_DELETE(list->apms, apm);
			apm_destroy(&apm);
		}
	}
}

struct cras_apm *cras_apm_list_add(struct cras_apm_list *list,
				   void *dev_ptr,
				   const struct cras_audio_format *fmt)
{
	struct cras_apm *apm;

	DL_FOREACH(list->apms, apm) {
		if (apm->dev_ptr == dev_ptr) {
			DL_DELETE(list->apms, apm);
			apm_destroy(&apm);
		}
	}

	// TODO(hychao): Remove the check when we enable more effects.
	if (!(list->effects & APM_ECHO_CANCELLATION))
		return NULL;

	apm = (struct cras_apm *)calloc(1, sizeof(*apm));

	apm->apm_ptr = webrtc_apm_create(
			fmt->num_channels,
			fmt->frame_rate,
			list->effects & APM_ECHO_CANCELLATION);
	if (apm->apm_ptr == NULL) {
		syslog(LOG_ERR, "Fail to create webrtc apm for ch %zu"
				" rate %zu effect %lu",
				fmt->num_channels,
				fmt->frame_rate,
				list->effects);
		free(apm);
		return NULL;
	}

	apm->fmt = *fmt;
	apm->dev_ptr = dev_ptr;

	/* WebRTC APM wants 10 ms equivalence of data to process. */
	apm->buffer = byte_buffer_create(10 * apm->fmt.frame_rate / 1000 *
					 cras_get_format_bytes(&apm->fmt));
	apm->fbuffer = float_buffer_create(10 * apm->fmt.frame_rate / 1000,
					   apm->fmt.num_channels);
	apm->area = cras_audio_area_create(apm->fmt.num_channels);
	cras_audio_area_config_channels(apm->area, &apm->fmt);

	DL_APPEND(list->apms, apm);

	return apm;
}

int cras_apm_list_destroy(struct cras_apm_list *list)
{
	struct cras_apm_list *tmp;
	struct cras_apm *apm;

	DL_FOREACH(apm_list, tmp) {
		if (tmp == list) {
			DL_DELETE(apm_list, tmp);
			break;
		}
	}

	if (tmp == NULL)
		return 0;

	DL_FOREACH(list->apms, apm) {
		DL_DELETE(list->apms, apm);
		apm_destroy(&apm);
	}
	free(list);

	return 0;
}

int cras_apm_list_process(struct cras_apm *apm,
			  struct float_buffer *input,
			  unsigned int offset)
{
	unsigned int writable, nframes, nread;
	int i, ret;
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

		for (i = 0; i < apm->fbuffer->num_channels; i++)
			memcpy(wp[i], rp[i], nread * sizeof(float));

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
						  apm->fmt.frame_rate,
						  rp);
		if (ret) {
			syslog(LOG_ERR, "APM process stream f err");
			return ret;
		}

		dsp_util_interleave(rp,
				    buf_write_pointer(apm->buffer),
				    apm->fbuffer->num_channels,
				    apm->fmt.format,
				    nread);
		buf_increment_write(apm->buffer,
				    nread * cras_get_format_bytes(&apm->fmt));
		float_buffer_reset(apm->fbuffer);
	}

	return writable;
}

struct cras_audio_area *cras_apm_list_get_processed(struct cras_apm *apm)
{
	uint8_t *buf_ptr;

	buf_ptr = buf_read_pointer_size(apm->buffer, &apm->area->frames);
	apm->area->frames /= cras_get_format_bytes(&apm->fmt);
	cras_audio_area_config_buf_pointers(apm->area, &apm->fmt, buf_ptr);
	return apm->area;
}

void cras_apm_list_put_processed(struct cras_apm *apm, unsigned int frames)
{
	buf_increment_read(apm->buffer,
			   frames * cras_get_format_bytes(&apm->fmt));
}
