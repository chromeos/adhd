/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // For asprintf
#endif

#include "cras/src/server/cras_alsa_card.h"

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <syslog.h>

#include "cras/common/check.h"
#include "cras/server/platform/features/features.h"
#include "cras/src/common/cras_alsa_card_info.h"
#include "cras/src/server/config/cras_card_config.h"
#include "cras/src/server/cras_alsa_common_io.h"
#include "cras/src/server/cras_alsa_config.h"
#include "cras/src/server/cras_alsa_io.h"
#include "cras/src/server/cras_alsa_io_ops.h"
#include "cras/src/server/cras_alsa_mixer.h"
#include "cras/src/server/cras_alsa_mixer_name.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_alsa_ucm_section.h"
#include "cras/src/server/cras_alsa_usb_io.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_system_state.h"
#include "cras_iodev_info.h"
#include "cras_types.h"
#include "cras_util.h"
#include "third_party/utlist/utlist.h"

#define MAX_ALSA_CARDS 32           // Alsa limit on number of cards.
#define MAX_ALSA_PCM_NAME_LENGTH 9  // Alsa pcm name "hw:XX,YY" + 1 for null.
#define MAX_COUPLED_OUTPUT_SIZE 4

struct iodev_list_node {
  struct cras_iodev* iodev;
  enum CRAS_STREAM_DIRECTION direction;
  struct iodev_list_node *prev, *next;
};

/* Keeps an fd that is registered with system state.  A list of fds must be
 * kept so that they can be removed when the card is destroyed. */
struct hctl_poll_fd {
  int fd;
  struct hctl_poll_fd *prev, *next;
};

// Holds information about each sound card on the system.
struct cras_alsa_card {
  // of the object containing the char array in form hw:XX.
  alsa_card_name_t name;
  // 0 based index, value of "XX" in the name.
  size_t card_index;
  // Input and output devices for this card.
  struct iodev_list_node* iodevs;
  // Controls the mixer controls for this card.
  struct cras_alsa_mixer* mixer;
  // CRAS use case manager if available.
  struct cras_use_case_mgr* ucm;
  // ALSA high-level control interface.
  snd_hctl_t* hctl;
  // List of fds registered with cras_system_state.
  struct hctl_poll_fd* hctl_poll_fds;
  // Config info for this card, can be NULL if none found.
  struct cras_card_config* config;
  enum CRAS_ALSA_CARD_TYPE card_type;
  struct cras_alsa_iodev_ops* ops;
};

static struct cras_alsa_iodev_ops cras_alsa_iodev_ops_internal_ops = {
    .create = alsa_iodev_create,
    .legacy_complete_init = alsa_iodev_legacy_complete_init,
    .ucm_add_nodes_and_jacks = alsa_iodev_ucm_add_nodes_and_jacks,
    .ucm_complete_init = alsa_iodev_ucm_complete_init,
    .destroy = alsa_iodev_destroy,
    .index = alsa_iodev_index,
    .has_hctl_jacks = alsa_iodev_has_hctl_jacks};

static struct cras_alsa_iodev_ops cras_alsa_iodev_ops_usb_ops = {
    .create = cras_alsa_usb_iodev_create,
    .legacy_complete_init = cras_alsa_usb_iodev_legacy_complete_init,
    .ucm_add_nodes_and_jacks = cras_alsa_usb_iodev_ucm_add_nodes_and_jacks,
    .ucm_complete_init = cras_alsa_usb_iodev_ucm_complete_init,
    .destroy = cras_alsa_usb_iodev_destroy,
    .index = cras_alsa_usb_iodev_index,
    .has_hctl_jacks = cras_alsa_usb_iodev_has_hctl_jacks};

/* Creates an iodev for the given device.
 * Args:
 *    alsa_card - the alsa_card the device will be added to.
 *    info - Information about the card type and priority.
 *    card_name - The name of the card.
 *    dev_name - The name of the device.
 *    dev_id - The id string of the device.
 *    device_index - 0 based index, value of "YY" in "hw:XX,YY".
 *    direction - Input or output.
 *    use_case - The intended use case of the device. e.g. HiFi, Low Latency.
 *    group_ref - An existing iodev of the same iodev group for reference.
 * Returns:
 *    Pointer to the created iodev, or NULL on error.
 *    other negative error code otherwise.
 */
