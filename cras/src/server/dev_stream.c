/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "audio_thread_log.h"
#include "byte_buffer.h"
#include "cras_fmt_conv.h"
#include "cras_rstream.h"
#include "dev_stream.h"
#include "cras_audio_area.h"
#include "cras_mix.h"
#include "cras_rstream.h"
#include "cras_shm.h"

 /*
 * Sleep this much time past the buffer size to be sure at least
 * the buffer size is captured when the audio thread wakes up.
 */
static const struct timespec capture_oversleep_time = {
	.tv_sec = 0,
	.tv_nsec = 250e3 /* TODO(dgreid), is this too long? */
};

struct dev_stream *dev_stream_create(struct cras_rstream *stream,
				     const struct cras_audio_format *dev_fmt)
{
	struct dev_stream *out;
	struct cras_audio_format *stream_fmt = &stream->format;
	int rc = 0;
	unsigned int max_frames;
	unsigned int fmt_conv_num_channels;
	unsigned int fmt_conv_frame_bytes;

	out = calloc(1, sizeof(*out));
	out->stream = stream;

	if (stream->direction == CRAS_STREAM_OUTPUT) {
		fmt_conv_num_channels = dev_fmt->num_channels;
		fmt_conv_frame_bytes = cras_get_format_bytes(dev_fmt);
		max_frames = MAX(stream->buffer_frames,
				 cras_frames_at_rate(stream_fmt->frame_rate,
						     stream->buffer_frames,
						     dev_fmt->frame_rate));
		rc = config_format_converter(&out->conv,
					     stream->direction,
					     stream_fmt,
					     dev_fmt,
					     max_frames);
	} else {
		fmt_conv_num_channels = stream->format.num_channels;
		fmt_conv_frame_bytes = cras_get_format_bytes(&stream->format);
		max_frames = MAX(stream->buffer_frames,
				 cras_frames_at_rate(dev_fmt->frame_rate,
						     stream->buffer_frames,
						     stream_fmt->frame_rate));
		rc = config_format_converter(&out->conv,
					     stream->direction,
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
		unsigned int buf_bytes;

		out->conv_buffer_size_frames = 2 * MAX(dev_frames,
						       stream->buffer_frames);
		buf_bytes = out->conv_buffer_size_frames * fmt_conv_frame_bytes;
		out->conv_buffer = byte_buffer_create(buf_bytes);
		out->conv_area = cras_audio_area_create(fmt_conv_num_channels);
	}

	cras_frames_to_time(cras_rstream_get_cb_threshold(stream),
			    stream_fmt->frame_rate,
			    &stream->sleep_interval_ts);
	clock_gettime(CLOCK_MONOTONIC, &stream->next_cb_ts);

	if (stream->direction != CRAS_STREAM_OUTPUT) {
		add_timespecs(&stream->next_cb_ts, &stream->sleep_interval_ts);
		add_timespecs(&stream->next_cb_ts, &capture_oversleep_time);
	}

	return out;
}

void dev_stream_destroy(struct dev_stream *dev_stream)
{
	if (dev_stream->conv) {
		cras_audio_area_destroy(dev_stream->conv_area);
		cras_fmt_conv_destroy(dev_stream->conv);
		byte_buffer_destroy(dev_stream->conv_buffer);
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
	unsigned int num_to_write;

	shm = cras_rstream_output_shm(dev_stream->stream);
	num_to_write = *count;
	fr_in_buf = cras_shm_get_frames(shm);
	if (fr_in_buf <= 0) {
		if (!cras_rstream_get_is_draining(dev_stream->stream))
			*count = 0;
		return 0;
	}
	if (fr_in_buf < num_to_write)
		num_to_write = fr_in_buf;

	/* Stream volume scaler. */
	mix_vol = cras_shm_get_volume_scaler(shm);

	fr_written = 0;
	fr_read = 0;
	while (fr_written < num_to_write) {
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
					dev_stream->conv_buffer->bytes,
					&read_frames,
					num_to_write - fr_written);
			src = (int16_t *)dev_stream->conv_buffer->bytes;
		} else {
			dev_frames = MIN(frames, num_to_write - fr_written);
			read_frames = dev_frames;
		}
		num_samples = dev_frames * num_channels;
		cras_mix_add(target, src, num_samples, *index,
			     cras_shm_get_mute(shm), mix_vol);
		target += num_samples;
		fr_written += dev_frames;
		fr_read += read_frames;
	}

	/* Don't update the write limit for streams that are draining. */
	if (!cras_rstream_get_is_draining(dev_stream->stream))
		*count = fr_written;
	cras_shm_buffer_read(shm, fr_read);
	audio_thread_event_log_data(atlog, AUDIO_THREAD_DEV_STREAM_MIX,
				    fr_written, fr_read, 0);

	*index = *index + 1;
	return *count;
}

