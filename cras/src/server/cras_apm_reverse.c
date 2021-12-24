/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_apm_list.h"
#include "cras_apm_reverse.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_system_state.h"
#include "float_buffer.h"

/*
 * Object used to analyze playback audio from output iodev. It is responsible
 * to get buffer containing latest output data and provide it to the APM
 * instances which want to analyze reverse stream.
 *
 * - How does this reverse module connects with output iodev?
 * An instance of this reverse module is expected to be passed as a
 * (struct ext_dsp_module *) to cras_iodev_set_ext_dsp_module() so that
 * when audio data runs through the associated iodev's DSP pipeline it
 * will trigger ext->run(ext, ...) which is implemented below as
 * reverse_data_run()
 *
 * Member:
 *    ext - The interface implemented to process reverse(output) stream
 *        data in various formats.
 *    fbuf - Middle buffer holding reverse data for APMs to analyze.
 *    odev - Pointer to the output iodev playing audio as the reverse
 *        stream. NULL if there's no playback stream.
 *    dev_rate - The sample rate odev is opened for.
 *    needs_to_process - Flag to indicate if this reverse module needs to
 *        process. The logic could be complex to determine if the overall
 *        APM states requires this reverse module to process. Given that
 *        ext->run() is called rather frequently from DSP pipeline, we use
 *        this flag to save the computation every time.
 */
struct cras_apm_reverse_module {
	struct ext_dsp_module ext;
	struct float_buffer *fbuf;
	struct cras_iodev *odev;
	unsigned int dev_rate;
	unsigned needs_to_process;
};

static bool hw_echo_ref_disabled = 0;

/* The reverse module corresponding to the dynamically changing default
 * enabled iodev in cras_iodev_list. It is subjected to change along
 * with audio output device selection. */
static struct cras_apm_reverse_module *default_rmod = NULL;

/* The utilitiy functions provided during init and wrapper to call into them. */
static process_reverse_t process_reverse_callback;
static process_reverse_needed_t process_reverse_needed_callback;

static int apm_process_reverse_callback(struct float_buffer *fbuf,
					unsigned int frame_rate)
{
	if (process_reverse_callback == NULL)
		return 0;
	return process_reverse_callback(fbuf, frame_rate);
}
static int apm_process_reverse_needed()
{
	if (process_reverse_needed_callback == NULL)
		return 0;
	return process_reverse_needed_callback();
}

/*
 * Determines the iodev to be used as the echo reference for APM reverse
 * analysis. If there exists the special purpose "echo reference dev" then
 * use it. Otherwise just use this output iodev.
 */
static struct cras_iodev *get_echo_reference_target(struct cras_iodev *iodev)
{
	/* Don't use HW echo_reference_dev if specified in board config. */
	if (hw_echo_ref_disabled)
		return iodev;
	return iodev->echo_reference_dev ? iodev->echo_reference_dev : iodev;
}

/*
 * Gets the first enabled output iodev in the list, determines the echo
 * reference target base on this output iodev, and registers default_rmod as
 * ext dsp module to this echo reference target.
 * When this echo reference iodev is opened and audio data flows through its
 * dsp pipeline, APMs will anaylize the reverse stream. This is expected to be
 * called in main thread when output devices enable/dsiable state changes.
 */
static void handle_iodev_states_changed(struct cras_iodev *iodev, void *cb_data)
{
	struct cras_iodev *echo_ref, *old;

	if (iodev && (iodev->direction != CRAS_STREAM_OUTPUT))
		return;

	/* Register to the first enabled output device. */
	iodev = cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT);
	if (iodev == NULL)
		return;

	echo_ref = get_echo_reference_target(iodev);

	/* If default_rmod is already tracking echo_ref, do nothing. */
	if (default_rmod->odev == echo_ref)
		return;

	/* Sets up the new default echo ref. */
	old = default_rmod->odev;
	default_rmod->odev = echo_ref;
	cras_iodev_set_ext_dsp_module(echo_ref, &default_rmod->ext);

	/* Detach from the old iodev that default_rmod was tracking.
	 * Note that default_rmod->odev is NULL when this function is
	 * called for the first time during init. */
	if (old)
		cras_iodev_set_ext_dsp_module(old, NULL);
}

static void reverse_data_run(struct ext_dsp_module *ext, unsigned int nframes)
{
	struct cras_apm_reverse_module *rmod =
		(struct cras_apm_reverse_module *)ext;
	unsigned int writable;
	int i, offset = 0;
	float *const *wp;

	if (!rmod->needs_to_process)
		return;

	/* Repeat the loop to copy total nframes of data from the DSP pipeline
	 * (i.e ext->ports) over to rmod->fbuf as AEC reference for the actual
	 * processing work in apm_process_reverse_callback.
	 */
	while (nframes) {
		/* If at any moment the rmod->fbuf is full, call out to
		 * the process reverse callback and then reset it to mark
		 * AEC reference data as consumed. */
		if (!float_buffer_writable(rmod->fbuf)) {
			apm_process_reverse_callback(rmod->fbuf,
						     rmod->dev_rate);
			float_buffer_reset(rmod->fbuf);
		}
		writable = float_buffer_writable(rmod->fbuf);
		writable = MIN(nframes, writable);
		wp = float_buffer_write_pointer(rmod->fbuf);
		for (i = 0; i < rmod->fbuf->num_channels; i++)
			memcpy(wp[i], ext->ports[i] + offset,
			       writable * sizeof(float));

		offset += writable;
		float_buffer_written(rmod->fbuf, writable);
		nframes -= writable;
	}
}

static void reverse_data_configure(struct ext_dsp_module *ext,
				   unsigned int buffer_size,
				   unsigned int num_channels, unsigned int rate)
{
	struct cras_apm_reverse_module *rmod =
		(struct cras_apm_reverse_module *)ext;
	if (rmod->fbuf)
		float_buffer_destroy(&rmod->fbuf);
	rmod->fbuf = float_buffer_create(rate / APM_NUM_BLOCKS_PER_SECOND,
					 num_channels);
	rmod->dev_rate = rate;
}

int cras_apm_reverse_init(process_reverse_t process_cb,
			  process_reverse_needed_t process_needed_cb)
{
	process_reverse_callback = process_cb;
	process_reverse_needed_callback = process_needed_cb;

	hw_echo_ref_disabled = cras_system_get_hw_echo_ref_disabled();

	if (default_rmod == NULL) {
		default_rmod = (struct cras_apm_reverse_module *)calloc(
			1, sizeof(*default_rmod));
		if (!default_rmod)
			return -ENOMEM;
		default_rmod->ext.run = reverse_data_run;
		default_rmod->ext.configure = reverse_data_configure;
	}

	cras_iodev_list_set_device_enabled_callback(
		handle_iodev_states_changed, handle_iodev_states_changed, NULL);
	handle_iodev_states_changed(NULL, NULL);
	return 0;
}

void cras_apm_reverse_state_update()
{
	if (default_rmod)
		default_rmod->needs_to_process = apm_process_reverse_needed();
}

bool cras_apm_reverse_is_aec_use_case()
{
	/* Invalid usage if caller didn't call init first. And we don't care
	 * what is returned in that case, so let's give it a false. */
	if (!default_rmod)
		return 0;
	return cras_iodev_is_aec_use_case(default_rmod->odev->active_node);
}

void cras_apm_reverse_deinit()
{
	if (default_rmod) {
		if (default_rmod->fbuf)
			float_buffer_destroy(&default_rmod->fbuf);
		free(default_rmod);
		default_rmod = NULL;
	}
}