struct cras_iodev* create_iodev_for_device(struct cras_alsa_card* alsa_card,
                                           struct cras_alsa_card_info* info,
                                           const char* card_name,
                                           const char* dev_name,
                                           const char* dev_id,
                                           unsigned device_index,
                                           enum CRAS_STREAM_DIRECTION direction,
                                           enum CRAS_USE_CASE use_case,
                                           struct cras_iodev* group_ref) {
  struct iodev_list_node* new_dev;
  struct iodev_list_node* node;
  int first = 1;
  char pcm_name[MAX_ALSA_PCM_NAME_LENGTH];

  /* Find whether this is the first device in this direction, and
   * avoid duplicate device indexes. */
  DL_FOREACH (alsa_card->iodevs, node) {
    if (node->direction != direction) {
      continue;
    }
    first = 0;
    if (cras_alsa_iodev_ops_index(alsa_card->ops, node->iodev) ==
        device_index) {
      syslog(LOG_DEBUG, "Skipping duplicate device for %s:%s:%s [%u]",
             card_name, dev_name, dev_id, device_index);
      return node->iodev;
    }
  }

  new_dev = calloc(1, sizeof(*new_dev));
  if (new_dev == NULL) {
    return NULL;
  }

  /* Append device index to card namem, ex: 'hw:0', for the PCM name of
   * target iodev. */
  snprintf(pcm_name, MAX_ALSA_PCM_NAME_LENGTH, "%s,%u", alsa_card->name.str,
           device_index);

  new_dev->direction = direction;
  new_dev->iodev = cras_alsa_iodev_ops_create(
      alsa_card->ops, info, card_name, device_index, pcm_name, dev_name, dev_id,
      first, alsa_card->mixer, alsa_card->config, alsa_card->ucm,
      alsa_card->hctl, direction, use_case, group_ref);
  if (new_dev->iodev == NULL) {
    syslog(LOG_ERR, "Couldn't create alsa_iodev for %s", pcm_name);
    free(new_dev);
    return NULL;
  }

  syslog(LOG_DEBUG, "New %s device %s for %s",
         direction == CRAS_STREAM_OUTPUT ? "playback" : "capture", pcm_name,
         cras_use_case_str(use_case));

  DL_APPEND(alsa_card->iodevs, new_dev);
  return new_dev->iodev;
}

/* Returns non-zero if this card has hctl jacks.
 */
static int card_has_hctl_jack(struct cras_alsa_card* alsa_card) {
  struct iodev_list_node* node;

  // Find the first device that has an hctl jack.
  DL_FOREACH (alsa_card->iodevs, node) {
    if (cras_alsa_iodev_ops_has_hctl_jacks(alsa_card->ops, node->iodev)) {
      return 1;
    }
  }
  return 0;
}

/* Filters an array of mixer control names. Keep a name if it is
 * specified in the ucm config. */
static struct mixer_name* filter_controls(struct cras_use_case_mgr* ucm,
                                          struct mixer_name* controls) {
  struct mixer_name* control;
  DL_FOREACH (controls, control) {
    char* dev = ucm_get_dev_for_mixer(ucm, control->name, CRAS_STREAM_OUTPUT);
    if (!dev) {
      DL_DELETE(controls, control);
    } else {
      free(dev);
    }
  }
  return controls;
}

/* Handles notifications from alsa controls.  Called by main thread when a poll
 * fd provided by alsa signals there is an event available. */
static void alsa_control_event_pending(void* arg, int revent) {
  struct cras_alsa_card* card;

  card = (struct cras_alsa_card*)arg;
  if (card == NULL) {
    syslog(LOG_WARNING, "Invalid card from control event.");
    return;
  }

  /* handle_events will trigger the callback registered with each control
   * that has changed. */
  snd_hctl_handle_events(card->hctl);
}

