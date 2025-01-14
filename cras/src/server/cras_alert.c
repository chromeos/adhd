/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_alert.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cras/server/cras_thread.h"
#include "cras/server/main_message.h"
#include "third_party/utlist/utlist.h"

// A list of callbacks for an alert
struct cras_alert_cb_list {
  cras_alert_cb callback;
  void* arg;
  struct cras_alert_cb_list *prev, *next;
};

// A list of data args to callbacks. Variable-length structure.
struct cras_alert_data {
  struct cras_alert_data *prev, *next;
  // This field must be the last in this structure.
  char buf[];
};

struct cras_alert {
  int pending;
  unsigned int flags;
  cras_alert_prepare prepare;
  struct cras_alert_cb_list* callbacks;
  struct cras_alert_data* data;
  struct cras_alert *prev, *next;
};

// A list of all alerts in the system
static struct cras_alert* all_alerts;
// If there is any alert pending.
static int has_alert_pending;

struct cras_alert* cras_alert_create(cras_alert_prepare prepare,
                                     unsigned int flags) {
  struct cras_alert* alert;
  alert = calloc(1, sizeof(*alert));
  if (!alert) {
    return NULL;
  }
  alert->prepare = prepare;
  alert->flags = flags;
  DL_APPEND(all_alerts, alert);
  return alert;
}

int cras_alert_add_callback(struct cras_alert* alert,
                            cras_alert_cb cb,
                            void* arg) {
  struct cras_alert_cb_list* alert_cb;

  if (cb == NULL) {
    return -EINVAL;
  }

  DL_FOREACH (alert->callbacks, alert_cb) {
    if (alert_cb->callback == cb && alert_cb->arg == arg) {
      return -EEXIST;
    }
  }

  alert_cb = calloc(1, sizeof(*alert_cb));
  if (alert_cb == NULL) {
    return -ENOMEM;
  }
  alert_cb->callback = cb;
  alert_cb->arg = arg;
  DL_APPEND(alert->callbacks, alert_cb);
  return 0;
}

int cras_alert_rm_callback(struct cras_alert* alert,
                           cras_alert_cb cb,
                           void* arg) {
  struct cras_alert_cb_list* alert_cb;

  DL_FOREACH (alert->callbacks, alert_cb) {
    if (alert_cb->callback == cb && alert_cb->arg == arg) {
      DL_DELETE(alert->callbacks, alert_cb);
      free(alert_cb);
      return 0;
    }
  }
  return -ENOENT;
}

/* Checks if the alert is pending, and invoke the prepare function and callbacks
 * if so. */
static void cras_alert_process(struct cras_alert* alert) {
  struct cras_alert_cb_list* cb;
  struct cras_alert_data* data;

  if (!alert->pending) {
    return;
  }

  alert->pending = 0;
  if (alert->prepare) {
    alert->prepare(alert);
  }

  if (!alert->data) {
    DL_FOREACH (alert->callbacks, cb) {
      cb->callback(cb->arg, NULL);
    }
  }

  // Have data arguments, pass each to the callbacks.
  DL_FOREACH (alert->data, data) {
    DL_FOREACH (alert->callbacks, cb) {
      cb->callback(cb->arg, (void*)data->buf);
    }
    DL_DELETE(alert->data, data);
    free(data);
  }
}

struct cras_alert_event {
  struct cras_alert* alert;
  // The data associated with the alert. May be NULL if the alert has no
  // associated data.
  struct cras_alert_data* data;
};

static void pending_event_on_main(struct cras_main_ctx* mctx,
                                  struct cras_alert_event event) {
  struct cras_alert* alert = event.alert;

  alert->pending = 1;
  has_alert_pending = 1;

  if (!event.data) {
    return;
  }

  struct cras_alert_data* d = event.data;

  if (!(alert->flags & CRAS_ALERT_FLAG_KEEP_ALL_DATA) && alert->data) {
    // There will never be more than one item in the list.
    free(alert->data);
    alert->data = NULL;
  }

  /* Even when there is only one item, it is important to use DL_APPEND
   * here so that d's next and prev pointers are setup correctly. */
  DL_APPEND(alert->data, d);
}

struct cras_alert_event_msg {
  struct cras_main_message header;
  struct cras_alert_event event;
};

static void pending_event(struct cras_alert_event event) {
  struct cras_main_ctx* mctx = get_main_ctx_or_null();
  if (mctx) {
    pending_event_on_main(mctx, event);
  } else {
    struct cras_alert_event_msg msg = {
        .header =
            {
                .length = sizeof(msg),
                .type = CRAS_MAIN_ALERT_EVENT,
            },
        .event = event,
    };
    cras_main_message_send(&msg.header);
  }
}

void cras_alert_pending(struct cras_alert* alert) {
  struct cras_alert_event event = {
      .alert = alert,
      .data = NULL,
  };
  pending_event(event);
}

void cras_alert_pending_data(struct cras_alert* alert,
                             void* data,
                             size_t data_size) {
  struct cras_alert_data* d =
      calloc(1, offsetof(struct cras_alert_data, buf) + data_size);
  memcpy(d->buf, data, data_size);

  struct cras_alert_event event = {
      .alert = alert,
      .data = d,
  };
  pending_event(event);
}

void cras_alert_process_all_pending_alerts() {
  struct cras_alert* alert;

  while (has_alert_pending) {
    has_alert_pending = 0;
    DL_FOREACH (all_alerts, alert) {
      cras_alert_process(alert);
    }
  }
}

void cras_alert_destroy(struct cras_alert* alert) {
  struct cras_alert_cb_list* cb;
  struct cras_alert_data* data;

  if (!alert) {
    return;
  }

  DL_FOREACH (alert->callbacks, cb) {
    DL_DELETE(alert->callbacks, cb);
    free(cb);
  }

  DL_FOREACH (alert->data, data) {
    DL_DELETE(alert->data, data);
    free(data);
  }

  alert->callbacks = NULL;
  DL_DELETE(all_alerts, alert);
  free(alert);
}

void cras_alert_destroy_all() {
  struct cras_alert* alert;
  DL_FOREACH (all_alerts, alert) {
    cras_alert_destroy(alert);
  }
}

void handle_alert_event_message(struct cras_main_message* msg, void* arg) {
  pending_event_on_main(checked_main_ctx(),
                        ((struct cras_alert_event_msg*)msg)->event);
}

int cras_alert_init() {
  return cras_main_message_add_handler(CRAS_MAIN_ALERT_EVENT,
                                       handle_alert_event_message, NULL);
}