/* Copy from the captured buffer to the temporary format converted buffer. */
static void capture_with_fmt_conv(struct dev_stream *dev_stream,
				  const uint8_t *source_samples,
				  unsigned int num_frames)
{
	const struct cras_audio_format *source_format;
	const struct cras_audio_format *dst_format;
	uint8_t *buffer;
	unsigned int total_read = 0;
	unsigned int write_frames;
	unsigned int read_frames;
	unsigned int source_frame_bytes;
	unsigned int dst_frame_bytes;

	source_format = cras_fmt_conv_in_format(dev_stream->conv);
	source_frame_bytes = cras_get_format_bytes(source_format);
	dst_format = cras_fmt_conv_out_format(dev_stream->conv);
	dst_frame_bytes = cras_get_format_bytes(dst_format);

	dev_stream->conv_area->num_channels = dst_format->num_channels;

	while (total_read < num_frames) {
		buffer = buf_write_pointer_size(dev_stream->conv_buffer,
						&write_frames);
		write_frames /= dst_frame_bytes;

		read_frames = num_frames - total_read;
		write_frames = cras_fmt_conv_convert_frames(
				dev_stream->conv,
				source_samples,
				buffer,
				&read_frames,
				write_frames);
		total_read += read_frames;
		source_samples += read_frames * source_frame_bytes;
		buf_increment_write(dev_stream->conv_buffer,
				    write_frames * dst_frame_bytes);
	}
}

/* Copy from the converted buffer to the stream shm.  These have the same format
 * at this point. */
static void capture_copy_converted_to_stream(struct dev_stream *dev_stream,
					     struct cras_rstream *rstream,
					     unsigned int dev_index)
{
	struct cras_audio_shm *shm;
	uint8_t *stream_samples;
	uint8_t *converted_samples;
	unsigned int num_frames;
	unsigned int total_written = 0;
	unsigned int write_frames;
	unsigned int frame_bytes;
	const struct cras_audio_format *fmt;

	shm = cras_rstream_input_shm(rstream);

	fmt = cras_fmt_conv_out_format(dev_stream->conv);
	frame_bytes = cras_get_format_bytes(fmt);

	stream_samples = cras_shm_get_writeable_frames(
				shm,
				cras_rstream_get_cb_threshold(rstream) -
						dev_stream->mix_offset,
				&rstream->audio_area->frames);
	num_frames = MIN(rstream->audio_area->frames,
			 buf_queued_bytes(dev_stream->conv_buffer) /
							frame_bytes);

	audio_thread_event_log_data(atlog, AUDIO_THREAD_CONV_COPY,
				    cras_shm_frames_written(shm),
				    shm->area->write_buf_idx,
				    num_frames);

	while (total_written < num_frames) {
		converted_samples =
			buf_read_pointer_size(dev_stream->conv_buffer,
					      &write_frames);
		write_frames /= frame_bytes;
		write_frames = MIN(write_frames, num_frames);

		cras_audio_area_config_buf_pointers(dev_stream->conv_area,
						    fmt,
						    converted_samples);
		cras_audio_area_config_channels(dev_stream->conv_area, fmt);
		dev_stream->conv_area->frames = write_frames;

		cras_audio_area_config_buf_pointers(rstream->audio_area,
						    &rstream->format,
						    stream_samples);

		cras_audio_area_copy(rstream->audio_area,
				     dev_stream->mix_offset,
				     cras_get_format_bytes(&rstream->format),
				     dev_stream->conv_area, dev_index);

		buf_increment_read(dev_stream->conv_buffer,
				   write_frames * frame_bytes);
		total_written += write_frames;
		dev_stream->mix_offset += write_frames;
	}

	audio_thread_event_log_data(atlog, AUDIO_THREAD_CAPTURE_WRITE,
				    rstream->stream_id,
				    total_written,
				    cras_shm_frames_written(shm));
}