static int add_controls_and_iodevs_by_matching(struct cras_alsa_card_info* info,
                                               struct cras_alsa_card* alsa_card,
                                               const char* card_name,
                                               snd_ctl_t* handle) {
  struct mixer_name* coupled_controls = NULL;
  int dev_idx;
  snd_pcm_info_t* dev_info;
  struct mixer_name* extra_controls = NULL;
  int rc = 0;

  snd_pcm_info_alloca(&dev_info);

  if (alsa_card->ucm) {
    char* extra_main_volume;

    // Filter the extra output mixer names
    extra_controls = filter_controls(
        alsa_card->ucm, mixer_name_add(extra_controls, "IEC958",
                                       CRAS_STREAM_OUTPUT, MIXER_NAME_VOLUME));

    // Get the extra main volume control.
    extra_main_volume = ucm_get_flag(alsa_card->ucm, "ExtraMainVolume");
    if (extra_main_volume) {
      extra_controls =
          mixer_name_add(extra_controls, extra_main_volume, CRAS_STREAM_OUTPUT,
                         MIXER_NAME_MAIN_VOLUME);
      free(extra_main_volume);
    }
    mixer_name_dump(extra_controls, "extra controls");

    // Check if coupled controls has been specified for speaker.
    coupled_controls = ucm_get_coupled_mixer_names(alsa_card->ucm, "Speaker");
    mixer_name_dump(coupled_controls, "coupled controls");
  }

  // Add controls to mixer by name matching.
  if (info->card_type == ALSA_CARD_TYPE_USB) {
    rc = cras_alsa_mixer_add_controls_by_name_matching_usb(alsa_card->mixer);
  } else {
    rc = cras_alsa_mixer_add_controls_by_name_matching_internal(
        alsa_card->mixer, extra_controls, coupled_controls);
  }

  if (rc) {
    syslog(LOG_ERR, "Fail adding controls to mixer for %s.",
           alsa_card->name.str);
    goto error;
  }

  // Go through every device.
  dev_idx = -1;
  while (1) {
    rc = snd_ctl_pcm_next_device(handle, &dev_idx);
    if (rc < 0) {
      goto error;
    }
    if (dev_idx < 0) {
      break;
    }

    snd_pcm_info_set_device(dev_info, dev_idx);
    snd_pcm_info_set_subdevice(dev_info, 0);

    // Check for playback devices.
    snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_PLAYBACK);
    if (snd_ctl_pcm_info(handle, dev_info) == 0) {
      struct cras_iodev* iodev = create_iodev_for_device(
          alsa_card, info, card_name, snd_pcm_info_get_name(dev_info),
          snd_pcm_info_get_id(dev_info), dev_idx, CRAS_STREAM_OUTPUT,
          CRAS_USE_CASE_HIFI, NULL);
      if (iodev) {
        rc = cras_alsa_iodev_ops_legacy_complete_init(alsa_card->ops, iodev);
        if (rc < 0) {
          goto error;
        }
      }
    }

    // Check for capture devices.
    snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_CAPTURE);
    if (snd_ctl_pcm_info(handle, dev_info) == 0) {
      struct cras_iodev* iodev = create_iodev_for_device(
          alsa_card, info, card_name, snd_pcm_info_get_name(dev_info),
          snd_pcm_info_get_id(dev_info), dev_idx, CRAS_STREAM_INPUT,
          CRAS_USE_CASE_HIFI, NULL);
      if (iodev) {
        rc = cras_alsa_iodev_ops_legacy_complete_init(alsa_card->ops, iodev);
        if (rc < 0) {
          goto error;
        }
      }
    }
  }
error:
  mixer_name_free(coupled_controls);
  mixer_name_free(extra_controls);
  return rc;
}

static struct cras_iodev* find_first_iodev_with_ucm_section_name(
    struct cras_alsa_card* alsa_card,
    const char* ucm_section_name) {
  struct iodev_list_node* dev;
  struct cras_ionode* node;

  if (!ucm_section_name) {
    return NULL;
  }

  DL_FOREACH (alsa_card->iodevs, dev) {
    DL_FOREACH (dev->iodev->nodes, node) {
      struct alsa_common_node* anode = (struct alsa_common_node*)node;
      if (!strncmp(anode->ucm_name, ucm_section_name,
                   sizeof(anode->ucm_name))) {
        return dev->iodev;
      }
    }
  }
  return NULL;
}

