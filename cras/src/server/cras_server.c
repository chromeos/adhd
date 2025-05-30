/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE  // Needed for Linux socket credential passing.

#include "cras/src/server/cras_server.h"

#include <dbus/dbus.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "cras/common/rust_common.h"
#include "cras/server/main_message.h"
#include "cras/server/platform/features/features.h"
#include "cras/server/platform/segmentation/segmentation.h"
#include "cras/server/s2/s2.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/server/cras_alert.h"
#include "cras/src/server/cras_alsa_helpers.h"
#include "cras/src/server/cras_audio_thread_monitor.h"
#include "cras/src/server/cras_bt_manager.h"
#include "cras/src/server/cras_dbus.h"
#include "cras/src/server/cras_dbus_control.h"
#include "cras/src/server/cras_device_monitor.h"
#include "cras/src/server/cras_dlc_manager.h"
#include "cras/src/server/cras_feature_monitor.h"
#include "cras/src/server/cras_hotword_handler.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_mix.h"
#include "cras/src/server/cras_non_empty_audio_handler.h"
#include "cras/src/server/cras_observer.h"
#include "cras/src/server/cras_rclient.h"
#include "cras/src/server/cras_rtc.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_stream_apm.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_tm.h"
#include "cras/src/server/cras_udev.h"
#include "cras_config.h"
#include "cras_messages.h"
#include "cras_types.h"
#include "cras_util.h"
#include "third_party/utlist/utlist.h"

// Store a list of clients that are attached to the server.
struct attached_client {
  // Unique identifier for this client.
  size_t id;
  // socket file descriptor used to communicate with client.
  int fd;
  // Process, user, and group ID of the client.
  struct ucred ucred;
  // rclient to handle messages from this client.
  struct cras_rclient* client;
  // Pointer to struct pollfd for this callback.
  struct pollfd* pollfd;
  struct attached_client *next, *prev;
};

/* Stores file descriptors to callback mappings for clients. Callback/fd/data
 * args are registered by clients.  When fd is ready, the callback will be
 * called on the main server thread and the callback data will be passed back to
 * it.  This allows the use of the main server loop instead of spawning a thread
 * to watch file descriptors.  The client can then read or write the fd.
 */
struct client_callback {
  // The file descriptor passed to select.
  int select_fd;
  // The function to call when fd is ready.
  void (*callback)(void* data, int revents);
  // Pointer passed to the callback.
  void* callback_data;
  // Pointer to struct pollfd for this callback.
  struct pollfd* pollfd;
  int deleted;
  // The events to poll for.
  int events;
  struct client_callback *prev, *next;
};

// Stores callback function and argument data to be executed later.
struct system_task {
  void (*callback)(void*);
  void* callback_data;
  struct system_task *next, *prev;
};

// A structure wraps data related to server socket.
struct server_socket {
  struct sockaddr_un addr;
  int fd;
  enum CRAS_CONNECTION_TYPE type;
};

// Local server data.
struct server_data {
  struct attached_client* clients_head;
  size_t num_clients;
  struct client_callback* client_callbacks;
  struct system_task* system_tasks;
  size_t num_client_callbacks;
  size_t next_client_id;
  struct server_socket server_sockets[CRAS_NUM_CONN_TYPE];
} server_instance;

// Cleanup a given server_socket
static void server_socket_cleanup(struct server_socket* socket) {
  if (socket && socket->fd >= 0) {
    close(socket->fd);
    socket->fd = -1;
    unlink(socket->addr.sun_path);
  }
}

/* Remove a client from the list and destroy it.  Calling rclient_destroy will
 * also free all the streams owned by the client */
static void remove_client(struct attached_client* client) {
  close(client->fd);
  DL_DELETE(server_instance.clients_head, client);
  server_instance.num_clients--;
  cras_rclient_destroy(client->client);
  free(client);
}

/* This is called when "select" indicates that the client has written data to
 * the socket.  Read out one message and pass it to the client message handler.
 */
