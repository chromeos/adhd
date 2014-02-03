/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Basic playback flow:
 *  cras_client_create - Create new structure and set to defaults.
 *  cras_client_connect - Connect client to server - sets up server_fd to
 *    communicate with the audio server.  After the client connects, the server
 *    will send back a message containing the client id.
 *  cras_client_add_stream - Add a playback or capture stream. Creates a
 *    client_stream struct and send a file descriptor to server. That file
 *    descriptor and aud_fd are a pair created from socketpair().
 *  client_connected - The server will send a connected message to indicate that
 *    the client should start receving audio events from aud_fd. This message
 *    also specifies the shared memory region to use to share audio samples.
 *    This region will be shmat'd.
 *  running - Once the connections are established, the client will listen for
 *    requests on aud_fd and fill the shm region with the requested number of
 *    samples. This happens in the aud_cb specified in the stream parameters.
 */

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

#include "cras_client.h"
#include "cras_config.h"
#include "cras_fmt_conv.h"
#include "cras_messages.h"
#include "cras_shm.h"
#include "cras_types.h"
#include "cras_util.h"
#include "utlist.h"

static const size_t MAX_CMD_MSG_LEN = 256;
static const size_t SERVER_CONNECT_TIMEOUT_US = 500000;
static const size_t SERVER_SHUTDOWN_TIMEOUT_US = 500000;
static const size_t SERVER_FIRST_MESSAGE_TIMEOUT_US = 500000;

/* Commands sent from the user to the running client. */
enum {
	CLIENT_STOP,
	CLIENT_ADD_STREAM,
	CLIENT_REMOVE_STREAM,
	CLIENT_SET_STREAM_VOLUME_SCALER,
	CLIENT_SERVER_CONNECT,
};

struct command_msg {
	unsigned len;
	unsigned msg_id;
	cras_stream_id_t stream_id;
};

struct set_stream_volume_command_message {
	struct command_msg header;
	float volume_scaler;
};

/* Adds a stream to the client.
 *  stream - The stream to add.
 *  stream_id_out - Filled with the stream id of the new stream.
 */
struct add_stream_command_message {
	struct command_msg header;
	struct client_stream *stream;
	cras_stream_id_t *stream_id_out;
};

/* Commands send from a running stream to the client. */
enum {
	CLIENT_STREAM_EOF,
};

struct stream_msg {
	unsigned msg_id;
	cras_stream_id_t stream_id;
};

/* Manage information for a thread. */
struct thread_state {
	pthread_t tid;
	unsigned  running;
};

/* Parameters used when setting up a capture or playback stream. See comment
 * above cras_client_create_stream_params in the header for descriptions. */
struct cras_stream_params {
	enum CRAS_STREAM_DIRECTION direction;
	size_t buffer_frames;
	size_t cb_threshold;
	size_t min_cb_level;
	enum CRAS_STREAM_TYPE stream_type;
	uint32_t flags;
	void *user_data;
	cras_playback_cb_t aud_cb;
	cras_unified_cb_t unified_cb;
	cras_error_cb_t err_cb;
	struct cras_audio_format format;
};

/* Represents an attached audio stream.
 * id - Unique stream identifier.
 * aud_fd - After server connects audio messages come in here.
 * direction - playback, capture, both, or loopback (see CRAS_STREAM_DIRECTION).
 * flags - Currently not used.
 * volume_scaler - Amount to scale the stream by, 0.0 to 1.0.
 * tid - Thread id of the audio thread spawned for this stream.
 * running - Audio thread runs while this is non-zero.
 * wake_fds - Pipe to wake the audio thread.
 * client - The client this stream is attached to.
 * config - Audio stream configuration.
 * capture_shm - Shared memory used to exchange audio samples with the server.
 * play_shm - Shared memory used to exchange audio samples with the server.
 * play_conv - Format converter, if the server's audio format doesn't match.
 * play_conv_buffer - Buffer used to store samples before sending for format
 *     conversion.
 * capture_conv - Format converter for capture stream.
 * capture_conv_buffer - Buffer used to store captured samples before sending
 *     for format conversion.
 * prev, next - Form a linked list of streams attached to a client.
 */
struct client_stream {
	cras_stream_id_t id;
	int aud_fd; /* audio messages from server come in here. */
	enum CRAS_STREAM_DIRECTION direction;
	uint32_t flags;
	float volume_scaler;
	struct thread_state thread;
	int wake_fds[2]; /* Pipe to wake the thread */
	struct cras_client *client;
	struct cras_stream_params *config;
	struct cras_audio_shm capture_shm;
	struct cras_audio_shm play_shm;
	struct cras_fmt_conv *play_conv;
	uint8_t *play_conv_buffer;
	struct cras_fmt_conv *capture_conv;
	uint8_t *capture_conv_buffer;
	struct client_stream *prev, *next;
};

/* Represents a client used to communicate with the audio server.
 * id - Unique identifier for this client, negative until connected.
 * server_fd Incoming messages from server.
 * stream_fds - Pipe for attached streams.
 * command_fds - Pipe for user commands to thread.
 * command_reply_fds - Pipe for acking/nacking command messages from thread.
 * sock_dir - Directory where the local audio socket can be found.
 * running - The client thread will run while this is non zero.
 * next_stream_id - ID to give the next stream.
 * tid - Thread ID of the client thread started by "cras_client_run_thread".
 * last_command_result - Passes back the result of the last user command.
 * streams - Linked list of streams attached to this client.
 * server_state - RO shared memory region holding server state.
 * debug_info_callback - Function to call when debug info is received.
 */
struct cras_client {
	int id;
	int server_fd;
	int stream_fds[2];
	int command_fds[2];
	int command_reply_fds[2];
	const char *sock_dir;
	struct thread_state thread;
	cras_stream_id_t next_stream_id;
	int last_command_result;
	struct client_stream *streams;
	const struct cras_server_state *server_state;
	void (*debug_info_callback)(struct cras_client *);
};

/*
 * Local Helpers
 */

static int handle_message_from_server(struct cras_client *client);

/* Get the stream pointer from a stream id. */
static struct client_stream *stream_from_id(const struct cras_client *client,
					    unsigned int id)
{
	struct client_stream *out;

	DL_SEARCH_SCALAR(client->streams, out, id, id);
	return out;
}

/* Waits until we have heard back from the server so that we know we are
 * connected.  The connected success/failure message is always the first message
 * the server sends. Return non zero if client is connected to the server. A
 * return code of zero means that the client is not connected to the server. */
static int check_server_connected_wait(struct cras_client *client)
{
	fd_set poll_set;
	int rc;
	int fd = client->server_fd;
	struct timeval timeout;

	timeout.tv_sec = 0;
	timeout.tv_usec = SERVER_FIRST_MESSAGE_TIMEOUT_US;

	while (timeout.tv_usec > 0 && client->id < 0) {
		FD_ZERO(&poll_set);
		FD_SET(fd, &poll_set);
		rc = select(fd + 1, &poll_set, NULL, NULL, &timeout);
		if (rc <= 0)
			return 0; /* Timeout or error. */
		if (FD_ISSET(fd, &poll_set)) {
			rc = handle_message_from_server(client);
			if (rc < 0)
				return 0;
		}
	}

	return client->id >= 0;
}

/* Waits until the fd is writable or the specified time has passed. Returns 0 if
 * the fd is writable, -1 for timeout or other error. */
static int wait_until_fd_writable(int fd, int timeout_us)
{
	struct timeval timeout;
	fd_set poll_set;
	int rc;

	timeout.tv_sec = 0;
	timeout.tv_usec = timeout_us;

	FD_ZERO(&poll_set);
	FD_SET(fd, &poll_set);
	rc = select(fd + 1, NULL, &poll_set, NULL, &timeout);

	if (rc <= 0)
		return -1;
	return 0;
}

