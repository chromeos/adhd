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
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>

#include "cras_client.h"
#include "cras_config.h"
#include "cras_fmt_conv.h"
#include "cras_messages.h"
#include "cras_shm.h"
#include "cras_types.h"
#include "cras_util.h"
#include "utlist.h"

static const size_t MAX_CMD_MSG_LEN = 256;

/* Commands sent from the user to the running client. */
enum {
	CLIENT_STOP,
	CLIENT_REMOVE_STREAM,
	CLIENT_SET_STREAM_VOLUME_SCALER,
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
	struct sockaddr_un aud_address;
	struct client_stream *prev, *next;
};

/* Represents a client used to communicate with the audio server.
 * id - unique identifier for this client.
 * server_fd Incoming messages from server.
 * stream_fds - Pipe for attached streams.
 * command_fds - Pipe for user commands to thread.
 * sock_dir - Directory where the local audio socket can be found.
 * running - The client thread will run while this is non zero.
 * next_stream_id - ID to give the next stream.
 * tid - Thread ID of the client thread started by "cras_client_run_thread".
 * command_barrier - Used to signal the completion of a command from the user.
 * last_command_result - Passes back the result of the last user command.
 * streams - Linked list of streams attached to this client.
 */
struct cras_client {
	unsigned id;
	int server_fd;
	int stream_fds[2];
	int command_fds[2];
	const char *sock_dir;
	struct thread_state thread;
	cras_stream_id_t next_stream_id;
	pthread_barrier_t command_barrier;
	int last_command_result;
	struct client_stream *streams;
};

