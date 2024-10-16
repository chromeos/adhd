/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_observer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cras/src/common/cras_observer_ops.h"
#include "cras/src/server/cras_alert.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras_types.h"
#include "third_party/utlist/utlist.h"

struct cras_observer_client {
  struct cras_observer_ops ops;
  void* context;
  struct cras_observer_client *next, *prev;
};

struct cras_observer_alerts {
  struct cras_alert* output_volume;
  struct cras_alert* output_mute;
  struct cras_alert* capture_gain;
  struct cras_alert* capture_mute;
  struct cras_alert* nodes;
  struct cras_alert* active_node;
  struct cras_alert* output_node_volume;
  struct cras_alert* node_left_right_swapped;
  struct cras_alert* input_node_gain;
  struct cras_alert* suspend_changed;
  struct cras_alert* hotword_triggered;
  /* If all events for active streams went through a single alert then
   * we might miss some because the alert code does not send every
   * alert message. To ensure that the event sent contains the correct
   * number of active streams per direction, make the alerts
   * per-direciton. */
  struct cras_alert* num_active_streams[CRAS_NUM_DIRECTIONS];
  struct cras_alert* num_non_chrome_output_streams;
  struct cras_alert* non_empty_audio_state_changed;
  struct cras_alert* bt_battery_changed;
  struct cras_alert* num_input_streams_with_permission;
  struct cras_alert* num_arc_streams;
  struct cras_alert* severe_underrun;
  struct cras_alert* underrun;
  struct cras_alert* general_survey;
  struct cras_alert* bluetooth_survey;
  struct cras_alert* output_proc_survey;
  struct cras_alert* speak_on_mute_detected;
  struct cras_alert* num_stream_ignore_ui_gains_changed;
  struct cras_alert* ewma_power_reported;
  struct cras_alert* sidetone_supported_changed;
  struct cras_alert* audio_effect_ui_appearance_changed;
};

struct cras_observer_server {
  struct cras_observer_alerts alerts;
  struct cras_observer_client* clients;
};

struct cras_observer_alert_data_volume {
  int32_t volume;
};

struct cras_observer_alert_data_mute {
  int muted;
  int user_muted;
  int mute_locked;
};

struct cras_observer_alert_data_active_node {
  enum CRAS_STREAM_DIRECTION direction;
  cras_node_id_t node_id;
};

struct cras_observer_alert_data_node_volume {
  cras_node_id_t node_id;
  int32_t volume;
};

struct cras_observer_alert_data_node_lr_swapped {
  cras_node_id_t node_id;
  int swapped;
};

struct cras_observer_alert_data_suspend {
  int suspended;
};

struct cras_observer_alert_data_streams {
  enum CRAS_STREAM_DIRECTION direction;
  uint32_t num_active_streams;
};

struct cras_observer_alert_data_num_non_chrome_output_streams {
  uint32_t num_non_chrome_output_streams;
};

struct cras_observer_alert_data_input_streams {
  uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE];
};

struct cras_observer_alert_data_num_arc_streams {
  uint32_t num_arc_streams;
};

struct cras_observer_alert_data_hotword_triggered {
  int64_t tv_sec;
  int64_t tv_nsec;
};

struct cras_observer_non_empty_audio_state {
  int non_empty;
};

struct cras_observer_alert_data_bt_battery_changed {
  const char* address;
  uint32_t level;
};

struct cras_observer_alert_data_bluetooth_survey {
  uint32_t bt_flags;
};

struct cras_observer_alert_data_general_survey {
  enum CRAS_STREAM_TYPE stream_type;
  enum CRAS_CLIENT_TYPE client_type;
  const char* node_type_pair;
};

struct cras_observer_alert_data_output_proc_survey {
  enum CRAS_NODE_TYPE node_type;
};

struct cras_observer_num_stream_ignore_ui_gains {
  int num;
};

struct cras_observer_alert_data_ewma_power_reported {
  double power;
};

struct cras_observer_alert_data_sidetone_supported_changed {
  bool supported;
};

struct cras_observer_alert_data_audio_effect_ui_appearance_changed {
  bool audio_effects_ready;
};

// Global observer instance.
static struct cras_observer_server* g_observer;

// Empty observer ops.
static struct cras_observer_ops g_empty_ops;

/*
 * Alert handlers for delayed callbacks.
 */

static void output_volume_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_volume* volume_data =
      (struct cras_observer_alert_data_volume*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.output_volume_changed) {
      client->ops.output_volume_changed(client->context, volume_data->volume);
    }
  }
}

