/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>

#include "cras_audio_area.h"
#include "cras_hfp_slc.h"
#include "cras_iodev.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "strlcpy.h"
#include "utlist.h"
#include "cras_bt_device.h"

#include "cras_hfp_alsa_iodev.h"
#include "cras_hfp_manager.h"

/* Object to represent a special HFP iodev which would be managed by bt_io but
 * playback/capture via an inner ALSA iodev.
 * Members:
 *    base - The base class cras_iodev.
 *    aio - The effective iodev for playback/capture.
 *  BlueZ (null if not applicable):
 *    device - The corresponding remote BT device.
 *    slc - The service level connection.
 *    sco - The cras_sco instance for configuring audio path.
 *  Floss (null if not applicable):
 *    hfp - The corresponding cras_hfp manager object
 */
struct hfp_alsa_io {
	struct cras_iodev base;
	struct cras_iodev *aio;
	struct cras_bt_device *device;
	struct hfp_slc_handle *slc;
	struct cras_sco *sco;
	struct cras_hfp *hfp;
};

static int hfp_alsa_get_valid_frames(struct cras_iodev *iodev,
				     struct timespec *hw_tstamp)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	return aio->get_valid_frames(aio, hw_tstamp);
}

static int hfp_alsa_open_dev(struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;
	int rc;

	rc = aio->open_dev(aio);
	if (rc) {
		syslog(LOG_ERR, "Failed to open aio: %d\n", rc);
		return rc;
	}

	if (hfp_alsa_io->device) {
		/* Check the associated SCO object first. Because configuring
		 * the shared SCO object can only be done once for the HFP
		 * input and output devices pair.
		 */
		rc = cras_sco_get_fd(hfp_alsa_io->sco);
		if (rc >= 0)
			return 0;

		hfp_slc_codec_connection_setup(hfp_alsa_io->slc);

		rc = cras_bt_device_sco_connect(
			hfp_alsa_io->device,
			hfp_slc_get_selected_codec(hfp_alsa_io->slc), true);
		if (rc < 0) {
			syslog(LOG_ERR, "Failed to get sco: %d\n", rc);
			return rc;
		}

		cras_sco_set_fd(hfp_alsa_io->sco, rc);
	} else {
		thread_callback empty_cb = NULL;
		cras_floss_hfp_start(hfp_alsa_io->hfp, empty_cb,
				     iodev->direction);
	}

	return 0;
}

static int hfp_alsa_update_supported_formats(struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;

	/* 16 bit, mono, 8kHz (narrow band speech); */
	free(iodev->supported_rates);
	iodev->supported_rates = malloc(2 * sizeof(*iodev->supported_rates));
	if (!iodev->supported_rates)
		return -ENOMEM;

	if (hfp_alsa_io->device) {
		iodev->supported_rates[0] =
			hfp_slc_get_selected_codec(hfp_alsa_io->slc) ==
					HFP_CODEC_ID_MSBC ?
				16000 :
				8000;
	} else {
		iodev->supported_rates[0] =
			cras_floss_hfp_get_wbs_supported(hfp_alsa_io->hfp) ?
				16000 :
				8000;
	}

	iodev->supported_rates[1] = 0;

	free(iodev->supported_channel_counts);
	iodev->supported_channel_counts =
		malloc(2 * sizeof(*iodev->supported_channel_counts));
	if (!iodev->supported_channel_counts)
		return -ENOMEM;
	iodev->supported_channel_counts[0] = 1;
	iodev->supported_channel_counts[1] = 0;

	free(iodev->supported_formats);
	iodev->supported_formats =
		malloc(2 * sizeof(*iodev->supported_formats));
	if (!iodev->supported_formats)
		return -ENOMEM;
	iodev->supported_formats[0] = SND_PCM_FORMAT_S16_LE;
	iodev->supported_formats[1] = 0;

	return 0;
}