/*
 * Local Helpers
 */

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

	config = stream->config;
	/* If this message is for an output stream, log error and drop it. */
	if (stream->direction != CRAS_STREAM_INPUT) {
		syslog(LOG_ERR, "Play data to input\n");
		return 0;
	}

	frames = config->aud_cb(stream->client,
				stream->id,
				cras_shm_get_curr_read_buffer(stream->shm),
				num_frames,
				&stream->shm->ts,
				config->user_data);
	if (frames > 0)
		cras_shm_buffer_read(stream->shm, frames);
	else if (frames == EOF) {
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
		buf = cras_fmt_conv_get_buffer(stream->conv);
		num_frames = cras_fmt_conv_out_frames_to_in(stream->conv,
							    num_frames);
	} else
		buf = cras_shm_get_curr_write_buffer(stream->shm);

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
			frames = cras_fmt_conv_convert_to(
					stream->conv,
					final_buf,
					frames);
		}
		/* And move the write pointer to indicate samples written. */
		cras_shm_buffer_written(stream->shm, frames);
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
				   const struct cras_audio_format *fmt)
{
	if (memcmp(&stream->config->format, fmt, sizeof(*fmt)) != 0) {
		stream->conv = cras_fmt_conv_create(
			&stream->config->format,
			fmt,
			stream->config->buffer_frames);
		if (stream->conv == NULL)
			return -ENOMEM;
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
	if (stream->conv)
		cras_fmt_conv_destroy(stream->conv);
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

	if (stream->conv != NULL)
		cras_fmt_conv_destroy(stream->conv);
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

/* Handles messages from the cras server. */
static int handle_server_message(struct cras_client *client)
{
	uint8_t buf[CRAS_SERV_MAX_MSG_SIZE];
	struct cras_message *msg;
	int rc = 0;

	if (read(client->server_fd, buf, sizeof(buf)) <= 0) {
		syslog(LOG_ERR, "Can't read from server\n");
		client->thread.running = 0;
		return -EIO;
	}

	msg = (struct cras_message *)buf;
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
			return 0;
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
	default:
		syslog(LOG_ERR, "Receive unknown command %d", msg->id);
		break;
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
	default:
		assert(0);
		break;
	}

cmd_msg_complete:
	client->last_command_result = rc;
	pthread_barrier_wait(&client->command_barrier);
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

	server_input.fd = client->server_fd;
	server_input.cb = handle_server_message;
	LL_APPEND(inputs, &server_input);
	command_input.fd = client->command_fds[0];
	command_input.cb = handle_command_message;
	LL_APPEND(inputs, &command_input);
	stream_input.fd = client->stream_fds[0];
	stream_input.cb = handle_stream_message;
	LL_APPEND(inputs, &stream_input);

	while (client->thread.running) {
		fd_set poll_set;
		struct client_input *curr_input;
		int max_fd;
		int rc;

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

	return NULL;
}

/* Sends a message to the client thread to complete an action requested by the
 * user.  Then waits for the action to complete and returns the result. */
static int send_command_message(struct cras_client *client,
				struct command_msg *msg)
{
	int rc;
	rc = write(client->command_fds[1], msg, msg->len);
	if (rc != msg->len)
		return -EPIPE;

	/* Wait for command to complete. */
	pthread_barrier_wait(&client->command_barrier);
	return client->last_command_result;
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
static int write_message_to_server(const struct cras_client *client,
				   const struct cras_message *msg)
{
	if (write(client->server_fd, msg, msg->length) != msg->length)
		return -EPIPE;
	return 0;
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
	rc = pthread_barrier_init(&(*client)->command_barrier, NULL, 1);
	if (rc != 0)
		goto free_error;

	rc = pipe((*client)->command_fds);
	if (rc < 0)
		goto free_error;
	rc = pipe((*client)->stream_fds);
	if (rc < 0)
		goto free_error;

	openlog("cras_client", LOG_PID, LOG_USER);

	return 0;
free_error:
	pthread_barrier_destroy(&(*client)->command_barrier);
	free(*client);
	*client = NULL;
	return rc;
}

void cras_client_destroy(struct cras_client *client)
{
	if (client == NULL)
		return;
	if (client->server_fd >= -1)
		close(client->server_fd);
	close(client->command_fds[0]);
	close(client->command_fds[1]);
	close(client->stream_fds[0]);
	close(client->stream_fds[1]);
	pthread_barrier_destroy(&(client->command_barrier));
	free(client);
}

int cras_client_connect(struct cras_client *client)
{
	struct sockaddr_un address;
	int rc = 0;

	client->server_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (client->server_fd < 0) {
		perror("socket\n");
		return client->server_fd;
	}

	memset(&address, 0, sizeof(struct sockaddr_un));

	address.sun_family = AF_UNIX;
	client->sock_dir = cras_config_get_socket_file_dir();
	if (!client->sock_dir)
		return -ENOMEM;
	snprintf(address.sun_path, sizeof(address.sun_path),
		 "%s/%s", client->sock_dir, CRAS_SOCKET_FILE);

	rc = connect(client->server_fd, (struct sockaddr *)&address,
		      sizeof(struct sockaddr_un));
	if (rc != 0) {
		perror("connect failed\n");
		return rc;
	}

	handle_server_message(client);

	return rc;
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
	struct cras_connect_message serv_msg;
	struct client_stream *stream;
	int rc = 0;
	cras_stream_id_t new_id;

	if (client == NULL || config == NULL)
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

	/* Find an available stream id. */
	while (1) {
		struct client_stream *out;

		new_id = cras_get_stream_id(client->id, client->next_stream_id);
		DL_SEARCH_SCALAR(client->streams, out, id, new_id);
		if (out == NULL)
			break;
		client->next_stream_id++;
	}
	stream->id = new_id;
	client->next_stream_id++;
	stream->client = client;
	stream->aud_fd = -1;
	stream->connection_fd = -1;
	stream->direction = config->direction;
	stream->volume_scaler = 1.0;

	/* Create a socket for the server to notify of audio events. */
	stream->aud_address.sun_family = AF_UNIX;
	snprintf(stream->aud_address.sun_path,
		 sizeof(stream->aud_address.sun_path), "%s/%s-%x",
		 client->sock_dir, CRAS_AUD_FILE_PATTERN, stream->id);
	unlink(stream->aud_address.sun_path);

	stream->connection_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (stream->connection_fd < 0)
		goto add_failed;

	rc = bind(stream->connection_fd,
		   (struct sockaddr *)&stream->aud_address,
		   sizeof(struct sockaddr_un));
	if (rc != 0)
		goto add_failed;

	rc = listen(stream->connection_fd, 1);
	if (rc != 0)
		goto add_failed;

	/* Add the stream to the linked list and send a message to the server
	 * requesting that the stream be started. */
	DL_APPEND(client->streams, stream);

	cras_fill_connect_message(&serv_msg,
				  config->direction,
				  stream->id,
				  config->stream_type,
				  config->buffer_frames,
				  config->cb_threshold,
				  config->min_cb_level,
				  stream->flags,
				  config->format);
	rc = write(client->server_fd, &serv_msg, sizeof(serv_msg));
	if (rc != sizeof(serv_msg))
		goto add_failed;

	*stream_id_out = stream->id;
	return 0;

add_failed:
	if (stream) {
		if (stream->connection_fd >= 0)
			close(stream->connection_fd);
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

	fill_cras_set_system_volume(&msg, volume);
	return write_message_to_server(client, &msg.header);
}

int cras_client_run_thread(struct cras_client *client)
{
	if (client == NULL)
		return -EINVAL;

	client->thread.running = 1;
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

	return 0;
}

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