/* Opens the server socket and connects to it. */
static int connect_to_server(struct cras_client *client)
{
	int rc;
	struct sockaddr_un address;

	if (client->server_fd >= 0)
		close(client->server_fd);
	client->server_fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
	if (client->server_fd < 0) {
		syslog(LOG_ERR, "%s: Socket failed.", __func__);
		return client->server_fd;
	}

	memset(&address, 0, sizeof(struct sockaddr_un));

	address.sun_family = AF_UNIX;
	client->sock_dir = cras_config_get_system_socket_file_dir();
	assert(client->sock_dir);
	snprintf(address.sun_path, sizeof(address.sun_path),
		 "%s/%s", client->sock_dir, CRAS_SOCKET_FILE);

	/* We make the file descriptor non-blocking when we do connect(), so we
	 * don't block indifinitely. */
	cras_make_fd_nonblocking(client->server_fd);
	rc = connect(client->server_fd, (struct sockaddr *)&address,
		     sizeof(struct sockaddr_un));

	if (rc == -1 && errno == EINPROGRESS) {
		rc = wait_until_fd_writable(client->server_fd,
					    SERVER_CONNECT_TIMEOUT_US);
	}

	cras_make_fd_blocking(client->server_fd);

	if (rc != 0) {
		close(client->server_fd);
		client->server_fd = -1;
		syslog(LOG_ERR, "%s: Connect server failed.", __func__);
	}

	return rc;
}

/* Tries to connect to the server.  Waits for the initial message from the
 * server.  This will happen near instantaneously if the server is already
 * running.*/
static int connect_to_server_wait(struct cras_client *client)
{
	unsigned int retries = 4;
	const unsigned int retry_delay_ms = 200;

	assert(client);

	/* Ignore sig pipe as it will be handled when we write to the socket. */
	signal(SIGPIPE, SIG_IGN);

	while (--retries) {
		/* If connected, wait for the first message from the server
		 * indicating it's ready. */
		if (connect_to_server(client) == 0 &&
		    check_server_connected_wait(client))
				return 0;

		/* If we didn't succeed, wait and try again. */
		usleep(retry_delay_ms * 1000);
	}

	return -EIO;
}

/*
 * Audio thread.
 */

/* Sends a message from the stream to the client to indicate an error.
 * If the running stream encounters an error, then it must tell the client
 * to stop running it.
 */
static int send_stream_message(const struct client_stream *stream,
			       unsigned msg_id)
{
	int res;
	struct stream_msg msg;

	msg.stream_id = stream->id;
	msg.msg_id = msg_id;
	res = write(stream->client->stream_fds[1], &msg, sizeof(msg));
	if (res != sizeof(msg))
		return -EPIPE;

	return 0;
}

/* Blocks until there is data to be read from the read_fd or until woken by an
 * incoming "poke" on wake_fd. Up to "len" bytes are read into "buf". */
static int read_with_wake_fd(int wake_fd, int read_fd, uint8_t *buf, size_t len)
{
	fd_set poll_set;
	int nread = 0;
	int rc, max_fd;
	char tmp;

	FD_ZERO(&poll_set);
	FD_SET(read_fd, &poll_set);
	FD_SET(wake_fd, &poll_set);
	max_fd = max(read_fd, wake_fd);

	rc = pselect(max_fd + 1, &poll_set, NULL, NULL, NULL, NULL);
	if (rc < 0)
		return rc;
	if (FD_ISSET(read_fd, &poll_set)) {
		nread = read(read_fd, buf, len);
		if (nread != (int)len)
			return -EIO;
	}
	if (FD_ISSET(wake_fd, &poll_set)) {
		rc = read(wake_fd, &tmp, 1);
		if (rc < 0)
			return rc;
	}

	return nread;
}

/* Check if doing format conversion and configure a caprute buffer appropriately
 * before passing to the client. */
static unsigned int config_capture_buf(struct client_stream *stream,
				       uint8_t **captured_frames,
				       unsigned int num_frames)
{
	*captured_frames = cras_shm_get_curr_read_buffer(&stream->capture_shm);

	/* If we need to do format conversion convert to the temporary
	 * buffer and pass the converted samples to the client. */
	if (stream->capture_conv) {
		num_frames = cras_fmt_conv_convert_frames(
				stream->capture_conv,
				*captured_frames,
				stream->capture_conv_buffer,
				num_frames,
				stream->config->buffer_frames);
		*captured_frames = stream->capture_conv_buffer;
	}

	/* Don't ask for more frames than the client desires. */
	num_frames = min(num_frames, stream->config->min_cb_level);

	return num_frames;
}

/* If doing format conversion, configure that, and configure a buffer to write
 * audio into. */
static unsigned int config_playback_buf(struct client_stream *stream,
					uint8_t **playback_frames,
					unsigned int num_frames)
{
	unsigned int limit;
	struct cras_audio_shm *shm = &stream->play_shm;

	/* Assert num_frames doesn't exceed shm limit, no matter
	 * format conversion is needed of not. */
	*playback_frames = cras_shm_get_writeable_frames(
			shm, cras_shm_used_frames(shm), &limit);
	num_frames = min(num_frames, limit);

	/* If we need to do format conversion on this stream, change to
	 * use an intermediate buffer to store the samples so they can be
	 * converted. */
	if (stream->play_conv) {
		*playback_frames = stream->play_conv_buffer;

		/* Recalculate the frames we'd want to request from client
		 * according to sample rate change. */
		num_frames = cras_fmt_conv_out_frames_to_in(
				stream->play_conv,
				num_frames);
	}

	/* Don't ask for more frames than the client desires. */
	num_frames = min(num_frames, stream->config->min_cb_level);

	return num_frames;
}

/* Marks 'num_frames' samples read in the shm are for capture. */
static void complete_capture_read(struct client_stream *stream,
				  unsigned int num_frames)
{
	unsigned int frames = num_frames;

	if (stream->capture_conv)
		frames = cras_fmt_conv_out_frames_to_in(stream->capture_conv,
							num_frames);
	cras_shm_buffer_read(&stream->capture_shm, frames);
}

static void complete_capture_read_current(struct client_stream *stream,
					  unsigned int num_frames)
{
	unsigned int frames = num_frames;

	if (stream->capture_conv)
		frames = cras_fmt_conv_out_frames_to_in(stream->capture_conv,
							num_frames);
	cras_shm_buffer_read_current(&stream->capture_shm, frames);
}

/* For capture streams this handles the message signalling that data is ready to
 * be passed to the user of this stream.  Calls the audio callback with the new
 * samples, and mark them as read.
 * Args:
 *    stream - The stream the message was received for.
 *    num_frames - The number of captured frames.
 * Returns:
 *    0, unless there is a fatal error or the client declares enod of file.
 */
static int handle_capture_data_ready(struct client_stream *stream,
				     unsigned int num_frames)
{
	int frames;
	struct cras_stream_params *config;
	uint8_t *captured_frames;
	struct timespec ts;

	config = stream->config;
	/* If this message is for an output stream, log error and drop it. */
	if (!cras_stream_has_input(stream->direction)) {
		syslog(LOG_ERR, "Play data to input\n");
		return 0;
	}

	num_frames = config_capture_buf(stream, &captured_frames, num_frames);
	cras_timespec_to_timespec(&ts, &stream->capture_shm.area->ts);

	if (config->unified_cb)
		frames = config->unified_cb(stream->client,
					    stream->id,
					    captured_frames,
					    NULL,
					    num_frames,
					    &ts,
					    NULL,
					    config->user_data);
	else
		frames = config->aud_cb(stream->client,
					stream->id,
					captured_frames,
					num_frames,
					&ts,
					config->user_data);
	if (frames == EOF) {
		send_stream_message(stream, CLIENT_STREAM_EOF);
		return EOF;
	}
	if (frames == 0)
		return 0;

	complete_capture_read_current(stream, frames);
	return 0;
}

/* Handles any format conversion that is necessary for newly written samples and
 * marks them as written to shm. */
