// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_ALSA_JACK_PRIVATE_H_
#define CRAS_ALSA_JACK_PRIVATE_H_

#include <alsa/asoundlib.h>

#include "cras_alsa_jack.h"

/* cras_gpio_jack:  Describes headphone & microphone jack connected to GPIO
 *
 *   On Arm-based systems, the headphone & microphone jacks are
 *   connected to GPIOs which are plumbed through the /dev/input/event
 *   system.  For these jacks, the software is written to open the
 *   corresponding /dev/input/event file and monitor it for 'insert' &
 *   'remove' activity.
 *
 *   fd           : File descriptor corresponding to the /dev/input/event file.
 *
 *   switch_event : Indicates the type of the /dev/input/event file.
 *                  Either SW_HEADPHONE_INSERT, or SW_MICROPHONE_INSERT.
 *
 *   current_state: 0 -> device not plugged in
 *                  1 -> device plugged in
 *   device_name  : Device name extracted from /dev/input/event[0..9]+.
 *                  Allocated on heap; must free.
 */
struct cras_gpio_jack {
	int fd;
	unsigned switch_event;
	unsigned current_state;
	char *device_name;
};

/* Represents a single alsa Jack, e.g. "Headphone Jack" or "Mic Jack".
 *    is_gpio: 1 -> gpio switch (union field: gpio)
 *             0 -> Alsa 'jack' (union field: elem)
 *    elem - alsa hcontrol element for this jack, when is_gpio == 0.
 *    gpio - description of gpio-based jack, when is_gpio != 0.
 *    eld_control - mixer control for ELD info buffer.
 *    jack_list - list of jacks this belongs to.
 *    mixer_output - mixer output control used to control audio to this jack.
 *        This will be null for input jacks.
 *    mixer_input - mixer input control used to control audio to this jack.
 *        This will be null for output jacks.
 *    ucm_device - Name of the ucm device if found, otherwise, NULL.
 *    edid_file - File to read the EDID from (if available, HDMI only).
 *    display_info_timer - Timer used to poll display info for HDMI jacks.
 *    display_info_retries - Number of times to retry reading display info.
 *
 *    mixer_output/mixer_input fields are only used to find the node for this
 *    jack. These are not used for setting volume or mute. There should be a
 *    1:1 map between node and jack. node->jack follows the pointer; jack->node
 *    is done by either searching node->jack pointers or searching the node that
 *    has the same mixer_control as the jack.
 */
struct cras_alsa_jack {
	unsigned is_gpio; /* !0 -> 'gpio' valid
			   *  0 -> 'elem' valid
			   */
	union {
		snd_hctl_elem_t *elem;
		struct cras_gpio_jack gpio;
	};

	snd_hctl_elem_t *eld_control;
	struct cras_alsa_jack_list *jack_list;
	struct mixer_control *mixer_output;
	struct mixer_control *mixer_input;
	char *ucm_device;
	const char *override_type_name;
	const char *edid_file;
	struct cras_timer *display_info_timer;
	unsigned int display_info_retries;
	struct cras_alsa_jack *prev, *next;
};

#endif
