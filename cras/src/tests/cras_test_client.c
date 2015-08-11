/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <unistd.h>

#include "cras_sbc_codec.h"
#include "cras_client.h"
#include "cras_types.h"
#include "cras_util.h"
#include "cras_version.h"

#define NOT_ASSIGNED (0)
#define PLAYBACK_BUFFERED_TIME_IN_US (5000)

#define BUF_SIZE 32768

static const size_t MAX_IODEVS = 10; /* Max devices to print out. */
static const size_t MAX_IONODES = 20; /* Max ionodes to print out. */
static const size_t MAX_ATTACHED_CLIENTS = 10; /* Max clients to print out. */

static int pipefd[2];
static struct timespec last_latency;
static int show_latency;
static float last_rms_sqr_sum;
static int last_rms_size;
static float total_rms_sqr_sum;
static int total_rms_size;
static int show_rms;
static int show_total_rms;
static int keep_looping = 1;
static int exit_after_done_playing = 1;
static size_t duration_frames;
static int pause_client = 0;

static struct cras_audio_codec *capture_codec;
static struct cras_audio_codec *playback_codec;
static unsigned char cap_buf[BUF_SIZE];
static char *channel_layout = NULL;
static int pin_device_id;

/* Conditional so the client thread can signal that main should exit. */
static pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;

struct cras_audio_format *aud_format;

static int terminate_stream_loop()
{
	keep_looping = 0;
	return write(pipefd[1], "1", 1);
}

static size_t get_block_size(uint64_t buffer_time_in_us, size_t rate)
{
	return (size_t)(buffer_time_in_us * rate / 1000000);
}

static void check_stream_terminate(size_t frames)
{
	if (duration_frames) {
		if (duration_frames <= frames)
			terminate_stream_loop();
		else
			duration_frames -= frames;
	}
}

/* Compute square sum of samples (for calculation of RMS value). */
float compute_sqr_sum_16(const int16_t *samples, int size)
{
	unsigned i;
	float sqr_sum = 0;

	for (i = 0; i < size; i++)
		sqr_sum += samples[i] * samples[i];

	return sqr_sum;
}

/* Update the RMS values with the given samples. */
int update_rms(const uint8_t *samples, int size)
{
	switch (aud_format->format) {
	case SND_PCM_FORMAT_S16_LE: {
		last_rms_sqr_sum = compute_sqr_sum_16((int16_t *)samples, size / 2);
		last_rms_size = size / 2;
		break;
	}
	default:
		return -EINVAL;
	}

	total_rms_sqr_sum += last_rms_sqr_sum;
	total_rms_size += last_rms_size;

	return 0;
}

/* Run from callback thread. */
static int got_samples(struct cras_client *client,
		       cras_stream_id_t stream_id,
		       uint8_t *captured_samples,
		       uint8_t *playback_samples,
		       unsigned int frames,
		       const struct timespec *captured_time,
		       const struct timespec *playback_time,
		       void *user_arg)
{
	int *fd = (int *)user_arg;
	int ret;
	int write_size;
	int processed_bytes, frame_bytes;
	size_t encoded;

	cras_client_calc_capture_latency(captured_time, &last_latency);

	frame_bytes = cras_client_format_bytes_per_frame(aud_format);
	write_size = frames * frame_bytes;

	/* Update RMS values with all available frames. */
	if (keep_looping) {
		update_rms(captured_samples,
			   MIN(write_size, duration_frames * frame_bytes));
	}

	check_stream_terminate(frames);

	if (capture_codec) {
		processed_bytes = capture_codec->encode(
				capture_codec,
				captured_samples,
				write_size,
				cap_buf,
				BUF_SIZE,
				&encoded);
		if (processed_bytes <= 0 || processed_bytes > write_size) {
			terminate_stream_loop();
			return EOF;
		}

		ret = write(*fd, cap_buf, encoded);
		if (ret != encoded)
			printf("Error writing file\n");

		return processed_bytes / frame_bytes;
	} else {
		ret = write(*fd, captured_samples, write_size);
		if (ret != write_size)
			printf("Error writing file\n");
		return frames;
	}
}

/* Run from callback thread. */
static int got_hotword(struct cras_client *client,
		       cras_stream_id_t stream_id,
		       uint8_t *captured_samples,
		       uint8_t *playback_samples,
		       unsigned int frames,
		       const struct timespec *captured_time,
		       const struct timespec *playback_time,
		       void *user_arg)
{
	printf("got hotword %u frames\n", frames);

	return frames;
}