static void handle_message_from_client(struct attached_client* client) {
  uint8_t buf[CRAS_SERV_MAX_MSG_SIZE];
  int nread;
  unsigned int num_fds = 2;
  int fds[num_fds];

  nread = cras_recv_with_fds(client->fd, buf, sizeof(buf), fds, &num_fds);
  if (nread < 0) {
    goto read_error;
  }
  if (cras_rclient_buffer_from_client(client->client, buf, nread, fds,
                                      num_fds) < 0) {
    goto read_error;
  }
  return;

read_error:
  for (int i = 0; i < num_fds; i++) {
    if (fds[i] >= 0) {
      close(fds[i]);
    }
  }
  switch (nread) {
    case 0:
      break;
    default:
      syslog(LOG_DEBUG, "read err [%d] '%s', removing client %zu", -nread,
             cras_strerror(-nread), client->id);
      break;
  }
  remove_client(client);
}

/* Discovers and fills in info about the client that can be obtained from the
 * socket. The pid of the attaching client identifies it in logs. */
static void fill_client_info(struct attached_client* client) {
  socklen_t ucred_length = sizeof(client->ucred);

  if (getsockopt(client->fd, SOL_SOCKET, SO_PEERCRED, &client->ucred,
                 &ucred_length)) {
    syslog(LOG_DEBUG, "Failed to get client socket info\n");
  }
}

// Fills the server_state with the current list of attached clients.
static void send_client_list_to_clients(struct server_data* serv) {
  struct attached_client* c;
  struct cras_attached_client_info* info;
  struct cras_server_state* state;
  unsigned i;

  state = cras_system_state_update_begin();
  if (!state) {
    return;
  }

  state->num_attached_clients =
      MIN(CRAS_MAX_ATTACHED_CLIENTS, serv->num_clients);

  info = state->client_info;
  i = 0;
  DL_FOREACH (serv->clients_head, c) {
    info->id = c->id;
    info->pid = c->ucred.pid;
    info->uid = c->ucred.uid;
    info->gid = c->ucred.gid;
    info++;
    if (++i == CRAS_MAX_ATTACHED_CLIENTS) {
      break;
    }
  }

  cras_system_state_update_complete();
}

/* Handles requests from a client to attach to the server.  Create a local
 * structure to track the client, assign it a unique id and let it attach */
static void handle_new_connection(struct server_socket* server_socket) {
  int connection_fd;
  struct attached_client* poll_client;
  socklen_t address_length;

  poll_client = malloc(sizeof(*poll_client));
  if (poll_client == NULL) {
    syslog(LOG_ERR, "Allocating poll_client");
    return;
  }

  memset(&address_length, 0, sizeof(address_length));
  connection_fd =
      accept(server_socket->fd, (struct sockaddr*)&server_socket->addr,
             &address_length);
  if (connection_fd < 0) {
    syslog(LOG_WARNING, "connecting");
    free(poll_client);
    return;
  }

  // find next available client id
  while (1) {
    struct attached_client* out;
    DL_SEARCH_SCALAR(server_instance.clients_head, out, id,
                     server_instance.next_client_id);
    poll_client->id = server_instance.next_client_id;
    server_instance.next_client_id++;
    if (out == NULL) {
      break;
    }
  }

  // When full, getting an error is preferable to blocking.
  cras_make_fd_nonblocking(connection_fd);

  poll_client->fd = connection_fd;
  poll_client->next = NULL;
  poll_client->pollfd = NULL;
  fill_client_info(poll_client);

  poll_client->client =
      cras_rclient_create(connection_fd, poll_client->id, server_socket->type);
  if (poll_client->client == NULL) {
    syslog(LOG_WARNING, "failed to create client");
    goto error;
  }

  DL_APPEND(server_instance.clients_head, poll_client);
  server_instance.num_clients++;
  // Send a current list of available inputs and outputs.
  cras_iodev_list_update_device_list();
  send_client_list_to_clients(&server_instance);
  return;
error:
  close(connection_fd);
  free(poll_client);
  return;
}

/* Add a file descriptor to be passed to select in the main loop. This is
 * registered with system state so that it is called when any client asks to
 * have a callback triggered based on an fd being readable. */