static int hfp_alsa_configure_dev(struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;
	int rc;

	/* Fill back the format iodev is using. */
	if (aio->format == NULL) {
		aio->format = (struct cras_audio_format *)malloc(
			sizeof(*aio->format));
		if (!aio->format)
			return -ENOMEM;
		*aio->format = *iodev->format;
	}

	rc = aio->configure_dev(aio);
	if (rc) {
		syslog(LOG_ERR, "Failed to configure aio: %d\n", rc);
		return rc;
	}

	if (hfp_alsa_io->device) {
		cras_sco_add_iodev(hfp_alsa_io->sco, iodev->direction,
				   iodev->format);
		hfp_set_call_status(hfp_alsa_io->slc, 1);
	}

	iodev->buffer_size = aio->buffer_size;

	return 0;
}

static int hfp_alsa_close_dev(struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	if (hfp_alsa_io->device) {
		cras_sco_rm_iodev(hfp_alsa_io->sco, iodev->direction);

		/* Check the associated SCO object because cleaning up the
		 * shared SLC and SCO objects should be done when the later
		 * of HFP input or output device gets closed.
		 */
		if (!cras_sco_has_iodev(hfp_alsa_io->sco)) {
			hfp_set_call_status(hfp_alsa_io->slc, 0);
			cras_sco_close_fd(hfp_alsa_io->sco);
		}
	} else {
		cras_floss_hfp_stop(hfp_alsa_io->hfp, iodev->direction);
	}

	cras_iodev_free_format(iodev);
	return aio->close_dev(aio);
}

static int hfp_alsa_frames_queued(const struct cras_iodev *iodev,
				  struct timespec *tstamp)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	return aio->frames_queued(aio, tstamp);
}

static int hfp_alsa_delay_frames(const struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	return aio->delay_frames(aio);
}

static int hfp_alsa_get_buffer(struct cras_iodev *iodev,
			       struct cras_audio_area **area, unsigned *frames)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	return aio->get_buffer(aio, area, frames);
}

static int hfp_alsa_put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	return aio->put_buffer(aio, nwritten);
}

static int hfp_alsa_flush_buffer(struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	return aio->flush_buffer(aio);
}

static void hfp_alsa_update_active_node(struct cras_iodev *iodev,
					unsigned node_idx, unsigned dev_enabled)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	aio->update_active_node(aio, node_idx, dev_enabled);
}

static int hfp_alsa_start(struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	return aio->start(aio);
}

static void hfp_alsa_set_volume(struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;

	if (hfp_alsa_io->device) {
		size_t volume = cras_system_get_volume();
		if (iodev->active_node)
			volume = cras_iodev_adjust_node_volume(
				iodev->active_node, volume);

		hfp_event_speaker_gain(hfp_alsa_io->slc, volume);
	} else {
		cras_floss_hfp_set_volume(hfp_alsa_io->hfp,
					  iodev->active_node->volume);
	}
}

static int hfp_alsa_no_stream(struct cras_iodev *iodev, int enable)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	/*
	 * Copy iodev->min_cb_level and iodev->max_cb_level from the parent
	 * (i.e. hfp_alsa_iodev).  no_stream() of alsa_io will use them.
	 */
	aio->min_cb_level = iodev->min_cb_level;
	aio->max_cb_level = iodev->max_cb_level;
	return aio->no_stream(aio, enable);
}

static int hfp_alsa_is_free_running(const struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	return aio->is_free_running(aio);
}

static int hfp_alsa_output_underrun(struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_iodev *aio = hfp_alsa_io->aio;

	/*
	 * Copy iodev->min_cb_level and iodev->max_cb_level from the parent
	 * (i.e. hfp_alsa_iodev).  output_underrun() of alsa_io will use them.
	 */
	aio->min_cb_level = iodev->min_cb_level;
	aio->max_cb_level = iodev->max_cb_level;

	return aio->output_underrun(aio);
}

struct cras_iodev *hfp_alsa_iodev_create(struct cras_iodev *aio,
					 struct cras_bt_device *device,
					 struct hfp_slc_handle *slc,
					 struct cras_sco *sco,
					 struct cras_hfp *hfp)
{
	struct hfp_alsa_io *hfp_alsa_io;
	struct cras_iodev *iodev;
	struct cras_ionode *node;
	const char *name;

	hfp_alsa_io = (struct hfp_alsa_io *)calloc(1, sizeof(*hfp_alsa_io));
	if (!hfp_alsa_io)
		return NULL;

	iodev = &hfp_alsa_io->base;
	iodev->direction = aio->direction;