/* Run from callback thread. */
static int put_samples(struct cras_client *client,
		       cras_stream_id_t stream_id,
		       uint8_t *captured_samples,
		       uint8_t *playback_samples,
		       unsigned int frames,
		       const struct timespec *captured_time,
		       const struct timespec *playback_time,
		       void *user_arg)
{
	size_t this_size, decoded;
	uint32_t frame_bytes = cras_client_format_bytes_per_frame(aud_format);
	int fd = *(int *)user_arg;
	uint8_t buff[BUF_SIZE];
	int nread;

	while (pause_client)
		usleep(10000);

	check_stream_terminate(frames);

	cras_client_calc_playback_latency(playback_time, &last_latency);

	nread = read(fd, buff, MIN(frames * frame_bytes, BUF_SIZE));
	if (nread <= 0) {
		if (exit_after_done_playing)
			terminate_stream_loop();
		return nread;
	}

	if (playback_codec) {
		this_size = playback_codec->decode(playback_codec,
				       buff,
				       nread,
				       playback_samples,
				       frames * frame_bytes,
				       &decoded);
		if (this_size == 0) {
			printf("stop looping\n");
			terminate_stream_loop();
			return EOF;
		}
		return decoded / frame_bytes;
	} else {
		memcpy(playback_samples,
		       buff,
		       nread);
		return nread / frame_bytes;
	}
}

/* Run from callback thread. */
static int put_stdin_samples(struct cras_client *client,
		       cras_stream_id_t stream_id,
		       uint8_t *captured_samples,
		       uint8_t *playback_samples,
		       unsigned int frames,
		       const struct timespec *captured_time,
		       const struct timespec *playback_time,
		       void *user_arg)
{
	int rc = 0;
	uint32_t frame_bytes = cras_client_format_bytes_per_frame(aud_format);

	rc = read(0, playback_samples, frames * frame_bytes);
	if (rc <= 0) {
		terminate_stream_loop();
		return -1;
	}

	return rc / frame_bytes;
}

static int stream_error(struct cras_client *client,
			cras_stream_id_t stream_id,
			int err,
			void *arg)
{
	printf("Stream error %d\n", err);
	terminate_stream_loop();
	return 0;
}

static void print_last_latency()
{
	if (last_latency.tv_sec > 0 || last_latency.tv_nsec > 0)
		printf("%u.%09u\n", (unsigned)last_latency.tv_sec,
		       (unsigned)last_latency.tv_nsec);
	else {
		printf("-%lld.%09lld\n", (long long)-last_latency.tv_sec,
		       (long long)-last_latency.tv_nsec);
	}
}

static void print_last_rms()
{
	if (last_rms_size != 0)
		printf("%.9f\n", sqrt(last_rms_sqr_sum / last_rms_size));
}

static void print_total_rms()
{
	if (total_rms_size != 0)
		printf("%.9f\n", sqrt(total_rms_sqr_sum / total_rms_size));
}

static void print_dev_info(const struct cras_iodev_info *devs, int num_devs)
{
	unsigned i;

	printf("\tID\tName\n");
	for (i = 0; i < num_devs; i++)
		printf("\t%u\t%s\n", devs[i].idx, devs[i].name);
}

static void print_node_info(const struct cras_ionode_info *nodes, int num_nodes,
			    int is_input)
{
	unsigned i;

	printf("\t ID\t%4s   Plugged\tL/R swapped\t      "
	       "Time\tType\t\t Name\n", is_input ? "Gain" : " Vol");
	for (i = 0; i < num_nodes; i++)
		printf("\t%u:%u\t%5g  %7s\t%14s\t%10ld\t%-16s%c%s\n",
		       nodes[i].iodev_idx,
		       nodes[i].ionode_idx,
		       is_input ? nodes[i].capture_gain / 100.0
		       : (double) nodes[i].volume,
		       nodes[i].plugged ? "yes" : "no",
		       nodes[i].left_right_swapped ? "yes" : "no",
		       (long) nodes[i].plugged_time.tv_sec,
		       nodes[i].type,
		       nodes[i].active ? '*' : ' ',
		       nodes[i].name);
}

static void print_device_lists(struct cras_client *client)
{
	struct cras_iodev_info devs[MAX_IODEVS];
	struct cras_ionode_info nodes[MAX_IONODES];
	size_t num_devs, num_nodes;
	int rc;

	num_devs = MAX_IODEVS;
	num_nodes = MAX_IONODES;
	rc = cras_client_get_output_devices(client, devs, nodes, &num_devs,
					    &num_nodes);
	if (rc < 0)
		return;
	printf("Output Devices:\n");
	print_dev_info(devs, num_devs);
	printf("Output Nodes:\n");
	print_node_info(nodes, num_nodes, 0);

	num_devs = MAX_IODEVS;
	num_nodes = MAX_IONODES;
	rc = cras_client_get_input_devices(client, devs, nodes, &num_devs,
					   &num_nodes);
	printf("Input Devices:\n");
	print_dev_info(devs, num_devs);
	printf("Input Nodes:\n");
	print_node_info(nodes, num_nodes, 1);
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
	num_clients = MIN(num_clients, MAX_ATTACHED_CLIENTS);
	printf("Attached clients:\n");
	printf("\tID\tpid\tuid\n");
	for (i = 0; i < num_clients; i++)
		printf("\t%u\t%d\t%d\n",
		       clients[i].id,
		       clients[i].pid,
		       clients[i].gid);
}