static void complete_playback_write(struct client_stream *stream,
				    unsigned int frames)
{
	struct cras_audio_shm *shm = &stream->play_shm;

	/* Possibly convert to the correct format. */
	if (stream->play_conv) {
		uint8_t *final_buf;
		unsigned limit;

		final_buf = cras_shm_get_writeable_frames(
				shm, cras_shm_used_frames(shm), &limit);

		frames = cras_fmt_conv_convert_frames(
				stream->play_conv,
				stream->play_conv_buffer,
				final_buf,
				frames,
				limit);
	}
	/* And move the write pointer to indicate samples written. */
	cras_shm_buffer_written(shm, frames);
	cras_shm_buffer_write_complete(shm);
}

/* Notifies the server that "frames" samples have been written. */
static int send_playback_reply(struct client_stream *stream,
			       unsigned int frames,
			       int error)
{
	struct audio_message aud_msg;
	int rc;

	if (!cras_stream_uses_output_hw(stream->direction))
		return 0;

	aud_msg.id = AUDIO_MESSAGE_DATA_READY;
	aud_msg.frames = frames;
	aud_msg.error = error;

	rc = write(stream->aud_fd, &aud_msg, sizeof(aud_msg));
	if (rc != sizeof(aud_msg))
		return -EPIPE;

	return 0;
}

/* For playback streams when current buffer is empty, this handles the request
 * for more samples by calling the audio callback for the thread, and signaling
 * the server that the samples have been written. */
static int handle_playback_request(struct client_stream *stream,
				   unsigned int num_frames)
{
	uint8_t *buf;
	int frames;
	int rc = 0;
	struct cras_stream_params *config;
	struct cras_audio_shm *shm = &stream->play_shm;
	struct timespec ts;

	config = stream->config;

	/* If this message is for an input stream, log error and drop it. */
	if (stream->direction != CRAS_STREAM_OUTPUT) {
		syslog(LOG_ERR, "Record data from output\n");
		return 0;
	}

	num_frames = config_playback_buf(stream, &buf, num_frames);

	/* Limit the amount of frames to the configured amount. */
	num_frames = min(num_frames, config->min_cb_level);

	cras_timespec_to_timespec(&ts, &shm->area->ts);

	/* Get samples from the user */
	if (config->unified_cb)
		frames = config->unified_cb(stream->client,
				stream->id,
				NULL,
				buf,
				num_frames,
				NULL,
				&ts,
				config->user_data);
	else
		frames = config->aud_cb(stream->client,
				stream->id,
				buf,
				num_frames,
				&ts,
				config->user_data);
	if (frames < 0) {
		send_stream_message(stream, CLIENT_STREAM_EOF);
		rc = frames;
		goto reply_written;
	}

	complete_playback_write(stream, frames);

reply_written:
	/* Signal server that data is ready, or that an error has occurred. */
	rc = send_playback_reply(stream, frames, rc);
	return rc;
}

/* Unified streams read audio and write back samples in the same callback. */
static int handle_unified_request(struct client_stream *stream,
				  unsigned int num_frames)
{
	struct cras_stream_params *config;
	uint8_t *captured_frames = NULL;
	uint8_t *playback_frames = NULL;
	struct timespec capture_ts;
	struct timespec playback_ts;
	int frames;
	int rc = 0;
	unsigned int server_frames = num_frames;
	const int has_input = cras_stream_has_input(stream->direction);
	const int has_output = cras_stream_uses_output_hw(stream->direction);

	config = stream->config;

	if (has_input) {
		num_frames = config_capture_buf(stream,
						&captured_frames,
						num_frames);
		cras_timespec_to_timespec(&capture_ts,
					  &stream->capture_shm.area->ts);
	}

	if (has_output) {
		unsigned int pb_frames = config_playback_buf(stream,
							     &playback_frames,
							     server_frames);
		if (!has_input)
			num_frames = pb_frames;
		cras_timespec_to_timespec(&playback_ts,
					  &stream->play_shm.area->ts);
	}

	/* Get samples from the user */
	frames = config->unified_cb(stream->client,
				    stream->id,
				    captured_frames,
				    playback_frames,
				    num_frames,
				    has_input ? &capture_ts : NULL,
				    has_output ? &playback_ts : NULL,
				    config->user_data);
	if (frames < 0) {
		send_stream_message(stream, CLIENT_STREAM_EOF);
		return send_playback_reply(stream, frames, frames);
	}

	if (cras_stream_has_input(stream->direction))
		complete_capture_read(stream, frames);

	if (cras_stream_uses_output_hw(stream->direction))
		complete_playback_write(stream, frames);

	/* Signal server that data is ready, or that an error has occurred. */
	return send_playback_reply(stream, frames, rc);
}

/* Listens to the audio socket for messages from the server indicating that
 * the stream needs to be serviced.  One of these runs per stream. */
static void *audio_thread(void *arg)
{
	struct client_stream *stream = (struct client_stream *)arg;
	int thread_terminated = 0;
	struct audio_message aud_msg;
	int num_read;

	if (arg == NULL)
		return (void *)-EIO;

	/* Try to get RT scheduling, if that fails try to set the nice value. */
	if (cras_set_rt_scheduling(CRAS_CLIENT_RT_THREAD_PRIORITY) ||
	    cras_set_thread_priority(CRAS_CLIENT_RT_THREAD_PRIORITY))
		cras_set_nice_level(CRAS_CLIENT_NICENESS_LEVEL);

	syslog(LOG_DEBUG, "audio thread started");
	while (stream->thread.running && !thread_terminated) {
		num_read = read_with_wake_fd(stream->wake_fds[0],
					     stream->aud_fd,
					     (uint8_t *)&aud_msg,
					     sizeof(aud_msg));
		if (num_read < 0)
			return (void *)-EIO;
		if (num_read == 0)
			continue;

		switch (aud_msg.id) {
		case AUDIO_MESSAGE_DATA_READY:
			thread_terminated = handle_capture_data_ready(
					stream,
					aud_msg.frames);
			break;
		case AUDIO_MESSAGE_REQUEST_DATA:
			thread_terminated = handle_playback_request(
					stream,
					aud_msg.frames);
			break;
		case AUDIO_MESSAGE_UNIFIED:
			thread_terminated = handle_unified_request(
					stream,
					aud_msg.frames);
			break;
		default:
			syslog(LOG_WARNING, "Unknown aud msg %d\n", aud_msg.id);
			break;
		}
	}

	return NULL;
}

/* Pokes the audio thread so that it can notice if it has been terminated. */
static int wake_aud_thread(struct client_stream *stream)
{
	int rc;

	rc = write(stream->wake_fds[1], &rc, 1);
	if (rc != 1)
		return rc;
	return 0;
}

/*
 * Client thread.
 */

/* Gets the shared memory region used to share audio data with the server. */
static int config_shm(struct cras_audio_shm *shm, int key, size_t size)
{
	int shmid;

	shmid = shmget(key, size, 0600);
	if (shmid < 0) {
		syslog(LOG_ERR, "shmget failed to get shm for stream.");
		return shmid;
	}
	shm->area = (struct cras_audio_shm_area *)shmat(shmid, NULL, 0);
	if (shm->area == (struct cras_audio_shm_area *)-1) {
		syslog(LOG_ERR, "shmat failed to attach shm for stream.");
		return errno;
	}
	/* Copy server shm config locally. */
	cras_shm_copy_shared_config(shm);

	return 0;
}

/* Release shm areas if references to them are held. */
static void free_shm(struct client_stream *stream)
{
	if (stream->capture_shm.area)
		shmdt(stream->capture_shm.area);
	if (stream->play_shm.area)
		shmdt(stream->play_shm.area);
	stream->capture_shm.area = NULL;
	stream->play_shm.area = NULL;
}

/* If the server cannot provide the requested format, configures an audio format
 * converter that handles transforming the input format to the format used by
 * the server. */
