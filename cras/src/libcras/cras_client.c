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
 *    client_stream struct and sets connection_fd to listen for audio
 *    requests from the server after the conncetion completes.
 *  client_connected - The server will send a connected message to indicate that
 *    the client should start listening for an audio connection on
 *    connection_fd.  This message also specifies the shared memory region to
 *    use to share audio samples.  This region will be shmat'd and a new
 *    aud_fd will be set up for the next connection to connection_fd.
 *  running - Once the connections are established, the client will listen for
 *    requests on aud_fd and fill the shm region with the requested number of
 *    samples. This happens in the aud_cb specified in the stream parameters.
 */

#include <grp.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

/* Commands sent from the user to the running client. */
enum {
	CLIENT_STOP,
	CLIENT_ADD_STREAM,
	CLIENT_REMOVE_STREAM,
	CLIENT_SET_STREAM_VOLUME_SCALER,
	CLIENT_GET_OUTPUT_DEVICE_LIST,
	CLIENT_GET_INPUT_DEVICE_LIST,
	CLIENT_GET_SYSTEM_VOLUME,
	CLIENT_GET_SYSTEM_CAPTURE_GAIN,
	CLIENT_GET_SYSTEM_MUTED,
	CLIENT_GET_SYSTEM_CAPTURE_MUTED,
	CLIENT_GET_ATTACHED_CLIENT_LIST,
	CLIENT_GET_SYSTEM_MIN_VOLUME,
	CLIENT_GET_SYSTEM_MAX_VOLUME,
	CLIENT_GET_SYSTEM_MIN_CAPTURE_GAIN,
	CLIENT_GET_SYSTEM_MAX_CAPTURE_GAIN,
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

struct get_device_list_message {
	struct command_msg header;
	struct cras_iodev_info *devs;
	size_t max_devs;
};

struct get_attached_client_list_message {
	struct command_msg header;
	struct cras_attached_client_info *clients;
	size_t max_clients;
};

/* Commands send from a running stream to the client. */
enum {
	CLIENT_STREAM_EOF,
	CLIENT_STREAM_SOCKET_ERROR,
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
	cras_error_cb_t err_cb;
	struct cras_audio_format format;
};

/* Represents an attached audio stream.
 * id - Unique stream identifier.
 * connection_fd - Listen for incoming connection from the server.
 * aud_fd - After server connects audio messages come in here.
 * direction - playback(CRAS_STREAM_OUTPUT) or capture(CRAS_STREAM_INPUT).
 * flags - Currently not used.
 * volume_scaler - Amount to scale the stream by, 0.0 to 1.0.
 * tid - Thread id of the audio thread spawned for this stream.
 * running - Audio thread runs while this is non-zero.
 * wake_fds - Pipe to wake the audio thread.
 * client - The client this stream is attached to.
 * config - Audio stream configuration.
 * shm - Shared memory used to exchange audio samples with the server.
 * conv - Format converter, used if the server's audio format doesn't match.
 * fmt_conv_buffer - Buffer used to store samples before sending for format
 *     conversion.
 * aud_address - Address used to listen for server requesting audio samples.
 * prev, next - Form a linked list of streams attached to a client.
 */
struct client_stream {
	cras_stream_id_t id;
	int connection_fd; /* Listen for incoming connection from the server. */
	int aud_fd; /* After server connects audio messages come in here. */
	enum CRAS_STREAM_DIRECTION direction; /* playback or capture. */
	uint32_t flags;
	float volume_scaler;
	struct thread_state thread;
	int wake_fds[2]; /* Pipe to wake the thread */
	struct cras_client *client;
	struct cras_stream_params *config;
	struct cras_audio_shm_area *shm;
	struct cras_fmt_conv *conv;
	uint8_t *fmt_conv_buffer;
	struct sockaddr_un aud_address;
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
 * num_input_devs - Number of input devices available.
 * input_devs - List of input devices available.
 * num_output_devs - Number of output devices available.
 * output_devs = List of output devices available.
 * system_volume - System playback volume level.
 * system_capture_gain - System capture gain level.
 * system_muted - True if the system playback path is muted.
 * system_capture_muted - True if the system capture path is muted.
 * system_min_volume - In dBFS * 100, minimum system volume.
 * system_max_volume - In dBFS * 100, maximum system volume.
 * system_min_capture_gain - In dBFS * 100, minimum system capture gain.
 * system_max_capture_gain - In dBFS * 100, maximum system capture gain.
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
	size_t num_input_devs;
	struct cras_iodev_info *input_devs;
	size_t num_output_devs;
	struct cras_iodev_info *output_devs;
	size_t num_attached_clients;
	struct cras_attached_client_info *attached_clients;
	size_t system_volume;
	long system_capture_gain;
	int system_muted;
	int system_capture_muted;
	long system_min_volume;
	long system_max_volume;
	long system_min_capture_gain;
	long system_max_capture_gain;
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

/* This must be written a million times... */
static void subtract_timespecs(const struct timespec *end,
			       const struct timespec *beg,
			       struct timespec *diff)
{
	/* If end is before geb, return 0. */
	if ((end->tv_sec < beg->tv_sec) ||
	    ((end->tv_sec == beg->tv_sec) && (end->tv_nsec <= beg->tv_nsec)))
		diff->tv_sec = diff->tv_nsec = 0;
	else {
		if (end->tv_nsec < beg->tv_nsec) {
			diff->tv_sec = end->tv_sec - beg->tv_sec - 1;
			diff->tv_nsec =
				end->tv_nsec + 1000000000L - beg->tv_nsec;
		} else {
			diff->tv_sec = end->tv_sec - beg->tv_sec;
			diff->tv_nsec = end->tv_nsec - beg->tv_nsec;
		}
	}
}

/* Attempts to set the group of the socket file to "cras" if that group exists,
 * then makes the socket readable and writable by that group, so the server can
 * have access to this socket file. */
static int set_socket_perms(const char *socket_path)
{
	const struct group *group_info;

	group_info = getgrnam(CRAS_DEFAULT_GROUP_NAME);
	if (group_info != NULL)
		if (chown(socket_path, -1, group_info->gr_gid) != 0)
			syslog(LOG_ERR, "Couldn't set group of audio socket.");

	return chmod(socket_path, 0770);
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
	timeout.tv_usec = SERVER_CONNECT_TIMEOUT_US;

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

/* Opens the server socket and connects to it. */
static int connect_to_server(struct cras_client *client)
{
	int rc;
	struct sockaddr_un address;

	if (client->server_fd >= 0)
		close(client->server_fd);
	client->server_fd = socket(PF_UNIX, SOCK_STREAM, 0);
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

	rc = connect(client->server_fd, (struct sockaddr *)&address,
			sizeof(struct sockaddr_un));
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
		if (nread != len)
			return -EIO;
	}
	if (FD_ISSET(wake_fd, &poll_set)) {
		rc = read(wake_fd, &tmp, 1);
		if (rc < 0)
			return rc;
	}

	return nread;
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
				     size_t num_frames)
{
	int frames;
	struct cras_stream_params *config;
	uint8_t *captured_frames;

	config = stream->config;
	/* If this message is for an output stream, log error and drop it. */
	if (stream->direction != CRAS_STREAM_INPUT) {
		syslog(LOG_ERR, "Play data to input\n");
		return 0;
	}

	captured_frames = cras_shm_get_curr_read_buffer(stream->shm);

	/* If we need to do format conversion convert to the temporary buffer
	 * and pass the converted samples to the client. */
	if (stream->conv) {
		num_frames = cras_fmt_conv_convert_frames(
				stream->conv,
				captured_frames,
				stream->fmt_conv_buffer,
				num_frames);
		captured_frames = stream->fmt_conv_buffer;
	}

	frames = config->aud_cb(stream->client,
				stream->id,
				captured_frames,
				num_frames,
				&stream->shm->ts,
				config->user_data);
	if (frames > 0) {
		if (stream->conv)
			frames = cras_fmt_conv_out_frames_to_in(
					stream->conv, frames);
		cras_shm_buffer_read(stream->shm, frames);
	} else if (frames == EOF) {
		send_stream_message(stream, CLIENT_STREAM_EOF);
		return EOF;
	}
	return 0;
}

/* For playback streams, this handles the request for more samples by calling
 * the audio callback for the thread, and signalling the server that the samples
 * have been written. */
static int handle_playback_request(struct client_stream *stream,
				   size_t num_frames)
{
	uint8_t *buf;
	int frames;
	int rc;
	struct cras_stream_params *config;
	struct audio_message aud_msg;

	config = stream->config;

	/* If this message is for an input stream, log error and drop it. */
	if (stream->direction != CRAS_STREAM_OUTPUT) {
		syslog(LOG_ERR, "Record data from output\n");
		return 0;
	}

	aud_msg.error = 0;

	/* If we need to do format conversion on this stream, use an
	 * intermediate buffer to store the samples so they can be converted. */
	if (stream->conv) {
		buf = stream->fmt_conv_buffer;
		num_frames = cras_fmt_conv_out_frames_to_in(stream->conv,
							    num_frames);
	} else
		buf = cras_shm_get_curr_write_buffer(stream->shm);

	/* Make sure not to ask for more frames than the buffer can hold. */
	if (num_frames > config->buffer_frames)
		num_frames = config->buffer_frames;

	/* Get samples from the user */
	frames = config->aud_cb(stream->client,
			stream->id,
			buf,
			num_frames,
			&stream->shm->ts,
			config->user_data);
	if (frames < 0) {
		send_stream_message(stream, CLIENT_STREAM_EOF);
		aud_msg.error = frames;
	} else {
		/* Possibly convert to the correct format. */
		if (stream->conv) {
			uint8_t *final_buf;
			final_buf = cras_shm_get_curr_write_buffer(stream->shm);
			frames = cras_fmt_conv_convert_frames(
					stream->conv,
					stream->fmt_conv_buffer,
					final_buf,
					frames);
		}
		/* And move the write pointer to indicate samples written. */
		cras_shm_check_write_overrun(stream->shm);
		cras_shm_buffer_written(stream->shm, frames);
		cras_shm_buffer_write_complete(stream->shm);
	}

	/* Signal server that data is ready, or that an error has occurred. */
	aud_msg.id = AUDIO_MESSAGE_DATA_READY;
	aud_msg.frames = frames;
	rc = write(stream->aud_fd, &aud_msg, sizeof(aud_msg));
	if (rc != sizeof(aud_msg))
		return -EPIPE;
	return aud_msg.error;
}

/* Listens to the audio socket for messages from the server indicating that
 * the stream needs to be serviced.  One of these runs per stream. */
static void *audio_thread(void *arg)
{
	struct client_stream *stream = (struct client_stream *)arg;
	socklen_t address_length = 0;
	int thread_terminated = 0;
	struct audio_message aud_msg;
	int num_read;

	if (arg == NULL)
		return (void *)-EIO;

	if (cras_set_rt_scheduling(CRAS_CLIENT_RT_THREAD_PRIORITY) == 0)
		cras_set_thread_priority(CRAS_CLIENT_RT_THREAD_PRIORITY);

	syslog(LOG_DEBUG, "accept on socket");
	stream->aud_fd = accept(stream->connection_fd,
				  (struct sockaddr *)&stream->aud_address,
				   &address_length);
	if (stream->aud_fd < 0) {
		syslog(LOG_ERR, "Connecting audio socket.");
		send_stream_message(stream, CLIENT_STREAM_SOCKET_ERROR);
		return NULL;
	}

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
		default:
			syslog(LOG_ERR, "Unknown aud message %d\n", aud_msg.id);
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
static int config_shm(struct client_stream *stream, int key, size_t size)
{
	int shmid;

	shmid = shmget(key, size, 0600);
	if (shmid < 0) {
		syslog(LOG_ERR, "shmget failed to get shm for stream %x.",
		       stream->id);
		return shmid;
	}
	stream->shm = shmat(shmid, NULL, 0);
	if (stream->shm == (struct cras_audio_shm_area *) -1) {
		syslog(LOG_ERR, "shmat failed to attach shm for stream %x.",
		       stream->id);
		return errno;
	}

	return 0;
}

/* If the server cannot provide the requested format, configures an audio format
 * converter that handles transforming the input format to the format used by
 * the server. */
static int config_format_converter(struct client_stream *stream,
				   const struct cras_audio_format *hwfmt)
{
	struct cras_audio_format *sfmt = &stream->config->format;

	if (memcmp(sfmt, hwfmt, sizeof(*hwfmt)) != 0) {
		syslog(LOG_DEBUG,
		       "format convert %s: stream:%d %zu %zu hw: %d %zu %zu",
		       stream->direction ? "input" : "output",
		       sfmt->format, sfmt->frame_rate, sfmt->num_channels,
		       hwfmt->format, hwfmt->frame_rate, hwfmt->num_channels);

		/* Convert from the stream format to the h/w format for output,
		 * from h/w format to stream format for input. */
		if (stream->direction == CRAS_STREAM_OUTPUT) {
			stream->conv = cras_fmt_conv_create(
					sfmt,
					hwfmt,
					stream->config->buffer_frames);
		} else {
			assert(stream->direction == CRAS_STREAM_INPUT);
			stream->conv = cras_fmt_conv_create(
					hwfmt,
					sfmt,
					stream->config->buffer_frames);
		}
		if (stream->conv == NULL) {
			syslog(LOG_ERR, "Failed to create format converter");
			return -ENOMEM;
		}

		/* Need a buffer to keep samples before converting them. */
		stream->fmt_conv_buffer = malloc(stream->config->buffer_frames *
						 cras_get_format_bytes(sfmt));
		if (stream->fmt_conv_buffer == NULL) {
			cras_fmt_conv_destroy(stream->conv);
			return -ENOMEM;
		}
	}

	return 0;
}

/* Handles the stream connected message from the server.  Check if we need a
 * format converter, configure the shared memory region, and start the audio
 * thread that will handle requests from the server. */
static int stream_connected(struct client_stream *stream,
			    const struct cras_client_stream_connected *msg)
{
	int rc;

	if (msg->err) {
		syslog(LOG_ERR, "Error Setting up stream %d\n", msg->err);
		return msg->err;
	}

	rc = config_format_converter(stream, &msg->format);
	if (rc < 0) {
		syslog(LOG_ERR, "Error setting up format conversion");
		return rc;
	}

	rc = config_shm(stream, msg->shm_key, msg->shm_max_size);
	if (rc < 0) {
		syslog(LOG_ERR, "Error configuring shared memory");
		goto err_ret;
	}
	cras_shm_set_volume_scaler(stream->shm, stream->volume_scaler);

	rc = pipe(stream->wake_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Error piping");
		goto err_ret;
	}

	stream->thread.running = 1;

	rc = pthread_create(&stream->thread.tid, NULL, audio_thread, stream);
	if (rc) {
		syslog(LOG_ERR, "Couldn't create audio stream.");
		goto err_ret;
	}

	return 0;
err_ret:
	if (stream->shm)
		shmdt(stream->shm);
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
	struct cras_connect_message serv_msg;
	cras_stream_id_t new_id;
	struct client_stream *out;

	if (!check_server_connected_wait(client)) {
		syslog(LOG_ERR, "Add stream failed to connect to server.");
		return -EIO;
	}

	/* Find an available stream id. */
	do {
		new_id = cras_get_stream_id(client->id, client->next_stream_id);
		client->next_stream_id++;
		DL_SEARCH_SCALAR(client->streams, out, id, new_id);
	} while (out != NULL);

	stream->id = new_id;
	*stream_id_out = new_id;
	stream->client = client;

	/* Create a socket for the server to notify of audio events. */
	stream->aud_address.sun_family = AF_UNIX;
	snprintf(stream->aud_address.sun_path,
		 sizeof(stream->aud_address.sun_path), "%s/%s-%x",
		 client->sock_dir, CRAS_AUD_FILE_PATTERN, stream->id);
	unlink(stream->aud_address.sun_path);

	stream->connection_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (stream->connection_fd < 0) {
		syslog(LOG_ERR, "add_stream failed to socket.");
		return stream->connection_fd;
	}

	rc = fchmod(stream->connection_fd, 0700);
	if (rc < 0) {
		syslog(LOG_ERR, "add_stream failed to fchmod socket.");
		goto add_stream_failed;
	}

	rc = bind(stream->connection_fd,
		   (struct sockaddr *)&stream->aud_address,
		   sizeof(struct sockaddr_un));
	if (rc != 0) {
		syslog(LOG_ERR, "add_stream failed to bind.");
		goto add_stream_failed;
	}

	rc = set_socket_perms(stream->aud_address.sun_path);
	if (rc < 0) {
		syslog(LOG_ERR, "add_stream failed to set socket params.");
		goto add_stream_failed;
	}

	rc = listen(stream->connection_fd, 1);
	if (rc != 0) {
		syslog(LOG_ERR, "add_stream: Listen failed.");
		goto add_stream_failed;
	}

	/* Add the stream to the linked list and send a message to the server
	 * requesting that the stream be started. */
	DL_APPEND(client->streams, stream);

	cras_fill_connect_message(&serv_msg,
				  stream->config->direction,
				  stream->id,
				  stream->config->stream_type,
				  stream->config->buffer_frames,
				  stream->config->cb_threshold,
				  stream->config->min_cb_level,
				  stream->flags,
				  stream->config->format);
	rc = write(client->server_fd, &serv_msg, sizeof(serv_msg));
	if (rc != sizeof(serv_msg)) {
		syslog(LOG_ERR, "add_stream: Send server message failed.");
		DL_DELETE(client->streams, stream);
		goto add_stream_failed;
	}

	return 0;

add_stream_failed:
	close(stream->connection_fd);
	return rc;
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
		syslog(LOG_ERR, "error removing stream from server\n");

	/* And shut down locally. */
	if (stream->thread.running) {
		stream->thread.running = 0;
		wake_aud_thread(stream);
		pthread_join(stream->thread.tid, NULL);
	}

	if(unlink(stream->aud_address.sun_path))
		syslog(LOG_ERR, "unlink failed for stream %x", stream->id);

	if (stream->shm)
		shmdt(stream->shm);

	DL_DELETE(client->streams, stream);
	if (stream->aud_fd >= 0)
		if (close(stream->aud_fd))
			syslog(LOG_ERR, "Couldn't close audio socket");
	if(close(stream->connection_fd))
		syslog(LOG_ERR, "Couldn't close connection socket");
	if (stream->conv) {
		cras_fmt_conv_destroy(stream->conv);
		free(stream->fmt_conv_buffer);
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
	if (stream->shm != NULL)
		cras_shm_set_volume_scaler(stream->shm, volume_scaler);

	return 0;
}

/* Gets the list of output devices. */
static int client_thread_output_device_list(struct cras_client *client,
					    struct cras_iodev_info *devs,
					    size_t max_devs)
{
	const size_t num_devs = min(max_devs, client->num_output_devs);

	if (num_devs == 0)
		return 0;
	memcpy(devs, client->output_devs, num_devs * sizeof(*devs));
	return num_devs;
}

/* Gets the list of input devices. */
static int client_thread_input_device_list(struct cras_client *client,
					   struct cras_iodev_info *devs,
					   size_t max_devs)
{
	const size_t num_devs = min(max_devs, client->num_input_devs);

	if (num_devs == 0)
		return 0;
	memcpy(devs, client->input_devs, num_devs * sizeof(*devs));
	return num_devs;
}

/* Gets the list of clients attached to the server. */
static int client_thread_attached_client_device_list(
		struct cras_client *client,
		struct cras_attached_client_info *attached_clients,
		size_t max_clients)
{
	const size_t num_clients = min(max_clients,
				       client->num_attached_clients);

	if (num_clients > 0)
		memcpy(attached_clients, client->attached_clients,
		       num_clients * sizeof(*attached_clients));
	return num_clients;
}

/* Re-attaches a stream that was removed on the server side so that it could be
 * moved to a new device. To achieve this, remove the stream and send the
 * connect message again. */
static int handle_stream_reattach(struct cras_client *client,
				  cras_stream_id_t stream_id)
{
	struct cras_connect_message serv_msg;
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

	if (stream->conv != NULL) {
		cras_fmt_conv_destroy(stream->conv);
		free(stream->fmt_conv_buffer);
	}
	stream->conv = NULL;
	if (stream->aud_fd >= 0)
		close(stream->aud_fd);
	if (stream->shm)
		shmdt(stream->shm);

	/* Now re-connect the stream and wait for a connected message. */
	cras_fill_connect_message(&serv_msg,
				  stream->config->direction,
				  stream->id,
				  stream->config->stream_type,
				  stream->config->buffer_frames,
				  stream->config->cb_threshold,
				  stream->config->min_cb_level,
				  stream->flags,
				  stream->config->format);
	rc = write(client->server_fd, &serv_msg, sizeof(serv_msg));
	if (rc != sizeof(serv_msg)) {
		if (stream->connection_fd < 0)
			close(stream->connection_fd);
		free(stream->config);
		free(stream);
		return rc;
	}

	return 0;
}

/* Handles a new list of iodevs. */
static int handle_new_iodev_list(struct cras_client *client,
				 struct cras_client_iodev_list *msg)
{
	free(client->input_devs);
	client->input_devs = NULL;
	free(client->output_devs);
	client->output_devs = NULL;

	client->num_output_devs = msg->num_outputs;
	if (client->num_output_devs > 0) {
		size_t output_size = sizeof(client->output_devs[0]) *
				client->num_output_devs;
		client->output_devs = malloc(output_size);
		if (client->output_devs == NULL)
			return -ENOMEM;
		memcpy(client->output_devs, &msg->iodevs[0], output_size);
	}
	client->num_input_devs = msg->num_inputs;
	if (client->num_input_devs > 0) {
		size_t input_size = sizeof(client->input_devs[0]) *
				client->num_input_devs;
		client->input_devs = malloc(input_size);
		if (client->input_devs == NULL) {
			free(client->output_devs);
			return -ENOMEM;
		}
		memcpy(client->input_devs,
		       &msg->iodevs[client->num_output_devs],
		       input_size);
	}
	return 0;
}

/* Handles a new list of attached clients. */
static int handle_new_attached_clients_list(
		struct cras_client *client,
		struct cras_client_client_list *msg)
{
	free(client->attached_clients);
	client->attached_clients = NULL;

	client->num_attached_clients = msg->num_attached_clients;
	if (client->num_attached_clients > 0) {
		size_t size = sizeof(client->attached_clients[0]) *
				client->num_attached_clients;
		client->attached_clients = malloc(size);
		if (client->attached_clients == NULL)
			return -ENOMEM;
		memcpy(client->attached_clients, &msg->client_info[0], size);
	}
	return 0;
}

/* Handles new volume state. */
static int handle_system_volume(struct cras_client *client,
				struct cras_client_volume_status *msg)
{
	client->system_volume = msg->volume;
	client->system_muted = !!msg->muted;
	client->system_capture_gain = msg->capture_gain;
	client->system_capture_muted = !!msg->capture_muted;
	client->system_min_volume = msg->volume_min_dBFS;
	client->system_max_volume = msg->volume_max_dBFS;
	client->system_min_capture_gain = msg->capture_gain_min_dBFS;
	client->system_max_capture_gain = msg->capture_gain_max_dBFS;
	return 0;
}

/* Handles messages from the cras server. */
static int handle_message_from_server(struct cras_client *client)
{
	uint8_t *buf = NULL;
	size_t msg_length;
	struct cras_client_message *msg;
	int rc = 0;
	int nread;

	nread = read(client->server_fd, &msg_length, sizeof(msg_length));
	if (nread <= 0)
		goto read_error;

	buf = malloc(msg_length);
	if (buf == NULL)
		goto read_error;
	msg = (struct cras_client_message *)buf;

	msg->length = msg_length;
	nread = read(client->server_fd, buf + nread, msg->length - nread);
	if (nread <= 0)
		goto read_error;

	switch (msg->id) {
	case CRAS_CLIENT_CONNECTED: {
		struct cras_client_connected *cmsg =
			(struct cras_client_connected *)msg;
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
	case CRAS_CLIENT_IODEV_LIST: {
		struct cras_client_iodev_list *cmsg =
			(struct cras_client_iodev_list *)msg;
		handle_new_iodev_list(client, cmsg);
		break;
	}
	case CRAS_CLIENT_VOLUME_UPDATE: {
		struct cras_client_volume_status *vmsg =
			(struct cras_client_volume_status *)msg;
		handle_system_volume(client, vmsg);
		break;
	}
	case CRAS_CLIENT_CLIENT_LIST_UPDATE:{
		struct cras_client_client_list *cmsg =
			(struct cras_client_client_list *)msg;
		handle_new_attached_clients_list(client, cmsg);
		break;
	}
	default:
		syslog(LOG_ERR, "Receive unknown command %d", msg->id);
		break;
	}

	free(buf);
	return 0;
read_error:
	rc = connect_to_server_wait(client);
	if (rc < 0) {
		syslog(LOG_ERR, "Can't read from server\n");
		free(buf);
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

	if (!check_server_connected_wait(client))
		if (connect_to_server_wait(client) < 0) {
			syslog(LOG_ERR, "Lost server connection.");
			rc = -EIO;
			goto cmd_msg_complete;
		}

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

	switch (msg->msg_id) {
	case CLIENT_STOP: {
		struct client_stream *s, *tmp;

		/* Stop all playing streams */
		DL_FOREACH_SAFE(client->streams, s, tmp)
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
	case CLIENT_GET_OUTPUT_DEVICE_LIST: {
		struct get_device_list_message *dev_msg =
			(struct get_device_list_message *)msg;
		rc = client_thread_output_device_list(client, dev_msg->devs,
						      dev_msg->max_devs);
		break;
	}
	case CLIENT_GET_INPUT_DEVICE_LIST: {
		struct get_device_list_message *dev_msg =
			(struct get_device_list_message *)msg;
		rc = client_thread_input_device_list(client, dev_msg->devs,
						     dev_msg->max_devs);
		break;
	}
	case CLIENT_GET_SYSTEM_VOLUME: {
		rc = client->system_volume;
		break;
	}
	case CLIENT_GET_SYSTEM_CAPTURE_GAIN: {
		rc = client->system_capture_gain;
		break;
	}
	case CLIENT_GET_SYSTEM_MUTED: {
		rc = client->system_muted;
		break;
	}
	case CLIENT_GET_SYSTEM_CAPTURE_MUTED: {
		rc = client->system_capture_muted;
		break;
	}
	case CLIENT_GET_ATTACHED_CLIENT_LIST: {
		struct get_attached_client_list_message *client_msg =
			(struct get_attached_client_list_message *)msg;
		rc = client_thread_attached_client_device_list(
				client,
				client_msg->clients,
				client_msg->max_clients);
		break;
	}
	case CLIENT_GET_SYSTEM_MIN_VOLUME: {
		rc = client->system_min_volume;
		break;
	}
	case CLIENT_GET_SYSTEM_MAX_VOLUME: {
		rc = client->system_max_volume;
		break;
	}
	case CLIENT_GET_SYSTEM_MIN_CAPTURE_GAIN:
		rc = client->system_min_capture_gain;
		break;
	case CLIENT_GET_SYSTEM_MAX_CAPTURE_GAIN:
		rc = client->system_max_capture_gain;
		break;
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
	if (rc != msg->len)
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
	if (write(client->server_fd, msg, msg->length) != msg->length) {
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
		if (write(client->server_fd, msg, msg->length) != msg->length)
			return -EINVAL;
	}
	return 0;
}

static int get_device_list(struct cras_client *client,
			   struct cras_iodev_info *devs,
			   size_t max_devs,
			   size_t msg_id)
{
	struct get_device_list_message msg;
	if (client == NULL || !client->thread.running)
		return -EINVAL;

	msg.header.len = sizeof(msg);
	msg.header.msg_id = msg_id;
	msg.devs = devs;
	msg.max_devs = max_devs;
	return send_command_message(client, &msg.header);
}

/*
 * Exported Client Interface
 */

int cras_client_create(struct cras_client **client)
{
	int rc;

	*client = calloc(1, sizeof(struct cras_client));
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

	return 0;
free_error:
	free(*client);
	*client = NULL;
	return rc;
}

void cras_client_destroy(struct cras_client *client)
{
	if (client == NULL)
		return;
	if (client->server_fd >= 0)
		close(client->server_fd);
	close(client->command_fds[0]);
	close(client->command_fds[1]);
	close(client->stream_fds[0]);
	close(client->stream_fds[1]);
	free(client->output_devs);
	free(client->input_devs);
	free(client->attached_clients);
	free(client);
}

int cras_client_connect(struct cras_client *client)
{
	return connect_to_server(client);
}

int cras_client_connect_wait(struct cras_client *client)
{
	return connect_to_server_wait(client);
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

	params = malloc(sizeof(*params));
	if (params == NULL)
		return NULL;

	params->direction = direction;
	params->buffer_frames = buffer_frames;
	params->cb_threshold = cb_threshold;
	params->min_cb_level = min_cb_level;
	params->stream_type = stream_type;
	params->flags = flags;
	params->user_data = user_data;
	params->aud_cb = aud_cb;
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

	if (config->aud_cb == NULL || config->err_cb == NULL)
		return -EINVAL;

	/* For input cb_threshold is buffer size. */
	if (config->direction == CRAS_STREAM_INPUT)
		config->cb_threshold = config->buffer_frames;

	stream = calloc(1, sizeof(*stream));
	if (stream == NULL) {
		rc = -ENOMEM;
		goto add_failed;
	}
	stream->config = malloc(sizeof(*(stream->config)));
	if (stream->config == NULL) {
		rc = -ENOMEM;
		goto add_failed;
	}
	memcpy(stream->config, config, sizeof(*config));
	stream->aud_fd = -1;
	stream->connection_fd = -1;
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
			     int iodev)
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

int cras_client_set_system_capture_mute(struct cras_client *client, int mute)
{
	struct cras_set_system_mute msg;

	if (client == NULL)
		return -EINVAL;

	cras_fill_set_system_capture_mute(&msg, mute);
	return write_message_to_server(client, &msg.header);
}

size_t cras_client_get_system_volume(struct cras_client *client)
{
	/* Send message to client thread to ensure data is synchronized. */
	return send_simple_cmd_msg(client, 0, CLIENT_GET_SYSTEM_VOLUME);
}

long cras_client_get_system_capture_gain(struct cras_client *client)
{
	/* Send message to client thread to ensure data is synchronized. */
	return send_simple_cmd_msg(client, 0, CLIENT_GET_SYSTEM_CAPTURE_GAIN);
}

int cras_client_get_system_muted(struct cras_client *client)
{
	/* Send message to client thread to ensure data is synchronized. */
	return send_simple_cmd_msg(client, 0, CLIENT_GET_SYSTEM_MUTED);
}

int cras_client_get_system_capture_muted(struct cras_client *client)
{
	/* Send message to client thread to ensure data is synchronized. */
	return send_simple_cmd_msg(client, 0, CLIENT_GET_SYSTEM_CAPTURE_MUTED);
}

long cras_client_get_system_min_volume(struct cras_client *client)
{
	return send_simple_cmd_msg(client, 0, CLIENT_GET_SYSTEM_MIN_VOLUME);
}

long cras_client_get_system_max_volume(struct cras_client *client)
{
	return send_simple_cmd_msg(client, 0, CLIENT_GET_SYSTEM_MAX_VOLUME);
}

long cras_client_get_system_min_capture_gain(struct cras_client *client)
{
	return send_simple_cmd_msg(client, 0,
				   CLIENT_GET_SYSTEM_MIN_CAPTURE_GAIN);
}

long cras_client_get_system_max_capture_gain(struct cras_client *client)
{
	return send_simple_cmd_msg(client, 0,
				   CLIENT_GET_SYSTEM_MAX_CAPTURE_GAIN);
}

int cras_client_notify_device(struct cras_client *client,
			      unsigned action,
			      unsigned card_number,
			      unsigned device_number,
			      unsigned active,
			      unsigned internal,
			      unsigned primary)
{
	struct cras_notify_device_info msg;

	if (client == NULL)
		return -EINVAL;

	cras_set_device_info(&msg, action, card_number, device_number,
			     active, internal, primary);
	return write_message_to_server(client, &msg.header);
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

int cras_client_get_output_devices(struct cras_client *client,
				   struct cras_iodev_info *devs,
				   size_t max_devs)
{
	return get_device_list(client,
			       devs,
			       max_devs,
			       CLIENT_GET_OUTPUT_DEVICE_LIST);
}

int cras_client_get_input_devices(struct cras_client *client,
				  struct cras_iodev_info *devs,
				  size_t max_devs)
{
	return get_device_list(client,
			       devs,
			       max_devs,
			       CLIENT_GET_INPUT_DEVICE_LIST);
}

int cras_client_get_attached_clients(struct cras_client *client,
				     struct cras_attached_client_info *clients,
				     size_t max_clients)
{
	struct get_attached_client_list_message msg;
	if (client == NULL || !client->thread.running)
		return -EINVAL;

	msg.header.len = sizeof(msg);
	msg.header.msg_id = CLIENT_GET_ATTACHED_CLIENT_LIST;
	msg.clients = clients;
	msg.max_clients = max_clients;
	return send_command_message(client, &msg.header);
}

int cras_client_format_bytes_per_frame(struct cras_audio_format *fmt)
{
	if (fmt == NULL)
		return -EINVAL;

	return cras_get_format_bytes(fmt);
}

/* Deprecated, TODO(dgreid) delete me */
int cras_client_bytes_per_frame(struct cras_client *client,
				cras_stream_id_t stream_id)
{
	struct client_stream *stream;

	if (client == NULL)
		return -EINVAL;

	stream = stream_from_id(client, stream_id);
	if (stream == NULL || stream->config == NULL)
		return 0;
	return cras_get_format_bytes(&stream->config->format);
}

int cras_client_calc_latency(const struct cras_client *client,
			     cras_stream_id_t stream_id,
			     const struct timespec *sample_time,
			     struct timespec *delay)
{
	struct timespec now;
	struct client_stream *stream;

	if (client == NULL)
		return -EINVAL;

	stream = stream_from_id(client, stream_id);
	if (stream == NULL)
		return -EINVAL;

	clock_gettime(CLOCK_MONOTONIC, &now);

	/* for input want time since sample read (now - t),
	 * for output time until sample played (t - now) */
	if (stream->direction == CRAS_STREAM_INPUT)
		subtract_timespecs(&now, sample_time, delay);
	else
		subtract_timespecs(sample_time, &now, delay);

	return 0;
}
