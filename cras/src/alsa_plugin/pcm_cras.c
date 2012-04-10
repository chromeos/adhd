/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <cras_client.h>
#include <sys/socket.h>

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

/* Holds configuration for the alsa plugin.
 *  io - ALSA ioplug object.
 *  fg - Wakes users with polled io.
 *  stream_playing - Indicates if the stream is playing/capturing.
 *  hw_ptr - Current read or write position.
 *  sample_bits - Number of bits in one sample.
 *  channels - Number of channels (Currently only supports two).
 *  stream_id - CRAS ID of the playing/capturing stream.
 *  direction - input or output.
 *  areas - ALSA areas used to read from/write to.
 *  client - CRAS client object.
 */
struct snd_pcm_cras {
	snd_pcm_ioplug_t io;
	int fd;
	int stream_playing;
	unsigned int hw_ptr;
	unsigned int sample_bits;
	unsigned int channels;
	cras_stream_id_t stream_id;
	enum CRAS_STREAM_DIRECTION direction;
	snd_pcm_channel_area_t *areas;
	struct cras_client *client;
};

/* Frees all resources allocated during use. */
static void snd_pcm_cras_free(struct snd_pcm_cras *pcm_cras)
{
	if (pcm_cras == NULL)
		return;
	assert(!pcm_cras->stream_playing);
	if (pcm_cras->fd >= 0)
		close(pcm_cras->fd);
	if (pcm_cras->io.poll_fd >= 0)
		close(pcm_cras->io.poll_fd);
	cras_client_destroy(pcm_cras->client);
	free(pcm_cras->areas);
	free(pcm_cras);
}

/* Stops a playing or capturing CRAS plugin. */
static int snd_pcm_cras_stop(snd_pcm_ioplug_t *io)
{
	struct snd_pcm_cras *pcm_cras = io->private_data;

	if (pcm_cras->stream_playing) {
		cras_client_rm_stream(pcm_cras->client, pcm_cras->stream_id);
		cras_client_stop(pcm_cras->client);
		pcm_cras->stream_playing = 0;
	}
	return 0;
}

/* Close a CRAS plugin opened with snd_pcm_cras_open. */
static int snd_pcm_cras_close(snd_pcm_ioplug_t *io)
{
	struct snd_pcm_cras *pcm_cras = io->private_data;

	if (pcm_cras->stream_playing)
		snd_pcm_cras_stop(io);
	snd_pcm_cras_free(pcm_cras);
	return 0;
}

/* Poll callback used to wait for data ready (playback) or space available
 * (capture). */
static int snd_pcm_cras_poll_revents(snd_pcm_ioplug_t *io,
				     struct pollfd *pfds,
				     unsigned int nfds,
				     unsigned short *revents)
{
	static char buf[1];
	int rc;

	if (pfds == NULL || nfds != 1 || revents == NULL)
		return -EINVAL;
	rc = read(pfds[0].fd, buf, 1);
	if (rc < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
		fprintf(stderr, "%s read failed %d\n", __func__, errno);
		return errno;
	}
	*revents = pfds[0].revents & ~(POLLIN | POLLOUT);
	if (pfds[0].revents & POLLIN)
		*revents |= (io->stream == SND_PCM_STREAM_PLAYBACK) ? POLLOUT
								    : POLLIN;
	return 0;
}

/* Callback to return the location of the write (playback) or read (capture)
 * pointer. */
static snd_pcm_sframes_t snd_pcm_cras_pointer(snd_pcm_ioplug_t *io)
{
	struct snd_pcm_cras *pcm_cras = io->private_data;
	return pcm_cras->hw_ptr;
}

/* Main callback for processing audio.  This is called by CRAS when more samples
 * are needed (playback) or ready (capture).  Copies bytes between ALSA and CRAS
 * buffers. */
