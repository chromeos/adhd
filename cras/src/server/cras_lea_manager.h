/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_LEA_MANAGER_H_
#define CRAS_SRC_SERVER_CRAS_LEA_MANAGER_H_

#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_iodev.h"
#include "cras_audio_format.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cras_lea;
struct fl_media;

enum LEA_AUDIO_CONTEXT_TYPE {
  LEA_AUDIO_CONTEXT_UNINITIALIZED = 0x0000,
  LEA_AUDIO_CONTEXT_UNSPECIFIED = 0x0001,
  LEA_AUDIO_CONTEXT_CONVERSATIONAL = 0x0002,
  LEA_AUDIO_CONTEXT_MEDIA = 0x0004,
  LEA_AUDIO_CONTEXT_GAME = 0x0008,
  LEA_AUDIO_CONTEXT_INSTRUCTIONAL = 0x0010,
  LEA_AUDIO_CONTEXT_VOICEASSISTANTS = 0x0020,
  LEA_AUDIO_CONTEXT_LIVE = 0x0040,
  LEA_AUDIO_CONTEXT_SOUNDEFFECTS = 0x0080,
  LEA_AUDIO_CONTEXT_NOTIFICATIONS = 0x0100,
  LEA_AUDIO_CONTEXT_RINGTONE = 0x0200,
  LEA_AUDIO_CONTEXT_ALERTS = 0x0400,
  LEA_AUDIO_CONTEXT_EMERGENCYALARM = 0x0800,
  LEA_AUDIO_CONTEXT_RFU = 0x1000,
};

enum LEA_AUDIO_DIRECTION {
  LEA_AUDIO_DIRECTION_NONE = 0,
  LEA_AUDIO_DIRECTION_OUTPUT = (1 << 0),
  LEA_AUDIO_DIRECTION_INPUT = (1 << 1),
};

enum LEA_GROUP_STATUS {
  LEA_GROUP_INACTIVE,
  LEA_GROUP_ACTIVE,
  LEA_GROUP_TURNED_IDLE_DURING_CALL,
};

enum LEA_GROUP_NODE_STATUS {
  LEA_GROUP_NODE_ADDED = 1,
  LEA_GROUP_NODE_REMOVED,
};

bool cras_floss_lea_is_active(struct cras_lea* lea);

bool cras_floss_lea_has_connected_group(struct cras_lea* lea);

bool cras_floss_lea_is_context_switching(struct cras_lea* lea);

void cras_floss_lea_set_is_context_switching(struct cras_lea* lea,
                                             bool is_context_switching);

void cras_floss_lea_set_target_context(struct cras_lea* lea,
                                       enum LEA_AUDIO_CONTEXT_TYPE context);

void cras_floss_lea_apply_target_context(struct cras_lea* lea);

bool cras_floss_lea_is_odev_started(struct cras_lea* lea);

bool cras_floss_lea_is_idev_started(struct cras_lea* lea);

struct cras_iodev* cras_floss_lea_get_primary_idev(struct cras_lea* lea);

struct cras_iodev* cras_floss_lea_get_primary_odev(struct cras_lea* lea);

struct cras_lea* cras_floss_lea_create(struct fl_media* fm);

void cras_floss_lea_destroy(struct cras_lea* lea);

int cras_floss_lea_start(struct cras_lea* lea,
                         thread_callback cb,
                         enum CRAS_STREAM_DIRECTION dir);

int cras_floss_lea_stop(struct cras_lea* lea, enum CRAS_STREAM_DIRECTION dir);

int cras_floss_lea_get_fd(struct cras_lea* lea);

void cras_floss_lea_set_active(struct cras_lea* lea,
                               int group_id,
                               unsigned enabled);

void cras_floss_lea_set_volume(struct cras_lea* lea, unsigned int volume);

int cras_floss_lea_fill_format(struct cras_lea* lea,
                               size_t** rates,
                               snd_pcm_format_t** formats,
                               size_t** channel_counts);

void cras_floss_lea_add_group(struct cras_lea* lea,
                              const char* name,
                              int group_id);

void cras_floss_lea_remove_group(struct cras_lea* lea, int group_id);

int cras_floss_lea_audio_conf_updated(struct cras_lea* lea,
                                      uint8_t direction,
                                      int group_id,
                                      uint32_t snk_audio_location,
                                      uint32_t src_audio_location,
                                      uint16_t available_contexts);

int cras_floss_lea_update_group_volume(struct cras_lea* lea,
                                       int group_id,
                                       uint8_t volume);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_LEA_MANAGER_H_