void dev_stream_capture(struct dev_stream *dev_stream,
			const struct cras_audio_area *area,
			unsigned int dev_index)
{
	struct cras_rstream *rstream = dev_stream->stream;
	struct cras_audio_shm *shm;
	uint8_t *stream_samples;

	/* Check if format conversion is needed. */
	if (dev_stream->conv) {
		capture_with_fmt_conv(dev_stream,
				      area->channels[0].buf,
				      area->frames);
		capture_copy_converted_to_stream(dev_stream, rstream,
						 dev_index);
	} else {
		/* Set up the shm area and copy to it. */
		shm = cras_rstream_input_shm(rstream);
		stream_samples = cras_shm_get_writeable_frames(
				shm,
				cras_rstream_get_cb_threshold(rstream),
				&rstream->audio_area->frames);
		cras_audio_area_config_buf_pointers(rstream->audio_area,
						    &rstream->format,
						    stream_samples);

		cras_audio_area_copy(rstream->audio_area,
				     dev_stream->mix_offset,
				     cras_get_format_bytes(&rstream->format),
				     area, dev_index);
		dev_stream->mix_offset += area->frames;
		audio_thread_event_log_data(atlog, AUDIO_THREAD_CAPTURE_WRITE,
					    rstream->stream_id,
					    area->frames,
					    cras_shm_frames_written(shm));
	}
}

int dev_stream_playback_frames(const struct dev_stream *dev_stream)
{
	struct cras_audio_shm *shm;
	int frames;

	shm = cras_rstream_output_shm(dev_stream->stream);

	frames = cras_shm_get_frames(shm);
	if (frames < 0)
		return frames;

	if (!dev_stream->conv)
		return frames;

	return cras_fmt_conv_in_frames_to_out(dev_stream->conv, frames);
}

unsigned int dev_stream_capture_avail(const struct dev_stream *dev_stream)
{
	struct cras_audio_shm *shm;
	struct cras_rstream *rstream = dev_stream->stream;
	unsigned int cb_threshold = cras_rstream_get_cb_threshold(rstream);
	unsigned int frames_avail;

	shm = cras_rstream_input_shm(rstream);

	cras_shm_get_writeable_frames(shm, cb_threshold, &frames_avail);

	if (!dev_stream->conv)
		return frames_avail;

	return buf_available_bytes(dev_stream->conv_buffer) /
			cras_shm_frame_bytes(shm);
}

/* TODO(dgreid) remove this hack to reset the time if needed. */
static void check_next_wake_time(struct dev_stream *dev_stream)
{
	struct cras_rstream *rstream = dev_stream->stream;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (timespec_after(&now, &rstream->next_cb_ts)) {
		rstream->next_cb_ts = now;
		add_timespecs(&rstream->next_cb_ts,
			      &rstream->sleep_interval_ts);
		printf("behind %x\n", rstream->stream_id);
	}
}