static void output_mute_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_mute* mute_data =
      (struct cras_observer_alert_data_mute*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.output_mute_changed) {
      client->ops.output_mute_changed(client->context, mute_data->muted,
                                      mute_data->user_muted,
                                      mute_data->mute_locked);
    }
  }
}

static void capture_gain_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_volume* volume_data =
      (struct cras_observer_alert_data_volume*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.capture_gain_changed) {
      client->ops.capture_gain_changed(client->context, volume_data->volume);
    }
  }
}

static void capture_mute_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_mute* mute_data =
      (struct cras_observer_alert_data_mute*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.capture_mute_changed) {
      client->ops.capture_mute_changed(client->context, mute_data->muted,
                                       mute_data->mute_locked);
    }
  }
}

static void nodes_prepare(struct cras_alert* alert) {
  cras_iodev_list_update_device_list();
}

static void nodes_alert(void* arg, void* data) {
  struct cras_observer_client* client;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.nodes_changed) {
      client->ops.nodes_changed(client->context);
    }
  }
}

static void active_node_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_active_node* node_data =
      (struct cras_observer_alert_data_active_node*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.active_node_changed) {
      client->ops.active_node_changed(client->context, node_data->direction,
                                      node_data->node_id);
    }
  }
}

static void output_node_volume_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_node_volume* node_data =
      (struct cras_observer_alert_data_node_volume*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.output_node_volume_changed) {
      client->ops.output_node_volume_changed(
          client->context, node_data->node_id, node_data->volume);
    }
  }
}

static void node_left_right_swapped_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_node_lr_swapped* node_data =
      (struct cras_observer_alert_data_node_lr_swapped*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.node_left_right_swapped_changed) {
      client->ops.node_left_right_swapped_changed(
          client->context, node_data->node_id, node_data->swapped);
    }
  }
}

static void input_node_gain_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_node_volume* node_data =
      (struct cras_observer_alert_data_node_volume*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.input_node_gain_changed) {
      client->ops.input_node_gain_changed(client->context, node_data->node_id,
                                          node_data->volume);
    }
  }
}

static void suspend_changed_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_suspend* suspend_data =
      (struct cras_observer_alert_data_suspend*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.suspend_changed) {
      client->ops.suspend_changed(client->context, suspend_data->suspended);
    }
  }
}

static void num_active_streams_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_streams* streams_data =
      (struct cras_observer_alert_data_streams*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.num_active_streams_changed) {
      client->ops.num_active_streams_changed(client->context,
                                             streams_data->direction,
                                             streams_data->num_active_streams);
    }
  }
}

static void num_non_chrome_output_streams_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_num_non_chrome_output_streams* streams_data =
      (struct cras_observer_alert_data_num_non_chrome_output_streams*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.num_non_chrome_output_streams_changed) {
      client->ops.num_non_chrome_output_streams_changed(
          client->context, streams_data->num_non_chrome_output_streams);
    }
  }
}

static void num_input_streams_with_permission_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_input_streams* input_streams_data =
      (struct cras_observer_alert_data_input_streams*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.num_input_streams_with_permission_changed) {
      client->ops.num_input_streams_with_permission_changed(
          client->context, input_streams_data->num_input_streams);
    }
  }
}

static void num_arc_streams_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_num_arc_streams* streams_data =
      (struct cras_observer_alert_data_num_arc_streams*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.num_arc_streams_changed) {
      client->ops.num_arc_streams_changed(client->context,
                                          streams_data->num_arc_streams);
    }
  }
}

static void hotword_triggered_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_hotword_triggered* triggered_data =
      (struct cras_observer_alert_data_hotword_triggered*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.hotword_triggered) {
      client->ops.hotword_triggered(client->context, triggered_data->tv_sec,
                                    triggered_data->tv_nsec);
    }
  }
}

static void non_empty_audio_state_changed_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_non_empty_audio_state* non_empty_audio_data =
      (struct cras_observer_non_empty_audio_state*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.non_empty_audio_state_changed) {
      client->ops.non_empty_audio_state_changed(
          client->context, non_empty_audio_data->non_empty);
    }
  }
}

static void bt_battery_changed_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_bt_battery_changed* triggered_data =
      (struct cras_observer_alert_data_bt_battery_changed*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.bt_battery_changed) {
      client->ops.bt_battery_changed(client->context, triggered_data->address,
                                     triggered_data->level);
    }
  }
}