static int config_format_converter(struct cras_fmt_conv **conv,
				   const struct cras_audio_format *from,
				   const struct cras_audio_format *to,
				   unsigned int frames)
{
	if (cras_fmt_conversion_needed(from, to)) {
		syslog(LOG_DEBUG,
		       "format convert: from:%d %zu %zu to: %d %zu %zu "
		       "frames = %u",
		       from->format, from->frame_rate, from->num_channels,
		       to->format, to->frame_rate, to->num_channels,
		       frames);

		*conv = cras_fmt_conv_create(from, to, frames);
		if (!*conv) {
			syslog(LOG_ERR, "Failed to create format converter");
			return -ENOMEM;
		}
	}

	return 0;
}

/* Free format converters if they exist. */
static void free_fmt_conv(struct client_stream *stream)
{
	if (stream->play_conv) {
		cras_fmt_conv_destroy(stream->play_conv);
		free(stream->play_conv_buffer);
	}
	if (stream->capture_conv) {
		cras_fmt_conv_destroy(stream->capture_conv);
		free(stream->capture_conv_buffer);
	}
	stream->play_conv = NULL;
	stream->capture_conv = NULL;
}

/* Handles the stream connected message from the server.  Check if we need a
 * format converter, configure the shared memory region, and start the audio
 * thread that will handle requests from the server. */
static int stream_connected(struct client_stream *stream,
			    const struct cras_client_stream_connected *msg)
{
	int rc;
	struct cras_audio_format *sfmt = &stream->config->format;
	struct cras_audio_format mfmt;

	if (msg->err) {
		syslog(LOG_ERR, "Error Setting up stream %d\n", msg->err);
		return msg->err;
	}

	unpack_cras_audio_format(&mfmt, &msg->format);

	if (cras_stream_has_input(stream->direction)) {
		unsigned int max_frames;

		rc = config_shm(&stream->capture_shm,
				msg->input_shm_key,
				msg->shm_max_size);
		if (rc < 0) {
			syslog(LOG_ERR, "Error configuring capture shm");
			goto err_ret;
		}

		max_frames = max(cras_shm_used_frames(&stream->capture_shm),
				 stream->config->buffer_frames);

		/* Convert from h/w format to stream format for input. */
		rc = config_format_converter(&stream->capture_conv,
					     &mfmt,
					     &stream->config->format,
					     max_frames);
		if (rc < 0) {
			syslog(LOG_ERR, "Error setting up capture conversion");
			goto err_ret;
		}

		/* If a converted is needed, allocate a buffer for it. */
		stream->capture_conv_buffer =
			(uint8_t *)malloc(max_frames * cras_get_format_bytes(sfmt));
		if (!stream->capture_conv_buffer) {
			rc = -ENOMEM;
			goto err_ret;
		}
	}

	if (cras_stream_uses_output_hw(stream->direction)) {
		unsigned int max_frames;

		rc = config_shm(&stream->play_shm,
				msg->output_shm_key,
				msg->shm_max_size);
		if (rc < 0) {
			syslog(LOG_ERR, "Error configuring playback shm");
			goto err_ret;
		}

		max_frames = max(cras_shm_used_frames(&stream->play_shm),
				 stream->config->buffer_frames);

		/* Convert the stream format to the h/w format for output */
		rc = config_format_converter(&stream->play_conv,
					     &stream->config->format,
					     &mfmt,
					     max_frames);
		if (rc < 0) {
			syslog(LOG_ERR, "Error setting up playback conversion");
			goto err_ret;
		}

		/* If a converted is needed, allocate a buffer for it. */
		stream->play_conv_buffer =
				(uint8_t *)malloc(max_frames *
						  cras_get_format_bytes(sfmt));
		if (!stream->play_conv_buffer) {
			rc = -ENOMEM;
			goto err_ret;
		}

		cras_shm_set_volume_scaler(&stream->play_shm,
					   stream->volume_scaler);
	}

	rc = pipe(stream->wake_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Error piping");
		goto err_ret;
	}

	stream->thread.running = 1;

	rc = pthread_create(&stream->thread.tid, NULL, audio_thread, stream);
	if (rc) {
		syslog(LOG_ERR, "Couldn't create audio stream.");
		stream->thread.running = 0;
		goto err_ret;
	}

	return 0;
err_ret:
	free_fmt_conv(stream);
	if (stream->wake_fds[0] >= 0) {
		close(stream->wake_fds[0]);
		close(stream->wake_fds[1]);
	}
	free_shm(stream);
	return rc;
}

static int send_connect_message(struct cras_client *client,
				struct client_stream *stream)
{
	int rc;
	struct cras_connect_message serv_msg;
	int sock[2] = {-1, -1};

	/* Create a socket pair for the server to notify of audio events. */
	rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sock);
	if (rc != 0) {
		syslog(LOG_ERR, "socketpair fails.");
		goto fail;
	}

	cras_fill_connect_message(&serv_msg,
				  stream->config->direction,
				  stream->id,
				  stream->config->stream_type,
				  stream->config->buffer_frames,
				  stream->config->cb_threshold,
				  stream->config->min_cb_level,
				  stream->flags,
				  stream->config->format);
	rc = cras_send_with_fd(client->server_fd, &serv_msg, sizeof(serv_msg),
			       sock[1]);
	if (rc != sizeof(serv_msg)) {
		rc = EIO;
		syslog(LOG_ERR, "add_stream: Send server message failed.");
		goto fail;
	}

	stream->aud_fd = sock[0];
	close(sock[1]);
	return 0;

fail:
	if (sock[0] != -1)
		close(sock[0]);
	if (sock[1] != -1)
		close(sock[1]);
	return rc;
}

/* Adds a stream to a running client.  Checks to make sure that the client is
 * attached, waits if it isn't.  The stream is prepared on the  main thread and
 * passed here. */
static int client_thread_add_stream(struct cras_client *client,
				    struct client_stream *stream,
				    cras_stream_id_t *stream_id_out)
{
	int rc;
	cras_stream_id_t new_id;
	struct client_stream *out;

	/* Find an available stream id. */
	do {
		new_id = cras_get_stream_id(client->id, client->next_stream_id);
		client->next_stream_id++;
		DL_SEARCH_SCALAR(client->streams, out, id, new_id);
	} while (out != NULL);

	stream->id = new_id;
	*stream_id_out = new_id;
	stream->client = client;

	/* send a message to the server asking that the stream be started. */
	rc = send_connect_message(client, stream);
	if (rc != 0)
		return rc;

	/* Add the stream to the linked list */
	DL_APPEND(client->streams, stream);

	return 0;
}

/* Removes a stream from a running client from within the running client's
 * context. */
static int client_thread_rm_stream(struct cras_client *client,
				   cras_stream_id_t stream_id)
{
	struct cras_disconnect_stream_message msg;
	struct client_stream *stream =
		stream_from_id(client, stream_id);
	int rc;

	if (stream == NULL)
		return 0;

	/* Tell server to remove. */
	cras_fill_disconnect_stream_message(&msg, stream_id);
	rc = write(client->server_fd, &msg, sizeof(msg));
	if (rc < 0)
		syslog(LOG_WARNING, "error removing stream from server\n");

	/* And shut down locally. */
	if (stream->thread.running) {
		stream->thread.running = 0;
		wake_aud_thread(stream);
		pthread_join(stream->thread.tid, NULL);
	}


	free_shm(stream);

	DL_DELETE(client->streams, stream);
	if (stream->aud_fd >= 0)
		if (close(stream->aud_fd))
			syslog(LOG_WARNING, "Couldn't close audio socket");

	free_fmt_conv(stream);

	if (stream->wake_fds[0] >= 0) {
		close(stream->wake_fds[0]);
		close(stream->wake_fds[1]);
	}
	free(stream->config);
	free(stream);

	return 0;
}

/* Sets the volume scaling factor for a playing stream. */
static int client_thread_set_stream_volume(struct cras_client *client,
					   cras_stream_id_t stream_id,
					   float volume_scaler)
{
	struct client_stream *stream;

	stream = stream_from_id(client, stream_id);
	if (stream == NULL || volume_scaler > 1.0 || volume_scaler < 0.0)
		return -EINVAL;

	stream->volume_scaler = volume_scaler;
	if (stream->play_shm.area != NULL)
		cras_shm_set_volume_scaler(&stream->play_shm, volume_scaler);

	return 0;
}