static void print_active_stream_info(struct cras_client *client)
{
	struct timespec ts;
	unsigned num_streams;

	num_streams = cras_client_get_num_active_streams(client, &ts);
	printf("Num active streams: %u\n", num_streams);
	printf("Last audio active time: %llu, %llu\n",
	       (long long)ts.tv_sec, (long long)ts.tv_nsec);
}

static void print_system_volumes(struct cras_client *client)
{
	printf("System Volume (0-100): %zu %s\n"
	       "Capture Gain (%.2f - %.2f): %.2fdB %s\n",
	       cras_client_get_system_volume(client),
	       cras_client_get_system_muted(client) ? "(Muted)" : "",
	       cras_client_get_system_min_capture_gain(client) / 100.0,
	       cras_client_get_system_max_capture_gain(client) / 100.0,
	       cras_client_get_system_capture_gain(client) / 100.0,
	       cras_client_get_system_capture_muted(client) ? "(Muted)" : "");
}

static void show_alog_tag(const struct audio_thread_event_log *log,
			  unsigned int tag_idx)
{
	unsigned int tag = (log->log[tag_idx].tag_sec >> 24) & 0x1f;
	unsigned int sec = log->log[tag_idx].tag_sec & 0x00ffffff;
	unsigned int nsec = log->log[tag_idx].nsec;
	unsigned int data1 = log->log[tag_idx].data1;
	unsigned int data2 = log->log[tag_idx].data2;
	unsigned int data3 = log->log[tag_idx].data3;

	/* Skip unused log entries. */
	if (log->log[tag_idx].tag_sec == 0 && log->log[tag_idx].nsec == 0)
		return;

	switch (tag) {
	case AUDIO_THREAD_WAKE:
		printf("WAKE: %u.%09u num_fds %d\n", sec, nsec, (int)data1);
		break;
	case AUDIO_THREAD_SLEEP:
		printf("SLEEP: %u.%09u %09d.%09d long:%09d\n", sec, nsec,
		       (int)data1, (int)data2, (int)data3);
		break;
	case AUDIO_THREAD_READ_AUDIO:
		printf("READ_AUDIO: %u.%09u dev: %x hw_level: %u read %u\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_READ_AUDIO_DONE:
		printf("READ_AUDIO_DONE: %u.%09u read remainder %u\n",
		       sec, nsec, data1);
		break;
	case AUDIO_THREAD_FILL_AUDIO:
		printf("FILL_AUDIO: %u.%09u dev %x hw_level %u\n",
		       sec, nsec, data1, data2);
		break;
	case AUDIO_THREAD_FILL_AUDIO_DONE:
		printf("FILL_AUDIO_DONE: %u.%09u total_written %u\n",
		       sec, nsec, data1);
		break;
	case AUDIO_THREAD_WRITE_STREAMS_WAIT:
		printf("WRITE_STREAMS_WAIT: %u.%09u for %u.%06u\n",
		       sec, nsec, data1, data2);
		break;
	case AUDIO_THREAD_WRITE_STREAMS_WAIT_TO:
		printf("WRITE_STREAMS_WAIT_TO: %u.%09u\n",
		       sec, nsec);
		break;
	case AUDIO_THREAD_WRITE_STREAMS_MIX:
		printf("WRITE_STREAMS_MIX: %u.%09u wlimit %u max_offset %u\n",
		       sec, nsec, data1, data2);
		break;
	case AUDIO_THREAD_WRITE_STREAMS_MIXED:
		printf("WRITE_STREAMS_MIXED: %u.%09u write_limit %u\n",
		       sec, nsec, data1);
		break;
	case AUDIO_THREAD_WRITE_STREAMS_STREAM:
		printf("WRITE_STREAMS_STREAM: %u.%09u id %x "
		       "shm_frames %u cb_pending %u\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_FETCH_STREAM:
		printf("WRITE_STREAMS_FETCH_STREAM: %u.%09u id %x cbth %u delay %u\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_STREAM_ADDED:
		printf("STREAM_ADDED: %u.%9u id %x dev_idx %u\n",
		       sec, nsec, data1, data2);
		break;
	case AUDIO_THREAD_STREAM_REMOVED:
		printf("STREAM_REMOVED: %u.%9u id %x\n", sec, nsec, data1);
		break;
	case AUDIO_THREAD_A2DP_ENCODE:
		printf("A2DP_ENCODE: %u.%09u proc %d queued %u readable %u\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_A2DP_WRITE:
		printf("A2DP_WRITE: %u.%09u written %d queued %u\n",
		       sec, nsec, data1, data2);
		break;
	case AUDIO_THREAD_DEV_STREAM_MIX:
		printf("DEV_STREAM_MIX: %u.%09u written %u read %u\n",
		       sec, nsec, data1, data2);
		break;
	case AUDIO_THREAD_CAPTURE_POST:
		printf("CAPTURE_POST: %u.%09u stream %x thresh %u rd_buf %u\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_CAPTURE_WRITE:
		printf("CAPTURE_WRITE: %u.%09u stream %x write %u shm_fr %u\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_CONV_COPY:
		printf("CONV_COPY: %u.%09u wr_buf %u shm_writable %u"
		       "offset %u\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_STREAM_SLEEP_TIME:
		printf("STREAM_SLEEP_TIME: %u.%09u id:%x wake:%09u.%09d\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_STREAM_SLEEP_ADJUST:
		printf("STREAM_SLEEP_ADJUST: %u.%09u id:%x from:%09u.%09d\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_STREAM_SKIP_CB:
		printf("STREAM_SKIP_CB: %u.%9u id %x write offsets %u %u\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_DEV_SLEEP_TIME:
		printf("DEV_SLEEP_TIME: %u.%09u devidx:%x wake:%09u.%09d\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_SET_DEV_WAKE:
		printf("SET_DEV_WAKE: %u.%09u devidx:%x adj:%u min_cbth:%u\n",
		       sec, nsec, data1, data2, data3);
		break;
	case AUDIO_THREAD_DEV_ADDED:
		printf("DEV_ADDED: %u.%09u devidx:%x\n",
		       sec, nsec, data1);
		break;
	case AUDIO_THREAD_DEV_REMOVED:
		printf("DEV_REMOVED: %u.%09u devidx:%x\n",
		       sec, nsec, data1);
		break;
	case AUDIO_THREAD_IODEV_CB:
		printf("IODEV_CB: %u.%09u is_write:%u\n", sec, nsec, data1);
		break;
	case AUDIO_THREAD_PB_MSG:
		printf("PB_MSG: %u.%09u msg_id:%u\n", sec, nsec, data1);
		break;
	case AUDIO_THREAD_ODEV_NO_STREAMS:
		printf("ODEV_NO_STREAMS: %u.%09u id:%u hw_level:%u cb_lev:%u\n",
		       sec, nsec, data1, data2, data3);
		break;
	default:
		printf("Unknown alog tag %u\n", tag);
		break;
	}
}

static void audio_debug_info(struct cras_client *client)
{
	const struct audio_debug_info *info;
	int i, j;

	info = cras_client_get_audio_debug_info(client);
	if (!info)
		return;

	printf("Audio Debug Stats:\n");
	printf("-------------devices------------\n");
	if (info->num_devs > MAX_DEBUG_DEVS)
		return;

	for (i = 0; i < info->num_devs; i++) {
		printf("%s dev: %s\n",
		       (info->devs[i].direction == CRAS_STREAM_INPUT)
				? "Input" : "Output",
		       info->devs[i].dev_name);
		printf("%u %u %u %u %u %u %lf\n",
		       (unsigned int)info->devs[i].buffer_size,
		       (unsigned int)info->devs[i].min_buffer_level,
		       (unsigned int)info->devs[i].min_cb_level,
		       (unsigned int)info->devs[i].max_cb_level,
		       (unsigned int)info->devs[i].frame_rate,
		       (unsigned int)info->devs[i].num_channels,
		       info->devs[i].est_rate_ratio);
	}

	printf("-------------stream_dump------------\n");
	if (info->num_streams > MAX_DEBUG_STREAMS)
		return;

	for (i = 0; i < info->num_streams; i++) {
		int channel;
		printf("stream: %llx dev: %x\n",
		       (unsigned long long)info->streams[i].stream_id,
		       (unsigned int)info->streams[i].dev_idx);
		printf("%d %u %u %u %u %u.%09u\n",
		       info->streams[i].direction,
		       (unsigned int)info->streams[i].buffer_frames,
		       (unsigned int)info->streams[i].cb_threshold,
		       (unsigned int)info->streams[i].frame_rate,
		       (unsigned int)info->streams[i].num_channels,
		       (unsigned int)info->streams[i].longest_fetch_sec,
		       (unsigned int)info->streams[i].longest_fetch_nsec);
		for (channel = 0; channel < CRAS_CH_MAX; channel++)
			printf("%d ", info->streams[i].channel_layout[channel]);
		printf("\n");
	}

	printf("Audio Thread Event Log:\n");

	j = info->log.write_pos;
	i = 0;
	printf("start at %d\n", j);
	for (; i < info->log.len; i++) {
		show_alog_tag(&info->log, j);
		j++;
		j %= info->log.len;
	}

	/* Signal main thread we are done after the last chunk. */
	pthread_mutex_lock(&done_mutex);
	pthread_cond_signal(&done_cond);
	pthread_mutex_unlock(&done_mutex);
}

static int start_stream(struct cras_client *client,
			cras_stream_id_t *stream_id,
			struct cras_stream_params *params,
			float stream_volume)
{
	int rc;

	if (pin_device_id)
		rc = cras_client_add_pinned_stream(client, pin_device_id,
						   stream_id, params);
	else
		rc = cras_client_add_stream(client, stream_id, params);
	if (rc < 0) {
		fprintf(stderr, "adding a stream %d\n", rc);
		return rc;
	}
	return cras_client_set_stream_volume(client, *stream_id, stream_volume);
}

static int parse_channel_layout(char *channel_layout_str,
				int8_t channel_layout[CRAS_CH_MAX])
{
	int i = 0;
	char *chp;

	chp = strtok(channel_layout_str, ",");
	while (chp && i < CRAS_CH_MAX) {
		channel_layout[i++] = atoi(chp);
		chp = strtok(NULL, ",");
	}

	return 0;
}

static int run_file_io_stream(struct cras_client *client,
			      int fd,
			      enum CRAS_STREAM_DIRECTION direction,
			      size_t block_size,
			      size_t rate,
			      size_t num_channels,
			      uint32_t flags,
			      int is_loopback)
{
	int rc, tty;
	struct cras_stream_params *params;
	cras_unified_cb_t aud_cb;
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
	int8_t layout[CRAS_CH_MAX];

	/* Set the sleep interval between latency/RMS prints. */
	sleep_ts.tv_sec = 1;
	sleep_ts.tv_nsec = 0;

	/* Open the pipe file descriptor. */
	rc = pipe(pipefd);
	if (rc == -1) {
		perror("failed to open pipe");
		return -errno;
	}

	/* Reset the total RMS value. */
	total_rms_sqr_sum = 0;
	total_rms_size = 0;

	if (direction == CRAS_STREAM_INPUT) {
		if (flags == HOTWORD_STREAM)
			aud_cb = got_hotword;
		else
			aud_cb = got_samples;
	} else {
		aud_cb = put_samples;
	}

	if (fd == 0) {
		if (direction != CRAS_STREAM_OUTPUT)
			return -EINVAL;
		aud_cb = put_stdin_samples;
	}

	aud_format = cras_audio_format_create(SND_PCM_FORMAT_S16_LE, rate,
					      num_channels);
	if (aud_format == NULL)
		return -ENOMEM;

	if (channel_layout) {
		/* Set channel layout to format */
		parse_channel_layout(channel_layout, layout);
		cras_audio_format_set_channel_layout(aud_format, layout);
	}

	params = cras_client_unified_params_create(direction,
						   block_size,
						   0,
						   flags,
						   pfd,
						   aud_cb,
						   stream_error,
						   aud_format);
	if (params == NULL)
		return -ENOMEM;

	cras_client_run_thread(client);
	if (is_loopback) {
		cras_client_connected_wait(client);
		pin_device_id = cras_client_get_first_dev_type_idx(client,
				CRAS_NODE_TYPE_POST_MIX_PRE_DSP,
				CRAS_STREAM_INPUT);
	}

	stream_playing =
		start_stream(client, &stream_id, params, volume_scaler) == 0;

	tty = open("/dev/tty", O_RDONLY);

	// There could be no terminal available when run in autotest.
	if (tty == -1)
		perror("warning: failed to open /dev/tty");

	while (keep_looping) {
		char input;
		int nread;

		FD_ZERO(&poll_set);
		if (tty >= 0)
			FD_SET(tty, &poll_set);
		FD_SET(pipefd[0], &poll_set);
		pselect(MAX(tty, pipefd[0]) + 1,
			&poll_set,
			NULL,
			NULL,
			show_latency || show_rms ? &sleep_ts : NULL,
			NULL);

		if (stream_playing && show_latency)
			print_last_latency();

		if (stream_playing && show_rms)
			print_last_rms();

		if (tty < 0 || !FD_ISSET(tty, &poll_set))
			continue;

		nread = read(tty, &input, 1);
		if (nread < 1) {
			fprintf(stderr, "Error reading stdin\n");
			return nread;
		}
		switch (input) {
		case 'p':
			pause_client = !pause_client;
			break;
		case 'q':
			terminate_stream_loop();
			break;
		case 's':
			if (stream_playing)
				break;

			/* If started by hand keep running after it finishes. */
			exit_after_done_playing = 0;

			stream_playing = start_stream(client,
						      &stream_id,
						      params,
						      volume_scaler) == 0;
			break;
		case 'r':
			if (!stream_playing)
				break;
			cras_client_rm_stream(client, stream_id);
			stream_playing = 0;
			break;
		case 'u':
			volume_scaler = MIN(volume_scaler + 0.1, 1.0);
			cras_client_set_stream_volume(client,
						      stream_id,
						      volume_scaler);
			break;
		case 'd':
			volume_scaler = MAX(volume_scaler - 0.1, 0.0);
			cras_client_set_stream_volume(client,
						      stream_id,
						      volume_scaler);
			break;
		case 'k':
			sys_volume = MIN(sys_volume + 1, 100);
			cras_client_set_system_volume(client, sys_volume);
			break;
		case 'j':
			sys_volume = sys_volume == 0 ? 0 : sys_volume - 1;
			cras_client_set_system_volume(client, sys_volume);
			break;
		case 'K':
			cap_gain = MIN(cap_gain + 100, 5000);
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

	if (show_total_rms)
		print_total_rms();

	cras_client_stop(client);

	cras_audio_format_destroy(aud_format);
	cras_client_stream_params_destroy(params);
	free(pfd);

	close(pipefd[0]);
	close(pipefd[1]);

	return 0;
}

static int run_capture(struct cras_client *client,
		       const char *file,
		       size_t block_size,
		       size_t rate,
		       size_t num_channels,
		       int is_loopback)
{
	int fd = open(file, O_CREAT | O_RDWR | O_TRUNC, 0666);
	if (fd == -1) {
		perror("failed to open file");
		return -errno;
	}

	run_file_io_stream(client, fd, CRAS_STREAM_INPUT, block_size, rate,
			   num_channels, 0, is_loopback);

	close(fd);
	return 0;
}

static int run_playback(struct cras_client *client,
			const char *file,
			size_t block_size,
			size_t rate,
			size_t num_channels)
{
	int fd;

	fd = open(file, O_RDONLY);
	if (fd == -1) {
		perror("failed to open file");
		return -errno;
	}

	run_file_io_stream(client, fd, CRAS_STREAM_OUTPUT,
			   block_size, rate, num_channels, 0, 0);

	close(fd);
	return 0;
}

static int run_hotword(struct cras_client *client,
		       size_t block_size,
		       size_t rate)
{
	run_file_io_stream(client, -1, CRAS_STREAM_INPUT, block_size, rate, 1,
			   HOTWORD_STREAM, 0);
	return 0;
}
static void print_server_info(struct cras_client *client)
{
	cras_client_run_thread(client);
	cras_client_connected_wait(client); /* To synchronize data. */
	print_system_volumes(client);
	print_device_lists(client);
	print_attached_client_list(client);
	print_active_stream_info(client);
}

static void print_audio_debug_info(struct cras_client *client)
{
	struct timespec wait_time;

	cras_client_run_thread(client);
	cras_client_connected_wait(client); /* To synchronize data. */
	cras_client_update_audio_debug_info(client, audio_debug_info);

	clock_gettime(CLOCK_REALTIME, &wait_time);
	wait_time.tv_sec += 2;

	pthread_mutex_lock(&done_mutex);
	pthread_cond_timedwait(&done_cond, &done_mutex, &wait_time);
	pthread_mutex_unlock(&done_mutex);
}

static void check_output_plugged(struct cras_client *client, const char *name)
{
	cras_client_run_thread(client);
	cras_client_connected_wait(client); /* To synchronize data. */
	printf("%s\n",
	       cras_client_output_dev_plugged(client, name) ? "Yes" : "No");
}

static void init_sbc_codec()
{
	capture_codec = cras_sbc_codec_create(SBC_FREQ_16000,
					      SBC_MODE_DUAL_CHANNEL,
					      SBC_SB_4,
					      SBC_AM_LOUDNESS,
					      SBC_BLK_8,
					      53);
	playback_codec = cras_sbc_codec_create(SBC_FREQ_16000,
					       SBC_MODE_DUAL_CHANNEL,
					       SBC_SB_4,
					       SBC_AM_LOUDNESS,
					       SBC_BLK_8,
					       53);
}

static struct option long_options[] = {
	{"show_latency",	no_argument, &show_latency, 1},
	{"show_rms",            no_argument, &show_rms, 1},
	{"show_total_rms",      no_argument, &show_total_rms, 1},
	{"select_input",        required_argument,      0, 'a'},
	{"block_size",		required_argument,	0, 'b'},
	{"capture_file",	required_argument,	0, 'c'},
	{"duration_seconds",	required_argument,	0, 'd'},
	{"sbc",                 no_argument,            0, 'e'},
	{"dump_dsp",            no_argument,            0, 'f'},
	{"capture_gain",        required_argument,      0, 'g'},
	{"help",                no_argument,            0, 'h'},
	{"dump_server_info",    no_argument,            0, 'i'},
	{"check_output_plugged",required_argument,      0, 'j'},
	{"add_active_input",	required_argument,	0, 'k'},
	{"add_active_output",	required_argument,	0, 't'},
	{"loopback_file",	required_argument,	0, 'l'},
	{"dump_audio_thread",   no_argument,            0, 'm'},
	{"num_channels",        required_argument,      0, 'n'},
	{"channel_layout",      required_argument,      0, 'o'},
	{"playback_file",	required_argument,	0, 'p'},
	{"user_mute",           required_argument,      0, 'q'},
	{"rate",		required_argument,	0, 'r'},
	{"reload_dsp",          no_argument,            0, 's'},
	{"mute",                required_argument,      0, 'u'},
	{"volume",              required_argument,      0, 'v'},
	{"set_node_volume",	required_argument,      0, 'w'},
	{"plug",                required_argument,      0, 'x'},
	{"select_output",       required_argument,      0, 'y'},
	{"capture_mute",        required_argument,      0, '0'},
	{"rm_active_input",	required_argument,	0, '1'},
	{"rm_active_output",	required_argument,	0, '2'},
	{"swap_left_right",     required_argument,      0, '3'},
	{"version",             no_argument,            0, '4'},
	{"add_test_dev",        required_argument,      0, '5'},
	{"test_hotword_file",   required_argument,      0, '6'},
	{"listen_for_hotword",  no_argument,            0, '7'},
	{"pin_device",		required_argument,	0, '8'},
	{"suspend",		required_argument,	0, '9'},
	{0, 0, 0, 0}
};

static void show_usage()
{
	printf("--add_active_input <N>:<M> - Add the ionode with the given id"
	       "to active input device list\n");
	printf("--add_active_output <N>:<M> - Add the ionode with the given id"
	       "to active output device list\n");
	printf("--add_test_dev <type> - add a test iodev.\n");
	printf("--block_size <N> - The number for frames per callback(dictates latency).\n");
	printf("--capture_file <name> - Name of file to record to.\n");
	printf("--capture_gain <dB> - Set system caputre gain in dB*100 (100 = 1dB).\n");
	printf("--capture_mute <0|1> - Set capture mute state.\n");
	printf("--channel_layout <layout_str> - Set multiple channel layout.\n");
	printf("--check_output_plugged <output name> - Check if the output is plugged in\n");
	printf("--dump_audio_thread - Dumps audio thread info.\n");
	printf("--dump_dsp - Print status of dsp to syslog.\n");
	printf("--dump_server_info - Print status of the server.\n");
	printf("--duration_seconds <N> - Seconds to record or playback.\n");
	printf("--help - Print this message.\n");
	printf("--listen_for_hotword - Listen for a hotword if supported\n");
	printf("--loopback_file <name> - Name of file to record loopback to.\n");
	printf("--mute <0|1> - Set system mute state.\n");
	printf("--num_channels <N> - Two for stereo.\n");
	printf("--pin_device <N> - Playback/Capture only on the given device."
	       "\n");
	printf("--playback_file <name> - Name of file to play, "
	       "\"-\" to playback raw audio from stdin.\n");
	printf("--plug <N>:<M>:<0|1> - Set the plug state (0 or 1) for the"
	       " ionode with the given index M on the device with index N\n");
	printf("--rate <N> - Specifies the sample rate in Hz.\n");
	printf("--reload_dsp - Reload dsp configuration from the ini file\n");
	printf("--rm_active_input <N>:<M> - Removes the ionode with the given"
	       "id from active input device list\n");
	printf("--rm_active_output <N>:<M> - Removes the ionode with the given"
	       "id from active output device list\n");
	printf("--sbc - Use sbc codec for playback/capture.\n");
	printf("--select_input <N>:<M> - Select the ionode with the given id as preferred input\n");
	printf("--select_output <N>:<M> - Select the ionode with the given id as preferred output\n");
	printf("--set_node_volume <N>:<M>:<0-100> - Set the volume of the ionode with the given id\n");
	printf("--show_latency - Display latency while playing or recording.\n");
	printf("--show_rms - Display RMS value of loopback stream.\n");
	printf("--show_total_rms - Display total RMS value of loopback stream at the end.\n");
	printf("--suspend <0|1> - Set audio suspend state.\n");
	printf("--swap_left_right <N>:<M>:<0|1> - Swap or unswap (1 or 0) the"
	       " left and right channel for the ionode with the given index M"
	       " on the device with index N\n");
	printf("--test_hotword_file <N>:<filename> - Use filename as a hotword buffer for device N\n");
	printf("--user_mute <0|1> - Set user mute state.\n");
	printf("--version - Print the git commit ID that was used to build the client.\n");
	printf("--volume <0-100> - Set system output volume.\n");
}

int main(int argc, char **argv)
{
	struct cras_client *client;
	int c, option_index;
	size_t block_size = NOT_ASSIGNED;
	size_t rate = 48000;
	size_t num_channels = 2;
	float duration_seconds = 0;
	const char *capture_file = NULL;
	const char *playback_file = NULL;
	const char *loopback_file = NULL;
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
		case 'e':
			init_sbc_codec();
			break;
		case 'p':
			playback_file = optarg;
			break;
		case 'l':
			loopback_file = optarg;
			break;
		case 'b':
			block_size = atoi(optarg);
			break;
		case 'r':
			rate = atoi(optarg);
			break;
		case 'n':
			num_channels = atoi(optarg);
			break;
		case 'd':
			duration_seconds = atof(optarg);
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
		case 'q': {
			int mute = atoi(optarg);
			rc = cras_client_set_user_mute(client, mute);
			if (rc < 0) {
				fprintf(stderr, "problem setting mute\n");
				goto destroy_exit;
			}
			break;
		}
		case 'v': {
			int volume = atoi(optarg);
			volume = MIN(100, MAX(0, volume));
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
		case 'j':
			check_output_plugged(client, optarg);
			break;
		case 's':
			cras_client_reload_dsp(client);
			break;
		case 'f':
			cras_client_dump_dsp_info(client);
			break;
		case 'i':
			print_server_info(client);
			break;
		case 'h':
			show_usage();
			break;
		case 'x': {
			int dev_index = atoi(strtok(optarg, ":"));
			int node_index = atoi(strtok(NULL, ":"));
			int value = atoi(strtok(NULL, ":")) ;
			cras_node_id_t id = cras_make_node_id(dev_index,
							      node_index);
			enum ionode_attr attr = IONODE_ATTR_PLUGGED;
			cras_client_set_node_attr(client, id, attr, value);
			break;
		}
		case 'y':
		case 'a': {
			int dev_index = atoi(strtok(optarg, ":"));
			int node_index = atoi(strtok(NULL, ":"));
			cras_node_id_t id = cras_make_node_id(dev_index,
							      node_index);

			enum CRAS_STREAM_DIRECTION direction = (c == 'y') ?
				CRAS_STREAM_OUTPUT : CRAS_STREAM_INPUT;
			cras_client_select_node(client, direction, id);
			break;
		}
		case 'k':
		case 't':
		case '1':
		case '2':{
			int dev_index = atoi(strtok(optarg, ":"));
			int node_index = atoi(strtok(NULL, ":"));
			enum CRAS_STREAM_DIRECTION dir;
			cras_node_id_t id = cras_make_node_id(dev_index,
							      node_index);

			if (c == 't' || c == '2')
				dir = CRAS_STREAM_OUTPUT;
			else
				dir = CRAS_STREAM_INPUT;

			if (c == 'k' || c == 't')
				cras_client_add_active_node(client, dir, id);
			else
				cras_client_rm_active_node(client, dir, id);
			break;
		}
		case 'w': {
			const char *s;
			int dev_index;
			int node_index;
			int value;

			s = strtok(optarg, ":");
			if (!s) {
				show_usage();
				return -EINVAL;
			}
			dev_index = atoi(s);

			s = strtok(NULL, ":");
			if (!s) {
				show_usage();
				return -EINVAL;
			}
			node_index = atoi(s);

			s = strtok(NULL, ":");
			if (!s) {
				show_usage();
				return -EINVAL;
			}
			value = atoi(s) ;

			cras_node_id_t id = cras_make_node_id(dev_index,
							      node_index);

			cras_client_set_node_volume(client, id, value);
			break;
		}
		case '0': {
			int mute = atoi(optarg);
			rc = cras_client_set_system_capture_mute(client, mute);
			if (rc < 0) {
				fprintf(stderr, "problem setting mute\n");
				goto destroy_exit;
			}
			break;
		}
		case 'm':
			print_audio_debug_info(client);
			break;
		case 'o':
			channel_layout = optarg;
			break;
		case '3': {
			int dev_index = atoi(strtok(optarg, ":"));
			int node_index = atoi(strtok(NULL, ":"));
			int value = atoi(strtok(NULL, ":")) ;
			cras_node_id_t id = cras_make_node_id(dev_index,
							      node_index);
			cras_client_swap_node_left_right(client, id, value);
			break;
		}
		case '4':
			printf("%s\n", VCSID);
			break;
		case '5': {
			cras_client_add_test_iodev(client, atoi(optarg));
			break;
		}
		case '6': {
			int dev_index = atoi(strtok(optarg, ":"));
			const char *file_name = strtok(NULL, ":");
			cras_client_test_iodev_command(client, dev_index,
					TEST_IODEV_CMD_HOTWORD_TRIGGER,
					strlen(file_name) + 1,
					(uint8_t *)file_name);
			break;
		}
		case '7': {
			run_hotword(client, 4096, 16000);
			break;
		}
		case '8':
			pin_device_id = atoi(optarg);
			break;
		case '9': {
			int suspend = atoi(optarg);
			cras_client_set_suspend(client, suspend);
			break;
		}
		default:
			break;
		}
	}

	duration_frames = duration_seconds * rate;
	if (block_size == NOT_ASSIGNED)
		block_size = get_block_size(PLAYBACK_BUFFERED_TIME_IN_US, rate);

	if (capture_file != NULL) {
		if (strcmp(capture_file, "-") == 0)
			rc = run_file_io_stream(client, 1, CRAS_STREAM_INPUT,
					block_size, rate, num_channels, 0, 0);
		else
			rc = run_capture(client, capture_file,
					block_size, rate, num_channels, 0);
	} else if (playback_file != NULL) {
		if (strcmp(playback_file, "-") == 0)
			rc = run_file_io_stream(client, 0, CRAS_STREAM_OUTPUT,
					block_size, rate, num_channels, 0, 0);
		else
			rc = run_playback(client, playback_file,
					block_size, rate, num_channels);
	} else if (loopback_file != NULL) {
		rc = run_capture(client, loopback_file,
				 block_size, rate, num_channels, 1);
	}

destroy_exit:
	cras_client_destroy(client);
	if (capture_codec)
		cras_sbc_codec_destroy(capture_codec);
	if (playback_codec)
		cras_sbc_codec_destroy(playback_codec);
	return rc;
}