static int set_stream_direction_filter(snd_pcm_info_t* dev_info,
                                       enum CRAS_STREAM_DIRECTION dir) {
  switch (dir) {
    case CRAS_STREAM_OUTPUT:
      snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_PLAYBACK);
      break;
    case CRAS_STREAM_INPUT:
      snd_pcm_info_set_stream(dev_info, SND_PCM_STREAM_CAPTURE);
      break;
    default:
      syslog(LOG_ERR, "Unexpected direction: %d", dir);
      return -EINVAL;
  }
  return 0;
}

static int create_iodevs_from_ucm_sections(
    struct cras_alsa_card_info* info,
    struct cras_alsa_card* alsa_card,
    const char* card_name,
    snd_ctl_t* handle,
    struct ucm_section* ucm_sections,
    enum CRAS_IODEV_VISIBILITY visibility,
    enum CRAS_USE_CASE use_case) {
  struct ucm_section* section;
  snd_pcm_info_t* dev_info;
  struct cras_iodev *iodev, *group_ref;
  int rc;

  snd_pcm_info_alloca(&dev_info);

  // Create all of the devices.
  DL_FOREACH (ucm_sections, section) {
    syslog(LOG_DEBUG, "Create iodev for ucm section: %s", section->name);
    /* If a UCM section specifies certain device as dependency
     * then don't create an alsa iodev for it, just append it
     * as node later. */
    if (section->dependent_dev_idx != -1) {
      continue;
    }
    snd_pcm_info_set_device(dev_info, section->dev_idx);
    snd_pcm_info_set_subdevice(dev_info, 0);
    rc = set_stream_direction_filter(dev_info, section->dir);
    if (rc) {
      return rc;
    }

    if (snd_ctl_pcm_info(handle, dev_info)) {
      syslog(LOG_WARNING, "Could not get info for device: %s", section->name);
      continue;
    }

    /* iodevs created from SectionDevices with the same name across UCM .conf
     * files are grouped together. */
    group_ref =
        find_first_iodev_with_ucm_section_name(alsa_card, section->name);
    iodev = create_iodev_for_device(
        alsa_card, info, card_name, snd_pcm_info_get_name(dev_info),
        snd_pcm_info_get_id(dev_info), section->dev_idx, section->dir, use_case,
        group_ref);
    if (iodev) {
      iodev->info.visibility = visibility;
    }
  }

  return 0;
}

static int create_iodevs_for_specialized_use_cases(
    struct cras_alsa_card_info* info,
    struct cras_alsa_card* alsa_card,
    const char* card_name,
    snd_ctl_t* handle) {
  struct ucm_section* ucm_sections;
  enum CRAS_USE_CASE use_case;
  cras_use_cases_t avail_use_cases;
  int rc;

  avail_use_cases = ucm_get_avail_use_cases(alsa_card->ucm);
  for (use_case = 0; use_case < CRAS_NUM_USE_CASES; use_case++) {
    // Skip if the use case is HIFI or not available.
    if ((avail_use_cases & (1 << use_case)) == 0) {
      continue;
    }
    if (use_case == CRAS_USE_CASE_HIFI) {
      continue;
    }

    syslog(LOG_DEBUG, "Scan iodevs for specialized use case: %s",
           cras_use_case_str(use_case));

    rc = ucm_set_use_case(alsa_card->ucm, use_case);
    if (rc) {
      return rc;
    }
    ucm_sections = ucm_get_sections(alsa_card->ucm);
    if (!ucm_sections) {
      syslog(LOG_ERR,
             "Could not retrieve any UCM SectionDevice for %s, card '%s'.",
             cras_use_case_str(use_case), card_name);
      return -ENOENT;
    }

    // iodevs for specialized use cases are hidden from user.
    rc = create_iodevs_from_ucm_sections(info, alsa_card, card_name, handle,
                                         ucm_sections, CRAS_IODEV_HIDDEN,
                                         use_case);
    ucm_section_free_list(ucm_sections);
    if (rc) {
      return rc;
    }
  }
  return 0;
}

