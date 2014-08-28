/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "audio_thread_log.h"
#include "cras_fmt_conv.h"
#include "cras_rstream.h"
#include "dev_stream.h"
#include "cras_audio_area.h"
#include "cras_mix.h"
#include "cras_rstream.h"
#include "cras_shm.h"

struct dev_stream *dev_stream_create(struct cras_rstream *stream,
				     const struct cras_audio_format *dev_fmt)
{
	struct dev_stream *out;
	struct cras_audio_format *stream_fmt = &stream->format;
	int rc;
	unsigned int max_frames;

	out = calloc(1, sizeof(*out));
	out->stream = stream;


	if (stream->direction == CRAS_STREAM_OUTPUT) {
		max_frames = MAX(stream->buffer_frames,
				 cras_frames_at_rate(stream_fmt->frame_rate,
						     stream->buffer_frames,
						     dev_fmt->frame_rate));
		rc = config_format_converter(&out->conv, stream_fmt,
				dev_fmt, max_frames);
	} else {
		max_frames = MAX(stream->buffer_frames,
				 cras_frames_at_rate(dev_fmt->frame_rate,
						     stream->buffer_frames,
						     stream_fmt->frame_rate));
		rc = config_format_converter(&out->conv,
					     dev_fmt,
					     stream_fmt,
					     max_frames);
	}

	if (rc) {
		free(out);
		return NULL;
	}

	if (out->conv) {
		unsigned int dev_frames =
			cras_fmt_conv_in_frames_to_out(out->conv,
						       stream->buffer_frames);

		out->conv_buffer_size_frames = dev_frames;
		out->conv_buffer = (uint8_t *)malloc(dev_frames *
						cras_get_format_bytes(dev_fmt));
	}

	return out;
}

void dev_stream_destroy(struct dev_stream *dev_stream)
{
	if (dev_stream->conv) {
		cras_fmt_conv_destroy(dev_stream->conv);
		free(dev_stream->conv_buffer);
	}
	free(dev_stream);
}

unsigned int dev_stream_mix(struct dev_stream *dev_stream,
			    size_t num_channels,
			    uint8_t *dst,
			    size_t *count,
			    size_t *index)
{
	struct cras_audio_shm *shm;
	int16_t *src;
	int16_t *target = (int16_t *)dst;
	unsigned int fr_written, fr_read;
	int fr_in_buf;
	unsigned int num_samples;
	size_t frames = 0;
	unsigned int dev_frames;
	float mix_vol;

	shm = cras_rstream_output_shm(dev_stream->stream);
	fr_in_buf = cras_shm_get_frames(shm);
	if (fr_in_buf <= 0) {
		*count = 0;
		return 0;
	}
	if (fr_in_buf < *count)
		*count = fr_in_buf;

	/* Stream volume scaler. */
	mix_vol = cras_shm_get_volume_scaler(shm);

	fr_written = 0;
	fr_read = 0;
	while (fr_written < *count) {
		unsigned int read_frames;
		src = cras_shm_get_readable_frames(shm, fr_read,
				&frames);
		if (frames == 0)
			break;
		if (dev_stream->conv) {
			read_frames = frames;
			dev_frames = cras_fmt_conv_convert_frames(
					dev_stream->conv,
					(uint8_t *)src,
					(uint8_t *)dev_stream->conv_buffer,
					&read_frames,
					*count - fr_written);
			src = (int16_t *)dev_stream->conv_buffer;
		} else {
			dev_frames = MIN(frames, *count - fr_written);
			read_frames = dev_frames;
		}
		num_samples = dev_frames * num_channels;
		cras_mix_add(target, src, num_samples, *index,
			     cras_shm_get_mute(shm), mix_vol);
		target += num_samples;
		fr_written += dev_frames;
		fr_read += read_frames;
	}

	*count = fr_written;
	cras_shm_buffer_read(shm, fr_read);
	audio_thread_event_log_data(atlog, AUDIO_THREAD_DEV_STREAM_MIX,
				    fr_written, fr_read, 0);

	*index = *index + 1;
	return *count;
}

void dev_stream_capture(const struct dev_stream *dev_stream,
			struct cras_audio_area *area,
			unsigned int offset,
			unsigned int count,
			unsigned int dev_index)
{
	struct cras_rstream *rstream = dev_stream->stream;
	struct cras_audio_shm *shm;
	uint8_t *dst;

	shm = cras_rstream_input_shm(rstream);
	dst = cras_shm_get_writeable_frames(shm,
					    cras_shm_used_frames(shm),
					    NULL);
	cras_audio_area_config_buf_pointers(rstream->input_audio_area,
					    &rstream->format,
					    dst);
	rstream->input_audio_area->frames = cras_shm_used_frames(shm);
	cras_audio_area_copy(rstream->input_audio_area, offset,
			     cras_get_format_bytes(&rstream->format),
			     area, dev_index);
}