static int add_select_fd(int fd,
                         void (*cb)(void* data, int events),
                         void* callback_data,
                         int events,
                         void* server_data) {
  struct client_callback* new_cb;
  struct client_callback* client_cb;
  struct server_data* serv;

  serv = (struct server_data*)server_data;
  if (serv == NULL) {
    return -EINVAL;
  }

  // Check if fd already exists.
  DL_FOREACH (serv->client_callbacks, client_cb) {
    if (client_cb->select_fd == fd && !client_cb->deleted) {
      return -EEXIST;
    }
  }

  new_cb = (struct client_callback*)calloc(1, sizeof(*new_cb));
  if (new_cb == NULL) {
    return -ENOMEM;
  }

  new_cb->select_fd = fd;
  new_cb->callback = cb;
  new_cb->callback_data = callback_data;
  new_cb->deleted = 0;
  new_cb->events = events;
  new_cb->pollfd = NULL;

  DL_APPEND(serv->client_callbacks, new_cb);
  server_instance.num_client_callbacks++;
  return 0;
}

/* Removes a file descriptor to be passed to select in the main loop. This is
 * registered with system state so that it is called when any client asks to
 * remove a callback added with add_select_fd. */
static void rm_select_fd(int fd, void* server_data) {
  struct server_data* serv;
  struct client_callback* client_cb;

  serv = (struct server_data*)server_data;
  if (serv == NULL) {
    return;
  }

  DL_FOREACH (serv->client_callbacks, client_cb) {
    if (client_cb->select_fd == fd) {
      client_cb->deleted = 1;
    }
  }
}

/* Creates a new task entry and append to system_tasks list, which will be
 * executed in main loop later without wait time.
 */
static int add_task(void (*cb)(void* data),
                    void* callback_data,
                    void* server_data) {
  struct server_data* serv;
  struct system_task* new_task;

  serv = (struct server_data*)server_data;
  if (serv == NULL) {
    return -EINVAL;
  }

  new_task = (struct system_task*)calloc(1, sizeof(*new_task));
  if (new_task == NULL) {
    return -ENOMEM;
  }

  new_task->callback = cb;
  new_task->callback_data = callback_data;

  DL_APPEND(serv->system_tasks, new_task);
  return 0;
}

/* Cleans up the file descriptor list removing items deleted during the main
 * loop iteration. */
static void cleanup_select_fds(void* server_data) {
  struct server_data* serv;
  struct client_callback* client_cb;

  serv = (struct server_data*)server_data;
  if (serv == NULL) {
    return;
  }

  DL_FOREACH (serv->client_callbacks, client_cb) {
    if (client_cb->deleted) {
      DL_DELETE(serv->client_callbacks, client_cb);
      server_instance.num_client_callbacks--;
      free(client_cb);
    }
  }
}

/* Checks whether the internal card is present. */
void check_internal_card(struct cras_timer* t, void* second) {
  cras_server_metrics_internal_soundcard_status(
      cras_system_state_internal_cards_detected(), (int)(intptr_t)second);
}

/*
 * Exported Interface.
 */

int cras_server_init() {
  // Log to syslog.
  openlog("cras_server", LOG_PID | LOG_PERROR, LOG_USER);
  if (cras_rust_init_logging()) {
    syslog(LOG_ERR, "cannot initialize logging in cras_rust");
  }
  cras_rust_register_panic_hook();
  cras_alsa_lib_error_handler_init();

  server_instance.next_client_id = RESERVED_CLIENT_IDS;

  // Initialize global observer.
  cras_observer_server_init();

  // init mixer with CPU capabilities
  cras_mix_init();

  /* Allow clients to register callbacks for file descriptors.
   * add_select_fd and rm_select_fd will add and remove file descriptors
   * from the list that are passed to select in the main loop below. */
  cras_system_set_select_handler(add_select_fd, rm_select_fd, &server_instance);
  cras_system_set_add_task_handler(add_task, &server_instance);

  int main_message_fd = cras_main_message_init();
  cras_system_add_select_fd(main_message_fd, handle_main_messages, NULL,
                            POLLIN);

  // Initializes all server_sockets
  for (int conn_type = 0; conn_type < CRAS_NUM_CONN_TYPE; conn_type++) {
    server_instance.server_sockets[conn_type].fd = -1;
  }

  // Initialize the cras_features backend.
  cras_features_init();
  cras_s2_set_ap_nc_segmentation_allowed(
      cras_segmentation_enabled("FeatureManagementAPNoiseCancellation"));

  return 0;
}

/*
 * Creates a server socket with given connection type and listens on it.
 * The socket_file will be created under cras_config_get_system_socket_file_dir
 * with permission=0770. The socket_fd will be listened with parameter
 * backlog=5.
 *
 * Returns 0 on success and leaves the created fd and the address information
 * in server_socket.
 * When error occurs, the created fd will be closed and the file path will be
 * unlinked and returns negative error code.
 */