/* Re-attaches a stream that was removed on the server side so that it could be
 * moved to a new device. To achieve this, remove the stream and send the
 * connect message again. */
static int handle_stream_reattach(struct cras_client *client,
				  cras_stream_id_t stream_id)
{
	struct client_stream *stream = stream_from_id(client, stream_id);
	int rc;

	if (stream == NULL)
		return 0;

	/* Shut down locally. Stream has been removed on the server side. */
	if (stream->thread.running) {
		stream->thread.running = 0;
		wake_aud_thread(stream);
		pthread_join(stream->thread.tid, NULL);
	}

	free_fmt_conv(stream);

	if (stream->aud_fd >= 0) {
		close(stream->aud_fd);
		stream->aud_fd = -1;
	}
	free_shm(stream);

	/* send a message to the server asking that the stream be started. */
	rc = send_connect_message(client, stream);
	if (rc != 0) {
		client_thread_rm_stream(client, stream_id);
		return rc;
	}

	return 0;
}

/* Attach to the shm region containing the server state. */
static int client_attach_shm(struct cras_client *client, key_t shm_key)
{
	int shmid;

	/* Should only happen once per client lifetime. */
	if (client->server_state)
		return -EBUSY;

	shmid = shmget(shm_key, sizeof(*(client->server_state)), 0400);
	if (shmid < 0) {
		syslog(LOG_ERR, "shmget failed to get shm for client.");
		return shmid;
	}
	client->server_state = (struct cras_server_state *)
			shmat(shmid, NULL, SHM_RDONLY);
	if (client->server_state == (void *)-1) {
		client->server_state = NULL;
		syslog(LOG_ERR, "shmat failed to attach shm for client.");
		return errno;
	}

	if (client->server_state->state_version != CRAS_SERVER_STATE_VERSION) {
		shmdt(client->server_state);
		client->server_state = NULL;
		syslog(LOG_ERR, "Unknown server_state version.");
		return -EINVAL;
	}

	return 0;
}

/* Handles messages from the cras server. */
static int handle_message_from_server(struct cras_client *client)
{
	uint8_t buf[CRAS_CLIENT_MAX_MSG_SIZE];
	struct cras_client_message *msg;
	int rc = 0;
	int nread;

	msg = (struct cras_client_message *)buf;
	nread = recv(client->server_fd, buf, sizeof(buf), 0);
	if (nread < (int)sizeof(msg->length))
		goto read_error;
	if ((int)msg->length != nread)
		goto read_error;

	switch (msg->id) {
	case CRAS_CLIENT_CONNECTED: {
		struct cras_client_connected *cmsg =
			(struct cras_client_connected *)msg;
		rc = client_attach_shm(client, cmsg->shm_key);
		if (rc)
			return rc;
		client->id = cmsg->client_id;

		break;
	}
	case CRAS_CLIENT_STREAM_CONNECTED: {
		struct cras_client_stream_connected *cmsg =
			(struct cras_client_stream_connected *)msg;
		struct client_stream *stream =
			stream_from_id(client, cmsg->stream_id);
		if (stream == NULL)
			break;
		rc = stream_connected(stream, cmsg);
		if (rc < 0)
			stream->config->err_cb(stream->client,
					       stream->id,
					       rc,
					       stream->config->user_data);
		break;
	}
	case CRAS_CLIENT_STREAM_REATTACH: {
		struct cras_client_stream_reattach *cmsg =
			(struct cras_client_stream_reattach *)msg;
		handle_stream_reattach(client, cmsg->stream_id);
		break;
	}
	case CRAS_CLIENT_AUDIO_DEBUG_INFO_READY:
		if (client->debug_info_callback)
			client->debug_info_callback(client);
		break;
	default:
		syslog(LOG_WARNING, "Receive unknown command %d", msg->id);
		break;
	}

	return 0;
read_error:
	rc = connect_to_server_wait(client);
	if (rc < 0) {
		syslog(LOG_WARNING, "Can't read from server\n");
		client->thread.running = 0;
		return -EIO;
	}
	return 0;
}

/* Handles messages from streams to this client. */
static int handle_stream_message(struct cras_client *client)
{
	struct stream_msg msg;
	int rc;

	rc = read(client->stream_fds[0], &msg, sizeof(msg));
	if (rc < 0)
		syslog(LOG_DEBUG, "Stream read failed %d\n", errno);
	/* The only reason a stream sends a message is if it needs to be
	 * removed. An error on read would mean the same thing so regardless of
	 * what gets us here, just remove the stream */
	client_thread_rm_stream(client, msg.stream_id);
	return 0;
}

/* Handles messages from users to this client. */
static int handle_command_message(struct cras_client *client)
{
	uint8_t buf[MAX_CMD_MSG_LEN];
	struct command_msg *msg = (struct command_msg *)buf;
	int rc, to_read;

	rc = read(client->command_fds[0], buf, sizeof(msg->len));
	if (rc != sizeof(msg->len) || msg->len > MAX_CMD_MSG_LEN) {
		rc = -EIO;
		goto cmd_msg_complete;
	}
	to_read = msg->len - rc;
	rc = read(client->command_fds[0], &buf[0] + rc, to_read);
	if (rc != to_read) {
		rc = -EIO;
		goto cmd_msg_complete;
	}

	if (!check_server_connected_wait(client))
		if (connect_to_server_wait(client) < 0) {
			syslog(LOG_ERR, "Lost server connection.");
			rc = -EIO;
			goto cmd_msg_complete;
		}

	switch (msg->msg_id) {
	case CLIENT_STOP: {
		struct client_stream *s;

		/* Stop all playing streams */
		DL_FOREACH(client->streams, s)
			client_thread_rm_stream(client, s->id);

		/* And stop this client */
		client->thread.running = 0;
		rc = 0;
		break;
	}
	case CLIENT_ADD_STREAM: {
		struct add_stream_command_message *add_msg =
			(struct add_stream_command_message *)msg;
		rc = client_thread_add_stream(client,
					      add_msg->stream,
					      add_msg->stream_id_out);
		break;
	}
	case CLIENT_REMOVE_STREAM:
		rc = client_thread_rm_stream(client, msg->stream_id);
		break;
	case CLIENT_SET_STREAM_VOLUME_SCALER: {
		struct set_stream_volume_command_message *vol_msg =
			(struct set_stream_volume_command_message *)msg;
		rc = client_thread_set_stream_volume(client,
						     vol_msg->header.stream_id,
						     vol_msg->volume_scaler);
		break;
	}
	case CLIENT_SERVER_CONNECT:
		rc = connect_to_server_wait(client);
		break;
	default:
		assert(0);
		break;
	}

cmd_msg_complete:
	/* Wake the waiting main thread with the result of the command. */
	if (write(client->command_reply_fds[1], &rc, sizeof(rc)) != sizeof(rc))
		return -EIO;
	return rc;
}

/*  This thread handles non audio sample communication with the audio server.
 *  The client program will call fucntions below to send messages to this thread
 *  to add or remove streams or change parameters.
 */