	hfp_alsa_io->aio = aio;
	hfp_alsa_io->device = device;
	hfp_alsa_io->slc = slc;
	hfp_alsa_io->sco = sco;
	hfp_alsa_io->hfp = hfp;

	/* Set iodev's name to device readable name or the address. */
	if (device) {
		name = cras_bt_device_name(device);
		if (!name)
			name = cras_bt_device_object_path(device);
	} else {
		name = cras_floss_hfp_get_display_name(hfp);
	}
	snprintf(iodev->info.name, sizeof(iodev->info.name), "%s", name);
	iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = 0;
	iodev->info.stable_id = device ? cras_bt_device_get_stable_id(device) :
					 cras_floss_hfp_get_stable_id(hfp);

	iodev->open_dev = hfp_alsa_open_dev;
	iodev->update_supported_formats = hfp_alsa_update_supported_formats;
	iodev->configure_dev = hfp_alsa_configure_dev;
	iodev->close_dev = hfp_alsa_close_dev;

	iodev->frames_queued = hfp_alsa_frames_queued;
	iodev->delay_frames = hfp_alsa_delay_frames;
	iodev->get_buffer = hfp_alsa_get_buffer;
	iodev->put_buffer = hfp_alsa_put_buffer;
	iodev->flush_buffer = hfp_alsa_flush_buffer;

	iodev->update_active_node = hfp_alsa_update_active_node;
	iodev->start = hfp_alsa_start;
	iodev->set_volume = hfp_alsa_set_volume;
	iodev->get_valid_frames = hfp_alsa_get_valid_frames;
	iodev->no_stream = hfp_alsa_no_stream;
	iodev->is_free_running = hfp_alsa_is_free_running;
	iodev->output_underrun = hfp_alsa_output_underrun;

	iodev->min_buffer_level = aio->min_buffer_level;

	node = calloc(1, sizeof(*node));
	node->dev = iodev;
	strlcpy(node->name, iodev->info.name, sizeof(node->name));

	node->plugged = 1;
	/* If headset mic uses legacy narrow band, i.e CVSD codec, report a
	 * different node type so UI can set different plug priority. */
	node->type = CRAS_NODE_TYPE_BLUETOOTH;
	if (device) {
		if (!hfp_slc_get_wideband_speech_supported(slc) &&
		    (iodev->direction == CRAS_STREAM_INPUT))
			node->type = CRAS_NODE_TYPE_BLUETOOTH_NB_MIC;
	} else {
		if (!cras_floss_hfp_get_wbs_supported(hfp) &&
		    (iodev->direction == CRAS_STREAM_INPUT))
			node->type = CRAS_NODE_TYPE_BLUETOOTH_NB_MIC;
	}
	node->volume = 100;
	gettimeofday(&node->plugged_time, NULL);

	node->btflags |= CRAS_BT_FLAG_HFP | CRAS_BT_FLAG_SCO_OFFLOAD;

	/* Prepare active node before append, so bt_io can extract correct
	 * info from hfp_alsa iodev and node. */
	cras_iodev_add_node(iodev, node);
	cras_iodev_set_active_node(iodev, node);

	if (device) {
		cras_bt_device_append_iodev(device, iodev, CRAS_BT_FLAG_HFP);
	}

	/* Record max supported channels into cras_iodev_info. */
	iodev->info.max_supported_channels = 1;

	/* Specifically disable EWMA calculation on this and the child iodev. */
	ewma_power_disable(&iodev->ewma);
	ewma_power_disable(&aio->ewma);

	return iodev;
}

void hfp_alsa_iodev_destroy(struct cras_iodev *iodev)
{
	struct hfp_alsa_io *hfp_alsa_io = (struct hfp_alsa_io *)iodev;
	struct cras_ionode *node;

	if (hfp_alsa_io->device) {
		cras_bt_device_rm_iodev(hfp_alsa_io->device, iodev);
	}

	node = iodev->active_node;
	if (node) {
		cras_iodev_rm_node(iodev, node);
		free(node);
	}

	free(iodev->supported_channel_counts);
	free(iodev->supported_rates);
	free(iodev->supported_formats);
	cras_iodev_free_resources(iodev);

	free(hfp_alsa_io);
}