static int create_and_listen_server_socket(
    enum CRAS_CONNECTION_TYPE conn_type,
    struct server_socket* server_socket) {
  int socket_fd = -1;
  int rc = 0;
  struct sockaddr_un* addr = &server_socket->addr;

  socket_fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
  if (socket_fd < 0) {
    syslog(LOG_ERR, "Main server socket failed.");
    rc = socket_fd;
    goto error;
  }

  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  rc = cras_fill_socket_path(conn_type, addr->sun_path);
  if (rc < 0) {
    goto error;
  }
  unlink(addr->sun_path);

  /* Linux quirk: calling fchmod before bind, sets the permissions of the
   * file created by bind, leaving no window for it to be modified. Start
   * with very restricted permissions. */
  rc = fchmod(socket_fd, 0700);
  if (rc < 0) {
    goto error;
  }

  rc = bind(socket_fd, (struct sockaddr*)addr, sizeof(*addr));
  if (rc < 0) {
    syslog(LOG_ERR, "Bind to server socket failed.");
    rc = -errno;
    goto error;
  }

  // Let other members in our group play audio through this socket.
  rc = chmod(addr->sun_path, 0770);
  if (rc < 0) {
    goto error;
  }

  if (listen(socket_fd, 5) != 0) {
    syslog(LOG_ERR, "Listen on server socket failed.");
    rc = -errno;
    goto error;
  }

  server_socket->fd = socket_fd;
  server_socket->type = conn_type;
  return 0;
error:
  if (socket_fd >= 0) {
    close(socket_fd);
    unlink(addr->sun_path);
  }
  return rc;
}

// Cleans up all server_socket in server_instance
static void cleanup_server_sockets() {
  for (int conn_type = 0; conn_type < CRAS_NUM_CONN_TYPE; conn_type++) {
    server_socket_cleanup(&server_instance.server_sockets[conn_type]);
  }
}