static void *client_thread(void *arg)
{
	struct client_input {
		int fd;
		int (*cb)(struct cras_client *client);
		struct client_input *next;
	};
	struct cras_client *client = (struct cras_client *)arg;
	struct client_input server_input, command_input, stream_input;
	struct client_input *inputs = NULL;

	if (arg == NULL)
		return (void *)-EINVAL;

	memset(&server_input, 0, sizeof(server_input));
	memset(&command_input, 0, sizeof(command_input));
	memset(&stream_input, 0, sizeof(stream_input));

	while (client->thread.running) {
		fd_set poll_set;
		struct client_input *curr_input;
		int max_fd;
		int rc;

		inputs = NULL;
		server_input.fd = client->server_fd;
		server_input.cb = handle_message_from_server;
		LL_APPEND(inputs, &server_input);
		command_input.fd = client->command_fds[0];
		command_input.cb = handle_command_message;
		LL_APPEND(inputs, &command_input);
		stream_input.fd = client->stream_fds[0];
		stream_input.cb = handle_stream_message;
		LL_APPEND(inputs, &stream_input);

		FD_ZERO(&poll_set);
		max_fd = 0;
		LL_FOREACH(inputs, curr_input) {
			FD_SET(curr_input->fd, &poll_set);
			max_fd = max(curr_input->fd, max_fd);
		}

		rc = select(max_fd + 1, &poll_set, NULL, NULL, NULL);
		if (rc < 0)
			continue;

		LL_FOREACH(inputs, curr_input)
			if (FD_ISSET(curr_input->fd, &poll_set))
				rc = curr_input->cb(client);
	}

	/* close the command reply pipe. */
	close(client->command_reply_fds[1]);
	client->command_reply_fds[1] = -1;

	return NULL;
}

/* Sends a message to the client thread to complete an action requested by the
 * user.  Then waits for the action to complete and returns the result. */
static int send_command_message(struct cras_client *client,
				struct command_msg *msg)
{
	int rc, cmd_res;
	if (client == NULL || !client->thread.running)
		return -EINVAL;

	rc = write(client->command_fds[1], msg, msg->len);
	if (rc != (int)msg->len)
		return -EPIPE;

	/* Wait for command to complete. */
	rc = read(client->command_reply_fds[0], &cmd_res, sizeof(cmd_res));
	if (rc != sizeof(cmd_res))
		return -EPIPE;
	return cmd_res;
}

/* Send a simple message to the client thread that holds no data. */
static int send_simple_cmd_msg(struct cras_client *client,
			       cras_stream_id_t stream_id,
			       unsigned msg_id)
{
	struct command_msg msg;

	msg.len = sizeof(msg);
	msg.stream_id = stream_id;
	msg.msg_id = msg_id;

	return send_command_message(client, &msg);
}

/* Sends the set volume message to the client thread. */
static int send_stream_volume_command_msg(struct cras_client *client,
					  cras_stream_id_t stream_id,
					  float volume_scaler)
{
	struct set_stream_volume_command_message msg;

	msg.header.len = sizeof(msg);
	msg.header.stream_id = stream_id;
	msg.header.msg_id = CLIENT_SET_STREAM_VOLUME_SCALER;
	msg.volume_scaler = volume_scaler;

	return send_command_message(client, &msg.header);
}

/* Sends a message back to the client and returns the error code. */
static int write_message_to_server(struct cras_client *client,
				   const struct cras_server_message *msg)
{
	if (write(client->server_fd, msg, msg->length) !=
			(ssize_t)msg->length) {
		int rc = 0;

		/* Write to server failed, try to re-connect. */
		syslog(LOG_DEBUG, "Server write failed, re-attach.");
		if (client->thread.running)
			rc = send_simple_cmd_msg(client, 0,
						 CLIENT_SERVER_CONNECT);
		else
			rc = connect_to_server_wait(client);
		if (rc < 0)
			return rc;
		if (write(client->server_fd, msg, msg->length) !=
				(ssize_t)msg->length)
			return -EINVAL;
	}
	return 0;
}

/* Gets the update_count of the server state shm region. */
static inline
unsigned begin_server_state_read(const struct cras_server_state *state)
{
	unsigned count;

	/* Version will be odd when the server is writing. */
	while ((count = *(volatile unsigned *)&state->update_count) & 1)
		sched_yield();
	__sync_synchronize();
	return count;
}

/* Checks if the update count of the server state shm region has changed from
 * count.  Returns 0 if the count still matches.
 */
static inline
int end_server_state_read(const struct cras_server_state *state, unsigned count)
{
	__sync_synchronize();
	if (count != *(volatile unsigned *)&state->update_count)
		return -EAGAIN;
	return 0;

}

/*
 * Exported Client Interface
 */

int cras_client_create(struct cras_client **client)
{
	int rc;

	*client = (struct cras_client *)calloc(1, sizeof(struct cras_client));
	if (*client == NULL)
		return -ENOMEM;
	(*client)->server_fd = -1;
	(*client)->id = -1;

	/* Pipes used by the main thread and the client thread to send commands
	 * and replies. */
	rc = pipe((*client)->command_fds);
	if (rc < 0)
		goto free_error;
	/* Pipe used to communicate between the client thread and the audio
	 * thread. */
	rc = pipe((*client)->stream_fds);
	if (rc < 0) {
		close((*client)->command_fds[0]);
		close((*client)->command_fds[1]);
		goto free_error;
	}
	(*client)->command_reply_fds[0] = -1;
	(*client)->command_reply_fds[1] = -1;

	openlog("cras_client", LOG_PID, LOG_USER);
	setlogmask(LOG_MASK(LOG_ERR));

	return 0;
free_error:
	free(*client);
	*client = NULL;
	return rc;
}

static inline
int shutdown_and_close_socket(int sockfd)
{
	int rc;
	uint8_t buffer[CRAS_CLIENT_MAX_MSG_SIZE];
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = SERVER_SHUTDOWN_TIMEOUT_US;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));

	rc = shutdown(sockfd, SHUT_WR);
	if (rc < 0)
		return rc;
	/* Wait until the socket is closed by the peer. */
	for (;;) {
		rc = recv(sockfd, buffer, sizeof(buffer), 0);
		if (rc <= 0)
			break;
	}
	return close(sockfd);
}

void cras_client_destroy(struct cras_client *client)
{
	if (client == NULL)
		return;
	cras_client_stop(client);
	if (client->server_state)
		shmdt(client->server_state);
	if (client->server_fd >= 0)
		shutdown_and_close_socket(client->server_fd);
	close(client->command_fds[0]);
	close(client->command_fds[1]);
	close(client->stream_fds[0]);
	close(client->stream_fds[1]);
	free(client);
}

int cras_client_connect(struct cras_client *client)
{
	return connect_to_server(client);
}

int cras_client_connected_wait(struct cras_client *client)
{
	return send_simple_cmd_msg(client, 0, CLIENT_SERVER_CONNECT);
}

struct cras_stream_params *cras_client_stream_params_create(
		enum CRAS_STREAM_DIRECTION direction,
		size_t buffer_frames,
		size_t cb_threshold,
		size_t min_cb_level,
		enum CRAS_STREAM_TYPE stream_type,
		uint32_t flags,
		void *user_data,
		cras_playback_cb_t aud_cb,
		cras_error_cb_t err_cb,
		struct cras_audio_format *format)
{
	struct cras_stream_params *params;

	params = (struct cras_stream_params *)malloc(sizeof(*params));
	if (params == NULL)
		return NULL;

	params->direction = direction;
	params->buffer_frames = buffer_frames;
	params->cb_threshold = cb_threshold;
	params->min_cb_level = min_cb_level;

	/* For input cb_thresh is buffer size. For output the callback level. */
	if (params->direction == CRAS_STREAM_OUTPUT)
		params->cb_threshold = params->buffer_frames;
	else
		params->cb_threshold = params->min_cb_level;

	params->stream_type = stream_type;
	params->flags = flags;
	params->user_data = user_data;
	params->aud_cb = aud_cb;
	params->unified_cb = 0;
	params->err_cb = err_cb;
	memcpy(&(params->format), format, sizeof(*format));
	return params;
}

struct cras_stream_params *cras_client_unified_params_create(
		enum CRAS_STREAM_DIRECTION direction,
		unsigned int block_size,
		enum CRAS_STREAM_TYPE stream_type,
		uint32_t flags,
		void *user_data,
		cras_unified_cb_t unified_cb,
		cras_error_cb_t err_cb,
		struct cras_audio_format *format)
{
	struct cras_stream_params *params;

	params = (struct cras_stream_params *)malloc(sizeof(*params));
	if (params == NULL)
		return NULL;

