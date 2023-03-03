// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_CRAS_ALSA_JACK_PRIVATE_H_
#define CRAS_SRC_SERVER_CRAS_ALSA_JACK_PRIVATE_H_

#include <alsa/asoundlib.h>

#include "cras/src/server/cras_alsa_jack.h"

/* cras_gpio_jack:  Describes headphone & microphone jack connected to GPIO
 *
 *   On Arm-based systems, the headphone & microphone jacks are
 *   connected to GPIOs which are plumbed through the /dev/input/event
 *   system.  For these jacks, the software is written to open the
 *   corresponding /dev/input/event file and monitor it for 'insert' &
 *   'remove' activity.
 */
struct cras_gpio_jack {
  // File descriptor corresponding to the /dev/input/event file.
  int fd;
  // Indicates the type of the /dev/input/event file.
  // Either SW_HEADPHONE_INSERT, or SW_MICROPHONE_INSERT.
  unsigned switch_event;
  // 0 -> device not plugged in
  // 1 -> device plugged in
  unsigned current_state;
  // Device name extracted from /dev/input/event[0..9]+.
  // Allocated on heap; must free.
  char* device_name;
};

/* Represents a single alsa Jack, e.g. "Headphone Jack" or "Mic Jack".
 *
 * mixer_output/mixer_input fields are only used to find the node for this
 * jack. These are not used for setting volume or mute. There should be a
 * 1:1 map between node and jack. node->jack follows the pointer; jack->node
 * is done by either searching node->jack pointers or searching the node that
 * has the same mixer_control as the jack.
 */
struct cras_alsa_jack {
  // 1 -> gpio switch (union field: gpio)
  // 0 -> Alsa 'jack' (union field: elem)
  unsigned is_gpio;
  union {
    // alsa hcontrol element for this jack, when is_gpio == 0.
    snd_hctl_elem_t* elem;
    // description of gpio-based jack, when is_gpio != 0.
    struct cras_gpio_jack gpio;
  };

  // mixer control for ELD info buffer.
  snd_hctl_elem_t* eld_control;
  // list of jacks this belongs to.
  struct cras_alsa_jack_list* jack_list;
  // mixer output control used to control audio to this jack.
  // This will be null for input jacks.
  struct mixer_control* mixer_output;
  // mixer input control used to control audio to this jack.
  // This will be null for output jacks.
  struct mixer_control* mixer_input;
  // Name of the ucm device if found, otherwise, NULL.
  char* ucm_device;
  const char* override_type_name;
  // File to read the EDID from (if available, HDMI only).
  const char* edid_file;
  // Timer used to poll display info for HDMI jacks.
  struct cras_timer* display_info_timer;
  // Number of times to retry reading display info.
  unsigned int display_info_retries;
  struct cras_alsa_jack *prev, *next;
};

#endif