int cras_server_run(unsigned int profile_disable_mask) {
  DBusConnection* dbus_conn;
  int rc = 0;
  struct attached_client* elm;
  struct client_callback* client_cb;
  struct system_task* tasks;
  struct system_task* system_task;
  struct cras_tm* tm;
  struct timespec ts, *poll_timeout;
  int timers_active;
  struct pollfd* pollfds;
  struct pollfd* pollfds_tmp;
  unsigned int pollfds_size = 32;
  unsigned int num_pollfds, poll_size_needed;

  pollfds = malloc(sizeof(*pollfds) * pollfds_size);

  if (!pollfds) {
    return -ENOMEM;
  }

  cras_udev_start_sound_subsystem_monitor();

  rc = cras_alert_init();
  if (rc < 0) {
    goto bail;
  }

  rc = cras_server_metrics_init();
  if (rc < 0) {
    goto bail;
  }

  rc = cras_device_monitor_init();
  if (rc < 0) {
    goto bail;
  }

  rc = cras_hotword_handler_init();
  if (rc < 0) {
    goto bail;
  }

  rc = cras_non_empty_audio_handler_init();
  if (rc < 0) {
    goto bail;
  }

  rc = cras_audio_thread_monitor_init();
  if (rc < 0) {
    goto bail;
  }

  rc = cras_stream_apm_message_handler_init();
  if (rc < 0) {
    goto bail;
  }

  rc = cras_feature_monitor_init();
  if (rc < 0) {
    goto bail;
  }

  rc = cras_rtc_init();
  if (rc < 0) {
    goto bail;
  }

  // `cras_dlc_manager` writes information that can be queried by dbus call, so
  // we initialize it before initializing dbus threads.
  cras_dlc_manager_init();

  rc = dbus_threads_init_default();
  if (!rc) {
    goto bail;
  }
  dbus_conn = cras_dbus_connect_system_bus();
  if (dbus_conn) {
    cras_bt_start(dbus_conn, profile_disable_mask);
    cras_dbus_control_start(dbus_conn);
  }

  for (int conn_type = 0; conn_type < CRAS_NUM_CONN_TYPE; conn_type++) {
    rc = create_and_listen_server_socket(
        conn_type, &server_instance.server_sockets[conn_type]);
    if (rc < 0) {
      goto bail;
    }
  }

  tm = cras_system_state_get_tm();
  if (!tm) {
    syslog(LOG_ERR, "Getting timer manager.");
    rc = -ENOMEM;
    goto bail;
  }

  // After 5 and 10s, make sure there is an internal soundcard probed.
  cras_tm_create_timer(tm, 5000, check_internal_card, (void*)5);
  cras_tm_create_timer(tm, 10000, check_internal_card, (void*)10);
  cras_tm_create_timer(tm, 30000, check_internal_card, (void*)30);

  // Main server loop - client callbacks are run from this context.
  while (1) {
    poll_size_needed = CRAS_NUM_CONN_TYPE + server_instance.num_clients +
                       server_instance.num_client_callbacks;
    if (poll_size_needed > pollfds_size) {
      pollfds_size = 2 * poll_size_needed;
      pollfds_tmp = realloc(pollfds, sizeof(*pollfds) * pollfds_size);
      if (!pollfds_tmp) {
        rc = -ENOMEM;
        goto bail;
      }
      pollfds = pollfds_tmp;
    }

    for (int conn_type = 0; conn_type < CRAS_NUM_CONN_TYPE; conn_type++) {
      pollfds[conn_type].fd = server_instance.server_sockets[conn_type].fd;
      pollfds[conn_type].events = POLLIN;
    }
    num_pollfds = CRAS_NUM_CONN_TYPE;

    DL_FOREACH (server_instance.clients_head, elm) {
      pollfds[num_pollfds].fd = elm->fd;
      pollfds[num_pollfds].events = POLLIN;
      elm->pollfd = &pollfds[num_pollfds];
      num_pollfds++;
    }
    DL_FOREACH (server_instance.client_callbacks, client_cb) {
      if (client_cb->deleted) {
        continue;
      }
      pollfds[num_pollfds].fd = client_cb->select_fd;
      pollfds[num_pollfds].events = client_cb->events;
      client_cb->pollfd = &pollfds[num_pollfds];
      num_pollfds++;
    }

    tasks = server_instance.system_tasks;
    server_instance.system_tasks = NULL;
    DL_FOREACH (tasks, system_task) {
      system_task->callback(system_task->callback_data);
      DL_DELETE(tasks, system_task);
      free(system_task);
    }

    timers_active = cras_tm_get_next_timeout(tm, &ts);

    /*
     * If new client task has been scheduled, no need to wait
     * for timeout, just do another loop to execute them.
     */
    if (server_instance.system_tasks) {
      poll_timeout = NULL;
    } else {
      poll_timeout = timers_active ? &ts : NULL;
    }

    rc = ppoll(pollfds, num_pollfds, poll_timeout, NULL);
    if (rc < 0) {
      continue;
    }

    cras_tm_call_callbacks(tm);

    // Check for new connections.
    for (int conn_type = 0; conn_type < CRAS_NUM_CONN_TYPE; conn_type++) {
      if (pollfds[conn_type].revents & POLLIN) {
        handle_new_connection(&server_instance.server_sockets[conn_type]);
      }
    }

    // Check if there are messages pending for any clients.
    DL_FOREACH (server_instance.clients_head, elm) {
      if (elm->pollfd && elm->pollfd->revents & POLLIN) {
        handle_message_from_client(elm);
      }
    }
    // Check any client-registered fd/callback pairs.
    DL_FOREACH (server_instance.client_callbacks, client_cb) {
      if (!client_cb->deleted && client_cb->pollfd &&
          (client_cb->pollfd->revents & client_cb->events)) {
        client_cb->callback(client_cb->callback_data,
                            client_cb->pollfd->revents);
      }
    }

    cleanup_select_fds(&server_instance);

    if (dbus_conn) {
      cras_dbus_dispatch(dbus_conn);
    }

    cras_alert_process_all_pending_alerts();
  }

bail:
  cleanup_server_sockets();
  free(pollfds);
  cras_observer_server_free();
  cras_features_deinit();
  return rc;
}

void cras_server_send_to_all_clients(const struct cras_client_message* msg) {
  struct attached_client* client;

  DL_FOREACH (server_instance.clients_head, client) {
    cras_rclient_send_message(client->client, msg, NULL, 0);
  }
}
