/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include <syslog.h>

#include <webrtc-apm/webrtc_apm.h>

#include "cras_apm_list.h"
#include "cras_audio_format.h"
#include "utlist.h"


/*
 * Structure holding a WebRTC audio processing module and necessary
 * info to process and transfer input buffer from device to stream.
 * Members:
 *    apm_ptr - An APM instance from libwebrtc_audio_processing
 *    dev_ptr - Pointer to the device this APM is associated with.
 *    fmt - The audio data format configured for this APM.
 */
struct cras_apm {
	webrtc_apm apm_ptr;
	void *dev_ptr;
	struct cras_audio_format fmt;
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
