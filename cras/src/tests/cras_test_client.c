/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "cras_client.h"
#include "cras_types.h"
#include "cras_util.h"

#define PLAYBACK_CB_THRESHOLD (480)
#define PLAYBACK_BUFFER_SIZE (4800)

static const size_t MAX_IODEVS = 10; /* Max devices to print out. */
static const size_t MAX_ATTACHED_CLIENTS = 10; /* Max clients to print out. */

static uint8_t *file_buf;
static size_t file_buf_size;
static size_t file_buf_read_offset;
static struct timespec last_latency;
static int show_latency;
static int keep_looping = 1;
static int exit_after_done_playing = 1;
static size_t duration_frames;
static int full_frames;
uint32_t min_cb_level = PLAYBACK_CB_THRESHOLD;

struct cras_audio_format *aud_format;

static void check_stream_terminate(size_t frames)
{
	if (duration_frames) {
		if (duration_frames <= frames)
			keep_looping = 0;
		else
			duration_frames -= frames;
	}
}

/* Run from callback thread. */
static int got_samples(struct cras_client *client, cras_stream_id_t stream_id,
		       uint8_t *samples, size_t frames,
		       const struct timespec *sample_time, void *arg)
{
	int *fd = (int *)arg;
	int ret;
	int write_size;

	check_stream_terminate(frames);

	cras_client_calc_capture_latency(sample_time, &last_latency);

	write_size = frames * cras_client_format_bytes_per_frame(aud_format);
	ret = write(*fd, samples, write_size);
	if (ret != write_size)
		printf("Error writing file\n");
	return frames;
}

/* Run from callback thread. */
static int put_samples(struct cras_client *client, cras_stream_id_t stream_id,
		       uint8_t *samples, size_t frames,
		       const struct timespec *sample_time, void *arg)
{
	size_t this_size;
	snd_pcm_uframes_t avail;
	uint32_t frame_bytes = cras_client_format_bytes_per_frame(aud_format);

	if (file_buf_read_offset >= file_buf_size) {
		if (exit_after_done_playing)
			keep_looping = 0;
		return EOF;
	}

	check_stream_terminate(frames);

	if (frames < min_cb_level)
		printf("req for only %zu - %d min\n", frames, min_cb_level);
	avail = frames * frame_bytes;

	this_size = file_buf_size - file_buf_read_offset;
	if (this_size > avail)
		this_size = avail;

	if (full_frames && this_size > min_cb_level * frame_bytes)
		this_size = min_cb_level * frame_bytes;

	cras_client_calc_playback_latency(sample_time, &last_latency);

	memcpy(samples, file_buf + file_buf_read_offset, this_size);
	file_buf_read_offset += this_size;

	return this_size / frame_bytes;
}

static int stream_error(struct cras_client *client,
			cras_stream_id_t stream_id,
			int err,
			void *arg)
{
	printf("Stream error %d\n", err);
	keep_looping = 0;
	return 0;
}

static void print_last_latency()
{
	printf("%u.%09u\n", (unsigned)last_latency.tv_sec,
	       (unsigned)last_latency.tv_nsec);
}

static void print_device_lists(struct cras_client *client)
{
	struct cras_iodev_info devs[MAX_IODEVS];
	size_t i;
	int num_devs;

	num_devs = cras_client_get_output_devices(client, devs, MAX_IODEVS);
	if (num_devs < 0)
		return;
	printf("Output Devices:\n");
	printf("\tID\tPriority\tName\n");
	for (i = 0; i < num_devs; i++)
		printf("\t%zu\t%zu\t\t%s\n", devs[i].idx,
		       devs[i].priority, devs[i].name);
	num_devs = cras_client_get_input_devices(client, devs, MAX_IODEVS);
	if (num_devs < 0)
		return;
	printf("Input Devices:\n");
	printf("\tID\tPriority\tName\n");
	for (i = 0; i < num_devs; i++)
		printf("\t%zu\t%zu\t\t%s\n", devs[i].idx,
		       devs[i].priority, devs[i].name);
}