static int pcm_cras_process_cb(struct cras_client *client,
			       cras_stream_id_t stream_id,
			       uint8_t *samples,
			       size_t nframes,
			       const struct timespec *sample_time,
			       void *arg)
{
	snd_pcm_ioplug_t *io;
	struct snd_pcm_cras *pcm_cras;
	const snd_pcm_channel_area_t *areas;
	snd_pcm_uframes_t count;
	char dummy_byte;
	size_t chan, frame_bytes, sample_bytes;
	int rc;

	io = (snd_pcm_ioplug_t *)arg;
	pcm_cras = (struct snd_pcm_cras *)io->private_data;
	frame_bytes = cras_client_bytes_per_frame(pcm_cras->client,
						  pcm_cras->stream_id);
	sample_bytes = pcm_cras->sample_bits / 8;

	if (io->stream == SND_PCM_STREAM_PLAYBACK) {
		if (io->state != SND_PCM_STATE_RUNNING) {
			memset(samples, 0, nframes * frame_bytes);
			return nframes;
		}
		/* Only take one period of data at a time. */
		if (nframes > io->period_size)
			nframes = io->period_size;
	}

	/* CRAS always takes interleaved samples. */
	for (chan = 0; chan < io->channels; chan++) {
		pcm_cras->areas[chan].addr = samples + chan * sample_bytes;
		pcm_cras->areas[chan].first = 0;
		pcm_cras->areas[chan].step =
			pcm_cras->sample_bits * io->channels;
	}

	areas = snd_pcm_ioplug_mmap_areas(io);

	count = 0;
	while (count < nframes) {
		snd_pcm_uframes_t frames = nframes - count;
		snd_pcm_uframes_t remain = io->buffer_size - pcm_cras->hw_ptr;

		if (frames > remain)
			frames = remain;

		for (chan = 0; chan < io->channels; chan++)
			if (io->stream == SND_PCM_STREAM_PLAYBACK)
				snd_pcm_area_copy(&pcm_cras->areas[chan],
						  count,
						  &areas[chan],
						  pcm_cras->hw_ptr,
						  frames,
						  io->format);
			else
				snd_pcm_area_copy(&areas[chan],
						  pcm_cras->hw_ptr,
						  &pcm_cras->areas[chan],
						  count,
						  frames,
						  io->format);

		pcm_cras->hw_ptr += frames;
		pcm_cras->hw_ptr %= io->buffer_size;
		count += frames;
	}

	rc = write(pcm_cras->fd, &dummy_byte, 1); /* Wake up polling clients. */
	if (rc < 0 && errno != EWOULDBLOCK && errno != EAGAIN)
		fprintf(stderr, "%s write failed %d\n", __func__, errno);

	return nframes;
}

/* Callback from CRAS for stream errors. */
static int pcm_cras_error_cb(struct cras_client *client,
			     cras_stream_id_t stream_id,
			     int err,
			     void *arg)
{
	fprintf(stderr, "Stream error %d\n", err);
	return 0;
}

/* ALSA calls this automatically when the stream enters the
 * SND_PCM_STATE_PREPARED state. */
static int snd_pcm_cras_prepare(snd_pcm_ioplug_t *io)
{
	struct snd_pcm_cras *pcm_cras = io->private_data;

	return cras_client_connect(pcm_cras->client);
}

/* Called when an ALSA stream is started. */
static int snd_pcm_cras_start(snd_pcm_ioplug_t *io)
{
	struct snd_pcm_cras *pcm_cras = io->private_data;
	struct cras_stream_params *params;
	struct cras_audio_format *audio_format;
	int rc;

	audio_format = cras_audio_format_create(io->format, io->rate,
					      pcm_cras->channels);
	if (audio_format == NULL)
		return -ENOMEM;

	params = cras_client_stream_params_create(
			pcm_cras->direction,
			(io->stream == SND_PCM_STREAM_PLAYBACK) ?
				io->buffer_size : io->period_size,
			io->period_size,
			io->period_size,
			0,
			0,
			io,
			pcm_cras_process_cb,
			pcm_cras_error_cb,
			audio_format);
	if (params == NULL) {
		rc = -ENOMEM;
		goto error_out;
	}

	rc = cras_client_run_thread(pcm_cras->client);
	if (rc < 0)
		goto error_out;

	rc = cras_client_add_stream(pcm_cras->client,
				    &pcm_cras->stream_id,
				    params);
	if (rc < 0) {
		fprintf(stderr, "CRAS add failed\n");
		goto error_out;
	}
	pcm_cras->stream_playing = 1;

error_out:
	cras_audio_format_destroy(audio_format);
	cras_client_stream_params_destroy(params);
	return rc;
}

static snd_pcm_ioplug_callback_t cras_pcm_callback = {
	.close = snd_pcm_cras_close,
	.start = snd_pcm_cras_start,
	.stop = snd_pcm_cras_stop,
	.pointer = snd_pcm_cras_pointer,
	.prepare = snd_pcm_cras_prepare,
	.poll_revents = snd_pcm_cras_poll_revents,
};

/* Set constraints for hw_params.  This lists the handled formats, sample rates,
 * access patters, and buffer/period sizes.  These are enforce in
 * snd_pcm_set_params(). */