	params->direction = direction;
	params->buffer_frames = block_size * 2;
	params->cb_threshold = block_size;
	params->min_cb_level = block_size;
	params->stream_type = stream_type;
	params->flags = flags;
	params->user_data = user_data;
	params->aud_cb = 0;
	params->unified_cb = unified_cb;
	params->err_cb = err_cb;
	memcpy(&(params->format), format, sizeof(*format));

	return params;
}

void cras_client_stream_params_destroy(struct cras_stream_params *params)
{
	free(params);
}

int cras_client_add_stream(struct cras_client *client,
			   cras_stream_id_t *stream_id_out,
			   struct cras_stream_params *config)
{
	struct add_stream_command_message cmd_msg;
	struct client_stream *stream;
	int rc = 0;

	if (client == NULL || config == NULL || stream_id_out == NULL)
		return -EINVAL;

	if (config->aud_cb == NULL && config->unified_cb == NULL)
		return -EINVAL;

	if (config->err_cb == NULL)
		return -EINVAL;

	stream = (struct client_stream *)calloc(1, sizeof(*stream));
	if (stream == NULL) {
		rc = -ENOMEM;
		goto add_failed;
	}
	stream->config = (struct cras_stream_params *)
			malloc(sizeof(*(stream->config)));
	if (stream->config == NULL) {
		rc = -ENOMEM;
		goto add_failed;
	}
	memcpy(stream->config, config, sizeof(*config));
	stream->aud_fd = -1;
	stream->wake_fds[0] = -1;
	stream->wake_fds[1] = -1;
	stream->direction = config->direction;
	stream->volume_scaler = 1.0;


	cmd_msg.header.len = sizeof(cmd_msg);
	cmd_msg.header.msg_id = CLIENT_ADD_STREAM;
	cmd_msg.header.stream_id = stream->id;
	cmd_msg.stream = stream;
	cmd_msg.stream_id_out = stream_id_out;
	rc = send_command_message(client, &cmd_msg.header);
	if (rc < 0) {
		syslog(LOG_ERR, "adding stream failed in thread %d", rc);
		goto add_failed;
	}

	return 0;

add_failed:
	if (stream) {
		if (stream->config)
			free(stream->config);
		free(stream);
	}
	return rc;
}

int cras_client_rm_stream(struct cras_client *client,
			  cras_stream_id_t stream_id)
{
	if (client == NULL)
		return -EINVAL;

	return send_simple_cmd_msg(client, stream_id, CLIENT_REMOVE_STREAM);
}

int cras_client_set_stream_volume(struct cras_client *client,
				  cras_stream_id_t stream_id,
				  float volume_scaler)
{
	if (client == NULL)
		return -EINVAL;

	return send_stream_volume_command_msg(client, stream_id, volume_scaler);
}

int cras_client_switch_iodev(struct cras_client *client,
			     enum CRAS_STREAM_TYPE stream_type,
			     uint32_t iodev)
{
	struct cras_switch_stream_type_iodev serv_msg;

	if (client == NULL)
		return -EINVAL;

	fill_cras_switch_stream_type_iodev(&serv_msg, stream_type, iodev);
	return write_message_to_server(client, &serv_msg.header);
}

int cras_client_set_system_volume(struct cras_client *client, size_t volume)
{
	struct cras_set_system_volume msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_system_volume(&msg, volume);
	return write_message_to_server(client, &msg.header);
}

int cras_client_set_system_capture_gain(struct cras_client *client, long gain)
{
	struct cras_set_system_capture_gain msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_system_capture_gain(&msg, gain);
	return write_message_to_server(client, &msg.header);
}

int cras_client_set_system_mute(struct cras_client *client, int mute)
{
	struct cras_set_system_mute msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_system_mute(&msg, mute);
	return write_message_to_server(client, &msg.header);
}

int cras_client_set_user_mute(struct cras_client *client, int mute)
{
	struct cras_set_system_mute msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_user_mute(&msg, mute);
	return write_message_to_server(client, &msg.header);
}

int cras_client_set_system_mute_locked(struct cras_client *client, int locked)
{
	struct cras_set_system_mute msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_system_mute_locked(&msg, locked);
	return write_message_to_server(client, &msg.header);
}

int cras_client_set_system_capture_mute(struct cras_client *client, int mute)
{
	struct cras_set_system_mute msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_system_capture_mute(&msg, mute);
	return write_message_to_server(client, &msg.header);
}

int cras_client_set_system_capture_mute_locked(struct cras_client *client,
					       int locked)
{
	struct cras_set_system_mute msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_system_capture_mute_locked(&msg, locked);
	return write_message_to_server(client, &msg.header);
}

size_t cras_client_get_system_volume(struct cras_client *client)
{
	if (!client || !client->server_state)
		return 0;
	return client->server_state->volume;
}

long cras_client_get_system_capture_gain(struct cras_client *client)
{
	if (!client || !client->server_state)
		return 0;
	return client->server_state->capture_gain;
}

int cras_client_get_system_muted(struct cras_client *client)
{
	if (!client || !client->server_state)
		return 0;
	return client->server_state->mute;
}

int cras_client_get_system_capture_muted(struct cras_client *client)
{
	if (!client || !client->server_state)
		return 0;
	return client->server_state->capture_mute;
}

long cras_client_get_system_min_volume(struct cras_client *client)
{
	if (!client || !client->server_state)
		return 0;
	return client->server_state->min_volume_dBFS;
}

long cras_client_get_system_max_volume(struct cras_client *client)
{
	if (!client || !client->server_state)
		return 0;
	return client->server_state->max_volume_dBFS;
}

long cras_client_get_system_min_capture_gain(struct cras_client *client)
{
	if (!client || !client->server_state)
		return 0;
	return client->server_state->min_capture_gain;
}

long cras_client_get_system_max_capture_gain(struct cras_client *client)
{
	if (!client || !client->server_state)
		return 0;
	return client->server_state->max_capture_gain;
}

const struct audio_debug_info *cras_client_get_audio_debug_info(
		struct cras_client *client)
{
	if (!client || !client->server_state)
		return NULL;

	return &client->server_state->audio_debug_info;
}

unsigned cras_client_get_num_active_streams(struct cras_client *client,
					    struct timespec *ts)
{
	unsigned num_streams, version;

	if (!client || !client->server_state)
		return 0;

read_active_streams_again:
	version = begin_server_state_read(client->server_state);
	num_streams = client->server_state->num_active_streams;
	if (ts) {
		if (num_streams)
			clock_gettime(CLOCK_MONOTONIC, ts);
		else
			cras_timespec_to_timespec(ts,
				&client->server_state->last_active_stream_time);
	}
	if (end_server_state_read(client->server_state, version))
		goto read_active_streams_again;

	return num_streams;
}

cras_node_id_t cras_client_get_selected_output(struct cras_client *client)
{
	unsigned version;
	cras_node_id_t id;

read_active_streams_again:
	version = begin_server_state_read(client->server_state);
	id = client->server_state->selected_output;
	if (end_server_state_read(client->server_state, version))
		goto read_active_streams_again;

	return id;
}

cras_node_id_t cras_client_get_selected_input(struct cras_client *client)
{
	unsigned version;
	cras_node_id_t id;

read_active_streams_again:
	version = begin_server_state_read(client->server_state);
	id = client->server_state->selected_input;
	if (end_server_state_read(client->server_state, version))
		goto read_active_streams_again;

	return id;
}

int cras_client_run_thread(struct cras_client *client)
{
	if (client == NULL || client->thread.running)
		return -EINVAL;

	assert(client->command_reply_fds[0] == -1 &&
	       client->command_reply_fds[1] == -1);

	client->thread.running = 1;
	if (pipe(client->command_reply_fds) < 0)
		return -EIO;
	if (pthread_create(&client->thread.tid, NULL, client_thread, client))
		return -ENOMEM;

	return 0;
}