static void print_attached_client_list(struct cras_client *client)
{
	struct cras_attached_client_info clients[MAX_ATTACHED_CLIENTS];
	size_t i;
	int num_clients;

	num_clients = cras_client_get_attached_clients(client,
						       clients,
						       MAX_ATTACHED_CLIENTS);
	if (num_clients < 0)
		return;
	num_clients = min(num_clients, MAX_ATTACHED_CLIENTS);
	printf("Attached clients:\n");
	printf("\tID\tpid\tuid\n");
	for (i = 0; i < num_clients; i++)
		printf("\t%zu\t%d\t%d\n",
		       clients[i].id,
		       clients[i].pid,
		       clients[i].gid);
}

static void print_system_volumes(struct cras_client *client)
{
	printf("System Volume (0-100): %zu %s\n"
	       "Capture Gain: %.2fdB %s\n",
	       cras_client_get_system_volume(client),
	       cras_client_get_system_muted(client) ? "(Muted)" : "",
	       cras_client_get_system_capture_gain(client) / 100.0,
	       cras_client_get_system_capture_muted(client) ? "(Muted)" : "");
}

static int start_stream(struct cras_client *client,
			cras_stream_id_t *stream_id,
			struct cras_stream_params *params,
			float stream_volume)
{
	int rc;

	file_buf_read_offset = 0;

	rc = cras_client_add_stream(client, stream_id, params);
	if (rc < 0) {
		fprintf(stderr, "adding a stream\n");
		return rc;
	}
	return cras_client_set_stream_volume(client, *stream_id, stream_volume);
}

static int run_file_io_stream(struct cras_client *client,
			      int fd,
			      enum CRAS_STREAM_DIRECTION direction,
			      size_t buffer_frames,
			      size_t cb_threshold,
			      size_t rate,
			      size_t num_channels,
			      int flags)
{
	struct cras_stream_params *params;
	cras_playback_cb_t aud_cb;
	cras_stream_id_t stream_id = 0;
	int stream_playing = 0;
	int *pfd = malloc(sizeof(*pfd));
	*pfd = fd;
	fd_set poll_set;
	struct timespec sleep_ts;
	float volume_scaler = 1.0;
	size_t sys_volume = 100;
	long cap_gain = 0;
	int mute = 0;

	sleep_ts.tv_sec = 0;
	sleep_ts.tv_nsec = 250 * 1000000;

	if (direction == CRAS_STREAM_INPUT)
		aud_cb = got_samples;
	else
		aud_cb = put_samples;

	aud_format = cras_audio_format_create(SND_PCM_FORMAT_S16_LE, rate,
					      num_channels);
	if (aud_format == NULL)
		return -ENOMEM;

	params = cras_client_stream_params_create(direction,
						  buffer_frames,
						  cb_threshold,
						  min_cb_level,
						  0,
						  0,
						  pfd,
						  aud_cb,
						  stream_error,
						  aud_format);
	if (params == NULL)
		return -ENOMEM;

	cras_client_run_thread(client);

	stream_playing =
		start_stream(client, &stream_id, params, volume_scaler);

	while (keep_looping) {
		char input;
		int nread;

		FD_ZERO(&poll_set);
		FD_SET(1, &poll_set);
		sleep_ts.tv_sec = 0;
		sleep_ts.tv_nsec = 750 * 1000000;
		pselect(2, &poll_set, NULL, NULL, &sleep_ts, NULL);

		if (stream_playing && show_latency)
			print_last_latency();

		if (!FD_ISSET(1, &poll_set))
			continue;

		nread = read(1, &input, 1);
		if (nread < 1) {
			fprintf(stderr, "Error reading stdin\n");
			return nread;
		}
		switch (input) {
		case 'q':
			keep_looping = 0;
			break;
		case 's':
			if (stream_playing)
				break;

			/* If started by hand keep running after it finishes. */
			exit_after_done_playing = 0;

			stream_playing = start_stream(client,
						      &stream_id,
						      params,
						      volume_scaler);
			break;
		case 'r':
			if (!stream_playing)
				break;
			cras_client_rm_stream(client, stream_id);
			stream_playing = 0;
			break;
		case 'u':
			volume_scaler = min(volume_scaler + 0.1, 1.0);
			cras_client_set_stream_volume(client,
						      stream_id,
						      volume_scaler);
			break;
		case 'd':
			volume_scaler = max(volume_scaler - 0.1, 0.0);
			cras_client_set_stream_volume(client,
						      stream_id,
						      volume_scaler);
			break;
		case 'k':
			sys_volume = min(sys_volume + 1, 100);
			cras_client_set_system_volume(client, sys_volume);
			break;
		case 'j':
			sys_volume = sys_volume == 0 ? 0 : sys_volume - 1;
			cras_client_set_system_volume(client, sys_volume);
			break;
		case 'K':
			cap_gain = min(cap_gain + 100, 5000);
			cras_client_set_system_capture_gain(client, cap_gain);
			break;
		case 'J':
			cap_gain = cap_gain == -5000 ? -5000 : cap_gain - 100;
			cras_client_set_system_capture_gain(client, cap_gain);
			break;
		case 'm':
			mute = !mute;
			cras_client_set_system_mute(client, mute);
			break;
		case '@':
			print_device_lists(client);
			break;
		case '#':
			print_attached_client_list(client);
			break;
		case 'v':
			printf("Volume: %zu%s Min dB: %ld Max dB: %ld\n"
			       "Capture: %ld%s Min dB: %ld Max dB: %ld\n",
			       cras_client_get_system_volume(client),
			       cras_client_get_system_muted(client) ? "(Muted)"
								    : "",
			       cras_client_get_system_min_volume(client),
			       cras_client_get_system_max_volume(client),
			       cras_client_get_system_capture_gain(client),
			       cras_client_get_system_capture_muted(client) ?
						"(Muted)" : "",
			       cras_client_get_system_min_capture_gain(client),
			       cras_client_get_system_max_capture_gain(client));
			break;
		case '\n':
			break;
		default:
			printf("Invalid key\n");
			break;
		}
	}
	cras_client_stop(client);

	cras_audio_format_destroy(aud_format);
	cras_client_stream_params_destroy(params);
	free(pfd);

	return 0;
}