static int add_controls_and_iodevs_with_ucm(struct cras_alsa_card_info* info,
                                            struct cras_alsa_card* alsa_card,
                                            const char* card_name,
                                            snd_ctl_t* handle) {
  struct mixer_name* main_volume_control_names;
  struct iodev_list_node* node;
  int rc = 0;
  struct ucm_section* section;
  struct ucm_section* ucm_sections;

  main_volume_control_names = ucm_get_main_volume_names(alsa_card->ucm);
  if (main_volume_control_names) {
    rc = cras_alsa_mixer_add_main_volume_control_by_name(
        alsa_card->mixer, main_volume_control_names);
    if (rc) {
      syslog(LOG_ERR,
             "Failed adding main volume controls to"
             " mixer for '%s'.'",
             card_name);
      goto cleanup_names;
    }
  }

  // Get info on the devices specified in the UCM config.
  ucm_sections = ucm_get_sections(alsa_card->ucm);
  if (!ucm_sections) {
    syslog(LOG_ERR,
           "Could not retrieve any UCM SectionDevice for CRAS_USE_CASE_HIFI, "
           "card '%s'.",
           card_name);
    rc = -ENOENT;
    goto cleanup_names;
  }

  // Create all of the controls first.
  DL_FOREACH (ucm_sections, section) {
    rc = cras_alsa_mixer_add_controls_in_section(alsa_card->mixer, section);
    if (rc) {
      syslog(LOG_ERR,
             "Failed adding controls to"
             " mixer for '%s:%s'",
             card_name, section->name);
      goto cleanup;
    }
  }

  // Create iodevs for the main (HiFi) use case.
  rc = create_iodevs_from_ucm_sections(info, alsa_card, card_name, handle,
                                       ucm_sections, CRAS_IODEV_VISIBLE,
                                       CRAS_USE_CASE_HIFI);
  if (rc) {
    goto cleanup;
  }

  /* Setup jacks and controls for the devices. If a SectionDevice is
   * dependent on another SectionDevice, it'll be added as a node to
   * a existing ALSA iodev. */
  DL_FOREACH (ucm_sections, section) {
    DL_FOREACH (alsa_card->iodevs, node) {
      if (node->direction != section->dir) {
        continue;
      }
      if (cras_alsa_iodev_ops_index(alsa_card->ops, node->iodev) ==
          section->dev_idx) {
        break;
      }
      if (cras_alsa_iodev_ops_index(alsa_card->ops, node->iodev) ==
          section->dependent_dev_idx) {
        break;
      }
    }
    if (node) {
      rc = cras_alsa_iodev_ops_ucm_add_nodes_and_jacks(alsa_card->ops,
                                                       node->iodev, section);
      if (rc < 0) {
        goto cleanup;
      }
    }
  }

  rc = create_iodevs_for_specialized_use_cases(info, alsa_card, card_name,
                                               handle);
  if (rc) {
    goto cleanup;
  }
  // Reset to HiFi since all UCM device sequences for ionodes are in HiFi.conf.
  rc = ucm_set_use_case(alsa_card->ucm, CRAS_USE_CASE_HIFI);
  if (rc) {
    goto cleanup;
  }

  DL_FOREACH (alsa_card->iodevs, node) {
    cras_alsa_iodev_ops_ucm_complete_init(alsa_card->ops, node->iodev);
  }

cleanup:
  ucm_section_free_list(ucm_sections);
cleanup_names:
  mixer_name_free(main_volume_control_names);
  return rc;
}

static void configure_echo_reference_dev(struct cras_alsa_card* alsa_card) {
  struct iodev_list_node *dev_node, *echo_ref_node;
  const char* echo_ref_name;

  if (!alsa_card->ucm) {
    return;
  }

  DL_FOREACH (alsa_card->iodevs, dev_node) {
    if (!dev_node->iodev->nodes) {
      continue;
    }

    struct alsa_common_node* anode =
        (struct alsa_common_node*)dev_node->iodev->nodes;
    echo_ref_name = ucm_get_echo_reference_dev_name_for_dev(alsa_card->ucm,
                                                            anode->ucm_name);
    if (!echo_ref_name) {
      continue;
    }
    DL_FOREACH (alsa_card->iodevs, echo_ref_node) {
      if (echo_ref_node->iodev->nodes == NULL) {
        continue;
      }
      if (!strcmp(echo_ref_name, echo_ref_node->iodev->nodes->name)) {
        break;
      }
    }
    if (echo_ref_node) {
      dev_node->iodev->echo_reference_dev = echo_ref_node->iodev;
    } else {
      syslog(LOG_ERR, "Echo ref dev %s doesn't exist on card %s", echo_ref_name,
             alsa_card->name.str);
    }
    free((void*)echo_ref_name);
  }
}