int cras_client_stop(struct cras_client *client)
{
	if (client == NULL || !client->thread.running)
		return -EINVAL;

	send_simple_cmd_msg(client, 0, CLIENT_STOP);
	pthread_join(client->thread.tid, NULL);

	/* The other end of the reply pipe is closed by the client thread, just
	 * clost the read end here. */
	close(client->command_reply_fds[0]);
	client->command_reply_fds[0] = -1;

	return 0;
}

int cras_client_get_output_devices(const struct cras_client *client,
				   struct cras_iodev_info *devs,
				   struct cras_ionode_info *nodes,
				   size_t *num_devs, size_t *num_nodes)
{
	const struct cras_server_state *state;
	unsigned avail_devs, avail_nodes, version;

	if (!client)
		return -EINVAL;
	state = client->server_state;
	if (!state)
		return -EINVAL;

read_outputs_again:
	version = begin_server_state_read(state);
	avail_devs = min(*num_devs, state->num_output_devs);
	memcpy(devs, state->output_devs, avail_devs * sizeof(*devs));
	avail_nodes = min(*num_nodes, state->num_output_nodes);
	memcpy(nodes, state->output_nodes, avail_nodes * sizeof(*nodes));
	if (end_server_state_read(state, version))
		goto read_outputs_again;

	*num_devs = avail_devs;
	*num_nodes = avail_nodes;

	return 0;
}

int cras_client_get_input_devices(const struct cras_client *client,
				  struct cras_iodev_info *devs,
				  struct cras_ionode_info *nodes,
				  size_t *num_devs, size_t *num_nodes)
{
	const struct cras_server_state *state;
	unsigned avail_devs, avail_nodes, version;

	if (!client)
		return -EINVAL;
	state = client->server_state;
	if (!state)
		return -EINVAL;

read_inputs_again:
	version = begin_server_state_read(state);
	avail_devs = min(*num_devs, state->num_input_devs);
	memcpy(devs, state->input_devs, avail_devs * sizeof(*devs));
	avail_nodes = min(*num_nodes, state->num_input_nodes);
	memcpy(nodes, state->input_nodes, avail_nodes * sizeof(*nodes));
	if (end_server_state_read(state, version))
		goto read_inputs_again;

	*num_devs = avail_devs;
	*num_nodes = avail_nodes;

	return 0;
}

int cras_client_get_attached_clients(const struct cras_client *client,
				     struct cras_attached_client_info *clients,
				     size_t max_clients)
{
	const struct cras_server_state *state;
	unsigned num, version;

	if (!client)
		return -EINVAL;
	state = client->server_state;
	if (!state)
		return 0;

read_clients_again:
	version = begin_server_state_read(state);
	num = min(max_clients, state->num_attached_clients);
	memcpy(clients, state->client_info, num * sizeof(*clients));
	if (end_server_state_read(state, version))
		goto read_clients_again;

	return num;
}

/* Find an output ionode on an iodev with the matching name.
 *
 * Args:
 *    dev_name - The prefix of the iodev name.
 *    node_name - The prefix of the ionode name.
 *    dev_info - The information about the iodev will be returned here.
 *    node_info - The information about the ionode will be returned here.
 * Returns:
 *    0 if successful, -1 if the node cannot be found.
 */
static int cras_client_find_output_node(const struct cras_client *client,
					const char *dev_name,
					const char *node_name,
					struct cras_iodev_info *dev_info,
					struct cras_ionode_info *node_info)
{
	size_t ndevs, nnodes;
	struct cras_iodev_info *devs = NULL;
	struct cras_ionode_info *nodes = NULL;
	int rc = -1;
	unsigned i, j;

	if (!client || !dev_name || !node_name)
		goto quit;

	devs = (struct cras_iodev_info *)
			malloc(CRAS_MAX_IODEVS * sizeof(*devs));
	if (!devs)
		goto quit;

	nodes = (struct cras_ionode_info *)
			malloc(CRAS_MAX_IONODES * sizeof(*nodes));
	if (!nodes)
		goto quit;

	ndevs = CRAS_MAX_IODEVS;
	nnodes = CRAS_MAX_IONODES;
	rc = cras_client_get_output_devices(client, devs, nodes, &ndevs,
					    &nnodes);
	if (rc < 0)
		goto quit;

	for (i = 0; i < ndevs; i++)
		if (!strncmp(dev_name, devs[i].name, strlen(dev_name)))
			goto found_dev;
	rc = -1;
	goto quit;

found_dev:
	for (j = 0; j < nnodes; j++)
		if (nodes[j].iodev_idx == devs[i].idx &&
		    !strncmp(node_name, nodes[j].name, strlen(node_name)))
			goto found_node;
	rc = -1;
	goto quit;

found_node:
	*dev_info = devs[i];
	*node_info = nodes[j];
	rc = 0;

quit:
	free(devs);
	free(nodes);
	return rc;
}

int cras_client_output_dev_plugged(const struct cras_client *client,
				   const char *name)
{
	struct cras_iodev_info dev_info;
	struct cras_ionode_info node_info;

	if (cras_client_find_output_node(client, name, "Front Headphone Jack",
					 &dev_info, &node_info) < 0)
		return 0;

	return node_info.plugged;
}

int cras_client_set_node_attr(struct cras_client *client,
			      cras_node_id_t node_id,
			      enum ionode_attr attr, int value)
{
	struct cras_set_node_attr msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_node_attr(&msg, node_id, attr, value);
	return write_message_to_server(client, &msg.header);
}

int cras_client_select_node(struct cras_client *client,
			    enum CRAS_STREAM_DIRECTION direction,
			    cras_node_id_t node_id)
{
	struct cras_select_node msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_select_node(&msg, direction, node_id);
	return write_message_to_server(client, &msg.header);
}

int cras_client_format_bytes_per_frame(struct cras_audio_format *fmt)
{
	if (fmt == NULL)
		return -EINVAL;

	return cras_get_format_bytes(fmt);
}

int cras_client_calc_playback_latency(const struct timespec *sample_time,
				      struct timespec *delay)
{
	struct timespec now;

	if (delay == NULL)
		return -EINVAL;

	clock_gettime(CLOCK_MONOTONIC, &now);

	/* for output return time until sample is played (t - now) */
	subtract_timespecs(sample_time, &now, delay);
	return 0;
}

int cras_client_calc_capture_latency(const struct timespec *sample_time,
				     struct timespec *delay)
{
	struct timespec now;

	if (delay == NULL)
		return -EINVAL;

	clock_gettime(CLOCK_MONOTONIC, &now);

	/* For input want time since sample read (now - t) */
	subtract_timespecs(&now, sample_time, delay);
	return 0;
}

int cras_client_reload_dsp(struct cras_client *client)
{
	struct cras_reload_dsp msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_reload_dsp(&msg);
	return write_message_to_server(client, &msg.header);
}

int cras_client_dump_dsp_info(struct cras_client *client)
{
	struct cras_dump_dsp_info msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_dump_dsp_info(&msg);
	return write_message_to_server(client, &msg.header);
}

int cras_client_update_audio_debug_info(
	struct cras_client *client,
	void (*debug_info_cb)(struct cras_client *))
{
	struct cras_dump_audio_thread msg;

	if (client == NULL)
		return -EINVAL;

	client->debug_info_callback = debug_info_cb;

	cras_fill_dump_audio_thread(&msg);
	return write_message_to_server(client, &msg.header);
}

int cras_client_set_node_volume(struct cras_client *client,
				cras_node_id_t node_id,
				uint8_t volume)
{
	struct cras_set_node_attr msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_node_attr(&msg, node_id, IONODE_ATTR_VOLUME, volume);
	return write_message_to_server(client, &msg.header);
}

int cras_client_set_node_capture_gain(struct cras_client *client,
				      cras_node_id_t node_id,
				      long gain)
{
	struct cras_set_node_attr msg;

	if (client == NULL)
		return -EINVAL;
	if (gain > INT_MAX || gain < INT_MIN)
		return -EINVAL;

	cras_fill_set_node_attr(&msg, node_id, IONODE_ATTR_CAPTURE_GAIN, gain);
	return write_message_to_server(client, &msg.header);
}