int dev_stream_capture_sleep_frames(struct dev_stream *dev_stream,
				    unsigned int written)
{
	struct cras_rstream *rstream = dev_stream->stream;
	unsigned int str_cb_threshold = cras_rstream_get_cb_threshold(rstream);
	struct cras_audio_shm *shm = cras_rstream_input_shm(rstream);
	unsigned int str_frames;

	if (dev_stream->conv)
		written = cras_fmt_conv_in_frames_to_out(dev_stream->conv,
							 written);
	cras_shm_buffer_written(shm, dev_stream->mix_offset);
	str_frames = cras_shm_frames_written(shm);

	dev_stream->mix_offset = 0;

	/* If this stream doesn't have enough data yet, skip it. */
	if (str_frames < str_cb_threshold)
		return 0;

	/* Enough data for this stream. */
	cras_shm_buffer_write_complete(shm);

	audio_thread_event_log_data(atlog, AUDIO_THREAD_CAPTURE_POST,
				    rstream->stream_id,
				    str_cb_threshold,
				    shm->area->read_buf_idx);

	/* Tell the client that samples are ready and mark the next time it
	 * should be called back. */
	add_timespecs(&rstream->next_cb_ts, &rstream->sleep_interval_ts);
	check_next_wake_time(dev_stream);

	return cras_rstream_audio_ready(rstream, str_cb_threshold);
}

void cras_set_playback_timestamp(size_t frame_rate,
				 size_t frames,
				 struct cras_timespec *ts)
{
	cras_clock_gettime(CLOCK_MONOTONIC, ts);

	/* For playback, want now + samples left to be played.
	 * ts = time next written sample will be played to DAC,
	 */
	ts->tv_nsec += frames * 1000000000ULL / frame_rate;
	while (ts->tv_nsec > 1000000000ULL) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000ULL;
	}
}

void cras_set_capture_timestamp(size_t frame_rate,
				size_t frames,
				struct cras_timespec *ts)
{
	long tmp;

	cras_clock_gettime(CLOCK_MONOTONIC, ts);

	/* For capture, now - samples left to be read.
	 * ts = time next sample to be read was captured at ADC.
	 */
	tmp = frames * (1000000000L / frame_rate);
	while (tmp > 1000000000L) {
		tmp -= 1000000000L;
		ts->tv_sec--;
	}
	if (ts->tv_nsec >= tmp)
		ts->tv_nsec -= tmp;
	else {
		tmp -= ts->tv_nsec;
		ts->tv_nsec = 1000000000L - tmp;
		ts->tv_sec--;
	}
}

void dev_stream_set_delay(const struct dev_stream *dev_stream,
			  unsigned int delay_frames)
{
	struct cras_rstream *rstream = dev_stream->stream;
	struct cras_audio_shm *shm;
	unsigned int stream_frames;

	if (rstream->direction == CRAS_STREAM_OUTPUT) {
		shm = cras_rstream_output_shm(rstream);
		stream_frames = cras_fmt_conv_out_frames_to_in(dev_stream->conv,
							       delay_frames);
		cras_set_playback_timestamp(rstream->format.frame_rate,
					    stream_frames +
						cras_shm_get_frames(shm),
					    &shm->area->ts);
	} else {
		shm = cras_rstream_input_shm(rstream);
		stream_frames = cras_fmt_conv_in_frames_to_out(dev_stream->conv,
							       delay_frames);
		if (cras_shm_frames_written(shm) == 0)
			cras_set_capture_timestamp(
					rstream->format.frame_rate,
					stream_frames,
					&shm->area->ts);
	}
}

int dev_stream_request_playback_samples(struct dev_stream *dev_stream)
{
	struct cras_rstream *rstream = dev_stream->stream;
	struct cras_audio_shm *shm;
	int rc;

	shm = cras_rstream_output_shm(rstream);

	if (cras_shm_is_buffer_available(shm)) {
		rc = cras_rstream_request_audio(dev_stream->stream);
		if (rc < 0)
			return rc;
	} else {
		audio_thread_event_log_data(
				atlog, AUDIO_THREAD_STREAM_SKIP_CB,
				rstream->stream_id,
				shm->area->write_offset[0],
				shm->area->write_offset[1]);
	}

	add_timespecs(&rstream->next_cb_ts, &rstream->sleep_interval_ts);
	check_next_wake_time(dev_stream);

	cras_shm_set_callback_pending(cras_rstream_output_shm(rstream), 1);
	return 0;
}