/*
 * Exported Interface.
 */

struct cras_alsa_card* cras_alsa_card_create(struct cras_alsa_card_info* info,
                                             const char* device_config_dir,
                                             const char* ucm_suffix) {
  snd_ctl_t* handle = NULL;
  int rc, n;
  snd_ctl_card_info_t* card_info;
  const char* card_name;
  struct cras_alsa_card* alsa_card;

  if (info->card_index >= MAX_ALSA_CARDS) {
    syslog(LOG_ERR, "Invalid alsa card index %u", info->card_index);
    return NULL;
  }

  snd_ctl_card_info_alloca(&card_info);

  alsa_card = calloc(1, sizeof(*alsa_card));
  if (alsa_card == NULL) {
    return NULL;
  }
  alsa_card->card_index = info->card_index;
  alsa_card->card_type = info->card_type;

  alsa_card->name = cras_alsa_card_get_name(info->card_index);
  alsa_card->ops = &cras_alsa_iodev_ops_internal_ops;
  if (alsa_card->card_type == ALSA_CARD_TYPE_USB) {
    alsa_card->ops = &cras_alsa_iodev_ops_usb_ops;
  }

  rc = snd_ctl_open(&handle, alsa_card->name.str, 0);
  if (rc < 0) {
    syslog(LOG_ERR, "Fail opening control %s.", alsa_card->name.str);
    goto error_bail;
  }

  rc = snd_ctl_card_info(handle, card_info);
  if (rc < 0) {
    syslog(LOG_WARNING, "Error getting card info.");
    goto error_bail;
  }

  card_name = snd_ctl_card_info_get_name(card_info);
  if (card_name == NULL) {
    syslog(LOG_WARNING, "Error getting card name.");
    goto error_bail;
  }

  if (info->card_type == ALSA_CARD_TYPE_USB ||
      cras_system_check_ignore_ucm_suffix(card_name)) {
    ucm_suffix = NULL;
  }

  // Read config file for this card if it exists.
  alsa_card->config = cras_card_config_create(device_config_dir, card_name);
  if (alsa_card->config == NULL) {
    syslog(LOG_DEBUG, "No config file for %s", alsa_card->name.str);
  }

  // Create a use case manager if a configuration is available.
  if (ucm_suffix) {
    char* ucm_name;
    if (asprintf(&ucm_name, "%s.%s", card_name, ucm_suffix) == -1) {
      syslog(LOG_ERR, "Error creating ucm name");
      goto error_bail;
    }
    alsa_card->ucm = ucm_create(ucm_name);
    syslog(LOG_DEBUG, "Card %s (%s) has UCM: %s", alsa_card->name.str, ucm_name,
           alsa_card->ucm ? "yes" : "no");
    free(ucm_name);
  } else {
    if (alsa_card->card_type == ALSA_CARD_TYPE_USB) {
      if (ucm_conf_exists(card_name)) {
        alsa_card->ucm = ucm_create(card_name);
      } else {
        alsa_card->ucm = NULL;
      }
    } else {
      alsa_card->ucm = ucm_create(card_name);
    }
    syslog(LOG_DEBUG, "Card %s (%s), Type %s, has UCM: %s", alsa_card->name.str,
           card_name, cras_card_type_to_string(alsa_card->card_type),
           alsa_card->ucm ? "yes" : "no");
  }

  if (info->card_type != ALSA_CARD_TYPE_USB && !alsa_card->ucm) {
    syslog(LOG_WARNING, "No ucm config on internal card %s", card_name);
  }

  rc = snd_hctl_open(&alsa_card->hctl, alsa_card->name.str, SND_CTL_NONBLOCK);
  if (rc < 0) {
    syslog(LOG_DEBUG, "failed to get hctl for %s", alsa_card->name.str);
    alsa_card->hctl = NULL;
  } else {
    rc = snd_hctl_nonblock(alsa_card->hctl, 1);
    if (rc < 0) {
      syslog(LOG_WARNING, "failed to nonblock hctl for %s",
             alsa_card->name.str);
      goto error_bail;
    }

    rc = snd_hctl_load(alsa_card->hctl);
    if (rc < 0) {
      syslog(LOG_WARNING, "failed to load hctl for %s", alsa_card->name.str);
      goto error_bail;
    }
  }

  // Create one mixer per card.
  alsa_card->mixer = cras_alsa_mixer_create(alsa_card->name.str);

  if (alsa_card->mixer == NULL) {
    syslog(LOG_WARNING, "Fail opening mixer for %s.", alsa_card->name.str);
    goto error_bail;
  }

  if (alsa_card->ucm && ucm_has_fully_specified_ucm_flag(alsa_card->ucm)) {
    rc = add_controls_and_iodevs_with_ucm(info, alsa_card, card_name, handle);
  } else {
    rc =
        add_controls_and_iodevs_by_matching(info, alsa_card, card_name, handle);
  }
  if (rc) {
    goto error_bail;
  }

  configure_echo_reference_dev(alsa_card);

  n = alsa_card->hctl ? snd_hctl_poll_descriptors_count(alsa_card->hctl) : 0;
  if (n != 0 && card_has_hctl_jack(alsa_card)) {
    struct hctl_poll_fd* registered_fd;
    struct pollfd* pollfds;
    int i;

    pollfds = malloc(n * sizeof(*pollfds));
    if (pollfds == NULL) {
      rc = -ENOMEM;
      goto error_bail;
    }

    n = snd_hctl_poll_descriptors(alsa_card->hctl, pollfds, n);
    for (i = 0; i < n; i++) {
      registered_fd = calloc(1, sizeof(*registered_fd));
      if (registered_fd == NULL) {
        free(pollfds);
        rc = -ENOMEM;
        goto error_bail;
      }
      registered_fd->fd = pollfds[i].fd;
      DL_APPEND(alsa_card->hctl_poll_fds, registered_fd);
      rc = cras_system_add_select_fd(
          registered_fd->fd, alsa_control_event_pending, alsa_card, POLLIN);
      if (rc < 0) {
        DL_DELETE(alsa_card->hctl_poll_fds, registered_fd);
        free(pollfds);
        goto error_bail;
      }
    }
    free(pollfds);
  }

  snd_ctl_close(handle);
  return alsa_card;

error_bail:
  if (handle != NULL) {
    snd_ctl_close(handle);
  }
  cras_alsa_card_destroy(alsa_card);
  return NULL;
}