static int run_capture(struct cras_client *client,
		       const char *file,
		       size_t buffer_frames,
		       size_t cb_threshold,
		       size_t rate,
		       size_t num_channels,
		       int flags)
{
	int fd = open(file, O_CREAT | O_RDWR, 0666);
	if (fd == -1) {
		perror("failed to open file");
		return -errno;
	}

	run_file_io_stream(client, fd, CRAS_STREAM_INPUT, buffer_frames,
			   cb_threshold, rate, num_channels, flags);

	close(fd);
	return 0;
}

static int run_playback(struct cras_client *client,
			const char *file,
			size_t buffer_frames,
			size_t cb_threshold,
			size_t rate,
			size_t num_channels,
			int flags)
{
	int fd;

	file_buf = malloc(1024*1024*4);
	if (!file_buf) {
		perror("allocating file_buf");
		return -ENOMEM;
	}

	fd = open(file, O_RDONLY);
	if (fd == -1) {
		perror("failed to open file");
		return -errno;
	}
	file_buf_size = read(fd, file_buf, 1024*1024*4);

	run_file_io_stream(client, fd, CRAS_STREAM_OUTPUT, buffer_frames,
			   cb_threshold, rate, num_channels, flags);

	close(fd);
	return 0;
}

static void print_server_info(struct cras_client *client)
{
	cras_client_run_thread(client);
	cras_client_connected_wait(client); /* To synchronize data. */
	print_system_volumes(client);
	print_device_lists(client);
	print_attached_client_list(client);
}

static struct option long_options[] = {
	{"show_latency",	no_argument, &show_latency, 1},
	{"write_full_frames",	no_argument, &full_frames, 1},
	{"rate",		required_argument,	0, 'r'},
	{"num_channels",        required_argument,      0, 'n'},
	{"iodev_index",		required_argument,	0, 'o'},
	{"capture_file",	required_argument,	0, 'c'},
	{"playback_file",	required_argument,	0, 'p'},
	{"callback_threshold",	required_argument,	0, 't'},
	{"min_cb_level",	required_argument,	0, 'm'},
	{"mute",                required_argument,      0, 'u'},
	{"buffer_frames",	required_argument,	0, 'b'},
	{"duration_seconds",	required_argument,	0, 'd'},
	{"volume",              required_argument,      0, 'v'},
	{"capture_gain",        required_argument,      0, 'g'},
	{"dump_server_info",    no_argument,            0, 'i'},
	{"help",                no_argument,            0, 'h'},
	{0, 0, 0, 0}
};

