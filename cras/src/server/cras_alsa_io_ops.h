/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_ALSA_IO_OPS_H_
#define CRAS_SRC_SERVER_CRAS_ALSA_IO_OPS_H_

#include <alsa/asoundlib.h>

#include "cras/src/server/config/cras_card_config.h"
#include "cras/src/server/cras_alsa_mixer.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_iodev.h"
#include "cras_types.h"

struct cras_alsa_iodev_ops {
  struct cras_iodev* (*create)(size_t card_index,
                               const char* card_name,
                               size_t device_index,
                               const char* pcm_name,
                               const char* dev_name,
                               const char* dev_id,
                               enum CRAS_ALSA_CARD_TYPE card_type,
                               int is_first,
                               struct cras_alsa_mixer* mixer,
                               const struct cras_card_config* config,
                               struct cras_use_case_mgr* ucm,
                               snd_hctl_t* hctl,
                               enum CRAS_STREAM_DIRECTION direction,
                               size_t usb_vid,
                               size_t usb_pid,
                               char* usb_serial_number);

  int (*legacy_complete_init)(struct cras_iodev* iodev);
  int (*ucm_add_nodes_and_jacks)(struct cras_iodev* iodev,
                                 struct ucm_section* section);
  void (*ucm_complete_init)(struct cras_iodev* iodev);
  void (*destroy)(struct cras_iodev* iodev);
  unsigned (*index)(struct cras_iodev* iodev);
  int (*has_hctl_jacks)(struct cras_iodev* iodev);
};

struct cras_iodev* cras_alsa_iodev_ops_create(
    struct cras_alsa_iodev_ops* ops,
    size_t card_index,
    const char* card_name,
    size_t device_index,
    const char* pcm_name,
    const char* dev_name,
    const char* dev_id,
    enum CRAS_ALSA_CARD_TYPE card_type,
    int is_first,
    struct cras_alsa_mixer* mixer,
    const struct cras_card_config* config,
    struct cras_use_case_mgr* ucm,
    snd_hctl_t* hctl,
    enum CRAS_STREAM_DIRECTION direction,
    size_t usb_vid,
    size_t usb_pid,
    char* usb_serial_number);

int cras_alsa_iodev_ops_legacy_complete_init(struct cras_alsa_iodev_ops* ops,
                                             struct cras_iodev* iodev);
int cras_alsa_iodev_ops_ucm_add_nodes_and_jacks(struct cras_alsa_iodev_ops* ops,
                                                struct cras_iodev* iodev,
                                                struct ucm_section* section);
void cras_alsa_iodev_ops_ucm_complete_init(struct cras_alsa_iodev_ops* ops,
                                           struct cras_iodev* iodev);
void cras_alsa_iodev_ops_destroy(struct cras_alsa_iodev_ops* ops,
                                 struct cras_iodev* iodev);
unsigned cras_alsa_iodev_ops_index(struct cras_alsa_iodev_ops* ops,
                                   struct cras_iodev* iodev);
int cras_alsa_iodev_ops_has_hctl_jacks(struct cras_alsa_iodev_ops* ops,
                                       struct cras_iodev* iodev);

#endif  // CRAS_SRC_SERVER_CRAS_ALSA_IO_OPS_H_
