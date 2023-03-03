/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include "cras/src/server/cras_main_message.h"
#include "cras/src/server/cras_observer.h"

struct hotword_triggered_msg {
  struct cras_main_message header;
  int64_t tv_sec;
  int64_t tv_nsec;
};

// The following functions are called from audio thread.

static int init_hotword_triggered_msg(struct hotword_triggered_msg* msg) {
  struct timespec now;
  int ret;

  ret = clock_gettime(CLOCK_MONOTONIC, &now);
  if (ret < 0) {
    return -errno;
  }

  memset(msg, 0, sizeof(*msg));
  msg->header.type = CRAS_MAIN_HOTWORD_TRIGGERED;
  msg->header.length = sizeof(*msg);
  msg->tv_sec = now.tv_sec;
  msg->tv_nsec = now.tv_nsec;
  return 0;
}

int cras_hotword_send_triggered_msg() {
  struct hotword_triggered_msg msg = CRAS_MAIN_MESSAGE_INIT;
  int rc;

  rc = init_hotword_triggered_msg(&msg);
  if (rc < 0) {
    return rc;
  }

  rc = cras_main_message_send((struct cras_main_message*)&msg);
  if (rc < 0) {
    syslog(LOG_ERR, "Failed to send hotword triggered message!");
  }

  return rc;
}

// The following functions are called from main thread.

static void handle_hotword_message(struct cras_main_message* msg, void* arg) {
  struct hotword_triggered_msg* hotword_msg =
      (struct hotword_triggered_msg*)msg;

  cras_observer_notify_hotword_triggered(hotword_msg->tv_sec,
                                         hotword_msg->tv_nsec);
}

int cras_hotword_handler_init() {
  return cras_main_message_add_handler(CRAS_MAIN_HOTWORD_TRIGGERED,
                                       handle_hotword_message, NULL);
}