void cras_alsa_card_destroy(struct cras_alsa_card* alsa_card) {
  struct iodev_list_node* curr;
  struct hctl_poll_fd* poll_fd;

  if (alsa_card == NULL) {
    return;
  }

  DL_FOREACH (alsa_card->iodevs, curr) {
    cras_alsa_iodev_ops_destroy(alsa_card->ops, curr->iodev);
    DL_DELETE(alsa_card->iodevs, curr);
    free(curr);
  }
  DL_FOREACH (alsa_card->hctl_poll_fds, poll_fd) {
    cras_system_rm_select_fd(poll_fd->fd);
    DL_DELETE(alsa_card->hctl_poll_fds, poll_fd);
    free(poll_fd);
  }
  if (alsa_card->hctl) {
    snd_hctl_close(alsa_card->hctl);
  }
  if (alsa_card->ucm) {
    ucm_destroy(alsa_card->ucm);
  }
  if (alsa_card->mixer) {
    cras_alsa_mixer_destroy(alsa_card->mixer);
  }
  if (alsa_card->config) {
    cras_card_config_destroy(alsa_card->config);
  }
  cras_alsa_config_release_controls_on_card(alsa_card->card_index);
  free(alsa_card);
}

size_t cras_alsa_card_get_index(const struct cras_alsa_card* alsa_card) {
  CRAS_CHECK(alsa_card);
  return alsa_card->card_index;
}

enum CRAS_ALSA_CARD_TYPE cras_alsa_card_get_type(
    const struct cras_alsa_card* alsa_card) {
  CRAS_CHECK(alsa_card);
  return alsa_card->card_type;
}

bool cras_alsa_card_has_ucm(const struct cras_alsa_card* alsa_card) {
  return !!alsa_card->ucm;
}