static void severe_underrun_alert(void* arg, void* data) {
  struct cras_observer_client* client;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.severe_underrun) {
      client->ops.severe_underrun(client->context);
    }
  }
}

static void underrun_alert(void* arg, void* data) {
  struct cras_observer_client* client;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.underrun) {
      client->ops.underrun(client->context);
    }
  }
}

static void general_survey_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_general_survey* triggered_data =
      (struct cras_observer_alert_data_general_survey*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.general_survey) {
      client->ops.general_survey(client->context, triggered_data->stream_type,
                                 triggered_data->client_type,
                                 triggered_data->node_type_pair);
    }
  }
}

static void bluetooth_survey_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_bluetooth_survey* triggered_data =
      (struct cras_observer_alert_data_bluetooth_survey*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.bluetooth_survey) {
      client->ops.bluetooth_survey(client->context, triggered_data->bt_flags);
    }
  }
}

static void output_proc_survey_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_output_proc_survey* triggered_data =
      (struct cras_observer_alert_data_output_proc_survey*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.output_proc_survey) {
      client->ops.output_proc_survey(client->context,
                                     triggered_data->node_type);
    }
  }
}

static void speak_on_mute_detected_alert(void* arg, void* data) {
  struct cras_observer_client* client;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.speak_on_mute_detected) {
      client->ops.speak_on_mute_detected(client->context);
    }
  }
}

static void num_stream_ignore_ui_gains_changed_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_num_stream_ignore_ui_gains* num_stream_ignore_ui_gains =
      (struct cras_observer_num_stream_ignore_ui_gains*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.num_stream_ignore_ui_gains_changed) {
      client->ops.num_stream_ignore_ui_gains_changed(
          client->context, num_stream_ignore_ui_gains->num);
    }
  }
}

static void ewma_power_reported_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_ewma_power_reported* report =
      (struct cras_observer_alert_data_ewma_power_reported*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.ewma_power_reported) {
      client->ops.ewma_power_reported(client->context, report->power);
    }
  }
}

static void sidetone_supported_changed_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_sidetone_supported_changed* report =
      (struct cras_observer_alert_data_sidetone_supported_changed*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.sidetone_supported_changed) {
      client->ops.sidetone_supported_changed(client->context,
                                             report->supported);
    }
  }
}

static void audio_effect_ui_appearance_changed_alert(void* arg, void* data) {
  struct cras_observer_client* client;
  struct cras_observer_alert_data_audio_effect_ui_appearance_changed* report =
      (struct cras_observer_alert_data_audio_effect_ui_appearance_changed*)data;

  DL_FOREACH (g_observer->clients, client) {
    if (client->ops.audio_effect_ui_appearance_changed) {
      client->ops.audio_effect_ui_appearance_changed(
          client->context, report->audio_effects_ready);
    }
  }
}

static int cras_observer_server_set_alert(struct cras_alert** alert,
                                          cras_alert_cb cb,
                                          cras_alert_prepare prepare,
                                          unsigned int flags) {
  *alert = cras_alert_create(prepare, flags);
  if (!*alert) {
    return -ENOMEM;
  }
  return cras_alert_add_callback(*alert, cb, NULL);
}