static void show_usage()
{
	printf("--show_latency - Display latency while playing or recording.\n");
	printf("--write_full_frames - Write data in blocks of min_cb_level.\n");
	printf("--rate <N> - Specifies the sample rate in Hz.\n");
	printf("--num_channels <N> - Two for stereo.\n");
	printf("--iodev_index <N> - Set active iodev to N.\n");
	printf("--capture_file <name> - Name of file to record to.\n");
	printf("--playback_file <name> - Name of file to play.\n");
	printf("--callback_threshold <N> - Number of samples remaining when callback in invoked.\n");
	printf("--min_cb_level <N> - Minimum # of samples writeable when playback callback is called.\n");
	printf("--mute <0|1> - Set system mute state.\n");
	printf("--buffer_frames <N> - Total number of frames to buffer.\n");
	printf("--duration_seconds <N> - Seconds to record or playback.\n");
	printf("--volume <0-100> - Set system output volume.\n");
	printf("--capture_gain <dB> - Set system caputre gain in dB*100 (100 = 1dB).\n");
	printf("--dump_server_info - Print status of the server.\n");
	printf("--help - Print this message.\n");
}

int main(int argc, char **argv)
{
	struct cras_client *client;
	int c, option_index;
	size_t buffer_size = PLAYBACK_BUFFER_SIZE;
	size_t cb_threshold = PLAYBACK_CB_THRESHOLD;
	size_t rate = 48000;
	size_t iodev_index = 0;
	int set_iodev = 0;
	size_t num_channels = 2;
	size_t duration_seconds = 0;
	const char *capture_file = NULL;
	const char *playback_file = NULL;
	int rc = 0;

	option_index = 0;

	rc = cras_client_create(&client);
	if (rc < 0) {
		fprintf(stderr, "Couldn't create client.\n");
		return rc;
	}

	rc = cras_client_connect(client);
	if (rc) {
		fprintf(stderr, "Couldn't connect to server.\n");
		goto destroy_exit;
	}

	while (1) {
		c = getopt_long(argc, argv, "o:s:",
				long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
		case 'c':
			capture_file = optarg;
			break;
		case 'p':
			playback_file = optarg;
			break;
		case 't':
			cb_threshold = atoi(optarg);
			break;
		case 'm':
			min_cb_level = atoi(optarg);
			break;
		case 'b':
			buffer_size = atoi(optarg);
			break;
		case 'r':
			rate = atoi(optarg);
			break;
		case 'n':
			num_channels = atoi(optarg);
			break;
		case 'o':
			set_iodev = 1;
			iodev_index = atoi(optarg);
			break;
		case 'd':
			duration_seconds = atoi(optarg);
			break;
		case 'u': {
			int mute = atoi(optarg);
			rc = cras_client_set_system_mute(client, mute);
			if (rc < 0) {
				fprintf(stderr, "problem setting mute\n");
				goto destroy_exit;
			}
			break;
		}
		case 'v': {
			int volume = atoi(optarg);
			volume = min(100, max(0, volume));
			rc = cras_client_set_system_volume(client, volume);
			if (rc < 0) {
				fprintf(stderr, "problem setting volume\n");
				goto destroy_exit;
			}
			break;
		}
		case 'g': {
			long gain = atol(optarg);
			rc = cras_client_set_system_capture_gain(client, gain);
			if (rc < 0) {
				fprintf(stderr, "problem setting capture\n");
				goto destroy_exit;
			}
			break;
		}
		case 'i':
			print_server_info(client);
			break;
		case 'h':
			show_usage();
			break;
		default:
			break;
		}
	}

	if (set_iodev) {
		rc = cras_client_switch_iodev(client,
					      CRAS_STREAM_TYPE_DEFAULT,
					      iodev_index);
		if (rc < 0)
			goto destroy_exit;
	}

	duration_frames = duration_seconds * rate;

	if (capture_file != NULL) {
		rc = run_capture(client, capture_file, buffer_size, 0, rate,
				 num_channels, 0);
		if (rc < 0)
			goto destroy_exit;
	}

	if (playback_file != NULL) {
		rc = run_playback(client, playback_file, buffer_size,
				  cb_threshold, rate, num_channels, 0);
	}

destroy_exit:
	cras_client_destroy(client);
	return rc;
}
