/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_alsa_io_ops.h"

#include <alsa/asoundlib.h>
#include <stdint.h>

#include "cras/src/server/config/cras_card_config.h"
#include "cras/src/server/cras_alsa_mixer.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_iodev.h"
#include "cras_types.h"

inline struct cras_iodev* cras_alsa_iodev_ops_create(
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
    char* usb_serial_number) {
  assert(ops->create);
  return ops->create(card_index, card_name, device_index, pcm_name, dev_name,
                     dev_id, card_type, is_first, mixer, config, ucm, hctl,
                     direction, usb_vid, usb_pid, usb_serial_number);
}

inline int cras_alsa_iodev_ops_legacy_complete_init(
    struct cras_alsa_iodev_ops* ops,
    struct cras_iodev* iodev) {
  assert(ops->legacy_complete_init);
  return ops->legacy_complete_init(iodev);
}

inline int cras_alsa_iodev_ops_ucm_add_nodes_and_jacks(
    struct cras_alsa_iodev_ops* ops,
    struct cras_iodev* iodev,
    struct ucm_section* section) {
  assert(ops->ucm_add_nodes_and_jacks);
  return ops->ucm_add_nodes_and_jacks(iodev, section);
}
inline void cras_alsa_iodev_ops_ucm_complete_init(
    struct cras_alsa_iodev_ops* ops,
    struct cras_iodev* iodev) {
  assert(ops->ucm_complete_init);
  ops->ucm_complete_init(iodev);
}
inline void cras_alsa_iodev_ops_destroy(struct cras_alsa_iodev_ops* ops,
                                        struct cras_iodev* iodev) {
  assert(ops->destroy);
  ops->destroy(iodev);
}

inline unsigned cras_alsa_iodev_ops_index(struct cras_alsa_iodev_ops* ops,
                                          struct cras_iodev* iodev) {
  assert(ops->index);
  return ops->index(iodev);
}

inline int cras_alsa_iodev_ops_has_hctl_jacks(struct cras_alsa_iodev_ops* ops,
                                              struct cras_iodev* iodev) {
  assert(ops->has_hctl_jacks);
  return ops->has_hctl_jacks(iodev);
}