#define CRAS_OBSERVER_SET_ALERT(alert, prepare, flags)                  \
  do {                                                                  \
    rc = cras_observer_server_set_alert(&g_observer->alerts.alert,      \
                                        alert##_alert, prepare, flags); \
    if (rc)                                                             \
      goto error;                                                       \
  } while (0)

#define CRAS_OBSERVER_SET_ALERT_WITH_DIRECTION(alert, direction)              \
  do {                                                                        \
    rc = cras_observer_server_set_alert(&g_observer->alerts.alert[direction], \
                                        alert##_alert, NULL, 0);              \
    if (rc)                                                                   \
      goto error;                                                             \
  } while (0)

/*
 * Public interface
 */

int cras_observer_server_init() {
  int rc;

  memset(&g_empty_ops, 0, sizeof(g_empty_ops));
  g_observer = (struct cras_observer_server*)calloc(1, sizeof(*g_observer));
  if (!g_observer) {
    return -ENOMEM;
  }

  CRAS_OBSERVER_SET_ALERT(output_volume, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(output_mute, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(capture_gain, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(capture_mute, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(nodes, nodes_prepare, 0);
  CRAS_OBSERVER_SET_ALERT(active_node, nodes_prepare,
                          CRAS_ALERT_FLAG_KEEP_ALL_DATA);
  CRAS_OBSERVER_SET_ALERT(output_node_volume, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(node_left_right_swapped, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(input_node_gain, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(suspend_changed, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(hotword_triggered, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(non_empty_audio_state_changed, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(bt_battery_changed, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(num_input_streams_with_permission, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(severe_underrun, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(underrun, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(general_survey, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(bluetooth_survey, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(output_proc_survey, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(speak_on_mute_detected, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(num_non_chrome_output_streams, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(num_stream_ignore_ui_gains_changed, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(num_arc_streams, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(ewma_power_reported, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(sidetone_supported_changed, NULL, 0);
  CRAS_OBSERVER_SET_ALERT(audio_effect_ui_appearance_changed, NULL, 0);

  CRAS_OBSERVER_SET_ALERT_WITH_DIRECTION(num_active_streams,
                                         CRAS_STREAM_OUTPUT);
  CRAS_OBSERVER_SET_ALERT_WITH_DIRECTION(num_active_streams, CRAS_STREAM_INPUT);
  CRAS_OBSERVER_SET_ALERT_WITH_DIRECTION(num_active_streams,
                                         CRAS_STREAM_POST_MIX_PRE_DSP);

  return 0;

error:
  cras_observer_server_free();
  return rc;
}

void cras_observer_server_free() {
  if (!g_observer) {
    return;
  }
  cras_alert_destroy(g_observer->alerts.output_volume);
  cras_alert_destroy(g_observer->alerts.output_mute);
  cras_alert_destroy(g_observer->alerts.capture_gain);
  cras_alert_destroy(g_observer->alerts.capture_mute);
  cras_alert_destroy(g_observer->alerts.nodes);
  cras_alert_destroy(g_observer->alerts.active_node);
  cras_alert_destroy(g_observer->alerts.output_node_volume);
  cras_alert_destroy(g_observer->alerts.node_left_right_swapped);
  cras_alert_destroy(g_observer->alerts.input_node_gain);
  cras_alert_destroy(g_observer->alerts.suspend_changed);
  cras_alert_destroy(g_observer->alerts.hotword_triggered);
  cras_alert_destroy(g_observer->alerts.non_empty_audio_state_changed);
  cras_alert_destroy(g_observer->alerts.bt_battery_changed);
  cras_alert_destroy(g_observer->alerts.severe_underrun);
  cras_alert_destroy(g_observer->alerts.underrun);
  cras_alert_destroy(g_observer->alerts.num_input_streams_with_permission);
  cras_alert_destroy(g_observer->alerts.num_active_streams[CRAS_STREAM_OUTPUT]);
  cras_alert_destroy(g_observer->alerts.num_active_streams[CRAS_STREAM_INPUT]);
  cras_alert_destroy(
      g_observer->alerts.num_active_streams[CRAS_STREAM_POST_MIX_PRE_DSP]);
  cras_alert_destroy(g_observer->alerts.num_non_chrome_output_streams);
  cras_alert_destroy(g_observer->alerts.general_survey);
  cras_alert_destroy(g_observer->alerts.bluetooth_survey);
  cras_alert_destroy(g_observer->alerts.output_proc_survey);
  cras_alert_destroy(g_observer->alerts.speak_on_mute_detected);
  cras_alert_destroy(g_observer->alerts.num_stream_ignore_ui_gains_changed);
  cras_alert_destroy(g_observer->alerts.num_arc_streams);
  cras_alert_destroy(g_observer->alerts.ewma_power_reported);
  cras_alert_destroy(g_observer->alerts.sidetone_supported_changed);
  cras_alert_destroy(g_observer->alerts.audio_effect_ui_appearance_changed);

  free(g_observer);
  g_observer = NULL;
}

int cras_observer_ops_are_empty(const struct cras_observer_ops* ops) {
  return memcmp(ops, &g_empty_ops, sizeof(*ops)) == 0;
}

void cras_observer_get_ops(const struct cras_observer_client* client,
                           struct cras_observer_ops* ops) {
  if (!ops) {
    return;
  }
  if (!client) {
    memset(ops, 0, sizeof(*ops));
  } else {
    memcpy(ops, &client->ops, sizeof(*ops));
  }
}

void cras_observer_set_ops(struct cras_observer_client* client,
                           const struct cras_observer_ops* ops) {
  if (!client) {
    return;
  }
  if (!ops) {
    memset(&client->ops, 0, sizeof(client->ops));
  } else {
    memcpy(&client->ops, ops, sizeof(client->ops));
  }
}

struct cras_observer_client* cras_observer_add(
    const struct cras_observer_ops* ops,
    void* context) {
  struct cras_observer_client* client;

  client = (struct cras_observer_client*)calloc(1, sizeof(*client));
  if (!client) {
    return NULL;
  }
  client->context = context;
  DL_APPEND(g_observer->clients, client);
  cras_observer_set_ops(client, ops);
  return client;
}

void cras_observer_remove(struct cras_observer_client* client) {
  if (!client) {
    return;
  }
  DL_DELETE(g_observer->clients, client);
  free(client);
}

/*
 * Public interface for notifiers.
 */

void cras_observer_notify_output_volume(int32_t volume) {
  struct cras_observer_alert_data_volume data;

  data.volume = volume;
  cras_alert_pending_data(g_observer->alerts.output_volume, &data,
                          sizeof(data));
}

void cras_observer_notify_output_mute(int muted,
                                      int user_muted,
                                      int mute_locked) {
  struct cras_observer_alert_data_mute data;

  data.muted = muted;
  data.user_muted = user_muted;
  data.mute_locked = mute_locked;
  cras_alert_pending_data(g_observer->alerts.output_mute, &data, sizeof(data));
}

void cras_observer_notify_capture_gain(int32_t gain) {
  struct cras_observer_alert_data_volume data;

  data.volume = gain;
  cras_alert_pending_data(g_observer->alerts.capture_gain, &data, sizeof(data));
}

void cras_observer_notify_capture_mute(int muted, int mute_locked) {
  struct cras_observer_alert_data_mute data;

  data.muted = muted;
  data.user_muted = 0;
  data.mute_locked = mute_locked;
  cras_alert_pending_data(g_observer->alerts.capture_mute, &data, sizeof(data));
}

void cras_observer_notify_nodes(void) {
  cras_alert_pending(g_observer->alerts.nodes);
}

void cras_observer_notify_active_node(enum CRAS_STREAM_DIRECTION dir,
                                      cras_node_id_t node_id) {
  struct cras_observer_alert_data_active_node data;

  data.direction = dir;
  data.node_id = node_id;
  cras_alert_pending_data(g_observer->alerts.active_node, &data, sizeof(data));
}

void cras_observer_notify_output_node_volume(cras_node_id_t node_id,
                                             int32_t volume) {
  struct cras_observer_alert_data_node_volume data;

  data.node_id = node_id;
  data.volume = volume;
  cras_alert_pending_data(g_observer->alerts.output_node_volume, &data,
                          sizeof(data));
}

void cras_observer_notify_node_left_right_swapped(cras_node_id_t node_id,
                                                  int swapped) {
  struct cras_observer_alert_data_node_lr_swapped data;

  data.node_id = node_id;
  data.swapped = swapped;
  cras_alert_pending_data(g_observer->alerts.node_left_right_swapped, &data,
                          sizeof(data));
}

void cras_observer_notify_input_node_gain(cras_node_id_t node_id,
                                          int32_t gain) {
  struct cras_observer_alert_data_node_volume data;

  data.node_id = node_id;
  data.volume = gain;
  cras_alert_pending_data(g_observer->alerts.input_node_gain, &data,
                          sizeof(data));
}

void cras_observer_notify_suspend_changed(int suspended) {
  struct cras_observer_alert_data_suspend data;

  data.suspended = suspended;
  cras_alert_pending_data(g_observer->alerts.suspend_changed, &data,
                          sizeof(data));
}

void cras_observer_notify_num_active_streams(enum CRAS_STREAM_DIRECTION dir,
                                             uint32_t num_active_streams) {
  struct cras_observer_alert_data_streams data;
  struct cras_alert* alert;

  data.direction = dir;
  data.num_active_streams = num_active_streams;
  alert = g_observer->alerts.num_active_streams[dir];
  if (!alert) {
    return;
  }

  cras_alert_pending_data(alert, &data, sizeof(data));
}

void cras_observer_notify_input_streams_with_permission(
    uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE]) {
  struct cras_observer_alert_data_input_streams data;
  struct cras_alert* alert;

  memcpy(&data.num_input_streams, num_input_streams,
         sizeof(*num_input_streams) * CRAS_NUM_CLIENT_TYPE);
  alert = g_observer->alerts.num_input_streams_with_permission;
  if (!alert) {
    return;
  }

  cras_alert_pending_data(alert, &data, sizeof(data));
}

void cras_observer_notify_hotword_triggered(int64_t tv_sec, int64_t tv_nsec) {
  struct cras_observer_alert_data_hotword_triggered data;

  data.tv_sec = tv_sec;
  data.tv_nsec = tv_nsec;
  cras_alert_pending_data(g_observer->alerts.hotword_triggered, &data,
                          sizeof(data));
}

void cras_observer_notify_non_empty_audio_state_changed(int non_empty) {
  struct cras_observer_non_empty_audio_state data;

  data.non_empty = non_empty;

  cras_alert_pending_data(g_observer->alerts.non_empty_audio_state_changed,
                          &data, sizeof(data));
}

void cras_observer_notify_bt_battery_changed(const char* address,
                                             uint32_t level) {
  struct cras_observer_alert_data_bt_battery_changed data;

  data.address = address;
  data.level = level;

  cras_alert_pending_data(g_observer->alerts.bt_battery_changed, &data,
                          sizeof(data));
}

void cras_observer_notify_severe_underrun() {
  cras_alert_pending(g_observer->alerts.severe_underrun);
}

void cras_observer_notify_underrun() {
  cras_alert_pending(g_observer->alerts.underrun);
}

void cras_observer_notify_general_survey(enum CRAS_STREAM_TYPE stream_type,
                                         enum CRAS_CLIENT_TYPE client_type,
                                         const char* node_type_pair) {
  struct cras_observer_alert_data_general_survey data;

  data.stream_type = stream_type;
  data.client_type = client_type;
  data.node_type_pair = node_type_pair;
  cras_alert_pending_data(g_observer->alerts.general_survey, &data,
                          sizeof(data));
}

void cras_observer_notify_bluetooth_survey(uint32_t bt_flags) {
  struct cras_observer_alert_data_bluetooth_survey data;

  data.bt_flags = bt_flags;

  cras_alert_pending_data(g_observer->alerts.bluetooth_survey, &data,
                          sizeof(data));
}

void cras_observer_notify_output_proc_survey(enum CRAS_NODE_TYPE node_type) {
  struct cras_observer_alert_data_output_proc_survey data;

  data.node_type = node_type;

  cras_alert_pending_data(g_observer->alerts.output_proc_survey, &data,
                          sizeof(data));
}

void cras_observer_notify_speak_on_mute_detected() {
  cras_alert_pending(g_observer->alerts.speak_on_mute_detected);
}

void cras_observer_notify_num_non_chrome_output_streams(
    uint32_t num_non_chrome_output_streams) {
  struct cras_observer_alert_data_num_non_chrome_output_streams data = {
      .num_non_chrome_output_streams = num_non_chrome_output_streams};
  cras_alert_pending_data(g_observer->alerts.num_non_chrome_output_streams,
                          &data, sizeof(data));
}

void cras_observer_notify_num_stream_ignore_ui_gains_changed(int num) {
  struct cras_observer_num_stream_ignore_ui_gains data;

  data.num = num;

  cras_alert_pending_data(g_observer->alerts.num_stream_ignore_ui_gains_changed,
                          &data, sizeof(data));
}

void cras_observer_notify_num_arc_streams(uint32_t num_arc_streams) {
  struct cras_observer_alert_data_num_arc_streams data = {.num_arc_streams =
                                                              num_arc_streams};
  cras_alert_pending_data(g_observer->alerts.num_arc_streams, &data,
                          sizeof(data));
}

void cras_observer_notify_ewma_power_reported(double power) {
  struct cras_observer_alert_data_ewma_power_reported data = {.power = power};

  cras_alert_pending_data(g_observer->alerts.ewma_power_reported, &data,
                          sizeof(data));
}

void cras_observer_notify_sidetone_supported_changed(bool supported) {
  struct cras_observer_alert_data_sidetone_supported_changed data = {
      .supported = supported};
  cras_alert_pending_data(g_observer->alerts.sidetone_supported_changed, &data,
                          sizeof(data));
}

void cras_observer_notify_audio_effect_ui_appearance_changed(
    bool audio_effects_ready) {
  struct cras_observer_alert_data_audio_effect_ui_appearance_changed data = {
      .audio_effects_ready = audio_effects_ready};
  cras_alert_pending_data(g_observer->alerts.audio_effect_ui_appearance_changed,
                          &data, sizeof(data));
}