static int set_hw_constraints(struct snd_pcm_cras *pcm_cras)
{
	/* period and buffer bytes must be power of two */
	static const unsigned int bytes_list[] = {
		1U<<6, 1U<<7, 1U<<8, 1U<<9, 1U<<10, 1U<<11, 1U<<12, 1U<<13,
		1U<<14, 1U<<15, 1U<<16
	};
	static const unsigned int access_list[] = {
		SND_PCM_ACCESS_MMAP_INTERLEAVED,
		SND_PCM_ACCESS_MMAP_NONINTERLEAVED,
		SND_PCM_ACCESS_RW_INTERLEAVED,
		SND_PCM_ACCESS_RW_NONINTERLEAVED
	};
	static const unsigned int format = SND_PCM_FORMAT_S16_LE;
	int rc;

	pcm_cras->sample_bits = snd_pcm_format_physical_width(format);
	rc = snd_pcm_ioplug_set_param_list(&pcm_cras->io,
					   SND_PCM_IOPLUG_HW_ACCESS,
					   ARRAY_SIZE(access_list),
					   access_list);
	if (rc < 0)
		return rc;
	rc = snd_pcm_ioplug_set_param_list(&pcm_cras->io,
					   SND_PCM_IOPLUG_HW_FORMAT,
					   1,
					   &format);
	if (rc < 0)
		return rc;
	rc = snd_pcm_ioplug_set_param_minmax(&pcm_cras->io,
					     SND_PCM_IOPLUG_HW_CHANNELS,
					     pcm_cras->channels,
					     pcm_cras->channels);
	if (rc < 0)
		return rc;
	rc = snd_pcm_ioplug_set_param_minmax(&pcm_cras->io,
					    SND_PCM_IOPLUG_HW_RATE,
					    8000,
					    48000);
	if (rc < 0)
		return rc;
	rc = snd_pcm_ioplug_set_param_list(&pcm_cras->io,
					   SND_PCM_IOPLUG_HW_BUFFER_BYTES,
					   ARRAY_SIZE(bytes_list), bytes_list);
	if (rc < 0)
		return rc;
	rc = snd_pcm_ioplug_set_param_list(&pcm_cras->io,
					   SND_PCM_IOPLUG_HW_PERIOD_BYTES,
					   ARRAY_SIZE(bytes_list), bytes_list);
	if (rc < 0)
		return rc;
	rc = snd_pcm_ioplug_set_param_minmax(&pcm_cras->io,
					     SND_PCM_IOPLUG_HW_PERIODS,
					     2,
					     64);
	return rc;
}

/* Don't want to block on the poll FDs. */
static int make_nonblock(int fd)
{
	int fl;

	fl = fcntl(fd, F_GETFL);
	if (fl < 0)
		return fl;
	if (fl & O_NONBLOCK)
		return 0;
	return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* Called by snd_pcm_open().  Creates a CRAS client and an ioplug plugin. */
static int snd_pcm_cras_open(snd_pcm_t **pcmp, const char *name,
			     snd_pcm_stream_t stream, int mode)
{
	struct snd_pcm_cras *pcm_cras;
	int rc;
	int fd[2];

	assert(pcmp);
	pcm_cras = calloc(1, sizeof(*pcm_cras));
	if (!pcm_cras)
		return -ENOMEM;

	pcm_cras->fd = -1;
	pcm_cras->io.poll_fd = -1;
	pcm_cras->channels = 2;
	pcm_cras->direction = (stream == SND_PCM_STREAM_PLAYBACK)
				? CRAS_STREAM_OUTPUT : CRAS_STREAM_INPUT;

	rc = cras_client_create(&pcm_cras->client);
	if (rc != 0 || pcm_cras->client == NULL) {
		fprintf(stderr, "Couldn't create CRAS client\n");
		free(pcm_cras);
		return rc;
	}

	pcm_cras->areas = calloc(pcm_cras->channels,
				 sizeof(snd_pcm_channel_area_t));
	if (pcm_cras->areas == NULL) {
		snd_pcm_cras_free(pcm_cras);
		return -ENOMEM;
	}

	socketpair(AF_LOCAL, SOCK_STREAM, 0, fd);

	make_nonblock(fd[0]);
	make_nonblock(fd[1]);

	pcm_cras->fd = fd[0];

	pcm_cras->io.version = SND_PCM_IOPLUG_VERSION;
	pcm_cras->io.name = "ALSA to CRAS Plugin";
	pcm_cras->io.callback = &cras_pcm_callback;
	pcm_cras->io.private_data = pcm_cras;
	pcm_cras->io.poll_fd = fd[1];
	pcm_cras->io.poll_events = POLLIN;
	pcm_cras->io.mmap_rw = 1;

	rc = snd_pcm_ioplug_create(&pcm_cras->io, name, stream, mode);
	if (rc < 0) {
		snd_pcm_cras_free(pcm_cras);
		return rc;
	}

	rc = set_hw_constraints(pcm_cras);
	if (rc < 0) {
		snd_pcm_ioplug_delete(&pcm_cras->io);
		return rc;
	}

	*pcmp = pcm_cras->io.pcm;

	return 0;
}


SND_PCM_PLUGIN_DEFINE_FUNC(cras)
{
	return snd_pcm_cras_open(pcmp, name, stream, mode);
}

SND_PCM_PLUGIN_SYMBOL(cras);
