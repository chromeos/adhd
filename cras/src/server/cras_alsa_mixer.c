/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_alsa_mixer.h"

#include <alsa/asoundlib.h>
#include <alsa/mixer.h>
#include <limits.h>
#include <stdio.h>
#include <syslog.h>

#include "cras/src/common/cras_string.h"
#include "cras/src/server/cras_alsa_mixer_name.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras_util.h"
#include "third_party/utlist/utlist.h"

#define MIXER_CONTROL_VOLUME_DB_INVALID LONG_MAX
#define MIXER_CONTROL_STEP_INVALID 0

/* Represents an ALSA control element. Each device can have several of these,
 * each potentially having independent volume and mute controls.
 */
struct mixer_control_element {
  // ALSA mixer element.
  snd_mixer_elem_t* elem;
  // non-zero indicates there is a volume control.
  int has_volume;
  // non-zero indicates there is a mute switch.
  int has_mute;
  // the maximum volume for this control, or
  // MIXER_CONTROL_VOLUME_DB_INVALID.
  long max_volume_dB;
  // the minimum volume for this control, or
  // MIXER_CONTROL_VOLUME_DB_INVALID.
  long min_volume_dB;
  // number of volume steps for this control, or
  // MIXER_CONTROL_STEP_INVALID.
  int number_of_volume_steps;
  struct mixer_control_element *prev, *next;
};

/* Represents an ALSA control element related to a specific input/output
 * node such as speakers or headphones. A device can have several of these,
 * each potentially having independent volume and mute controls.
 *
 * Each will have at least one mixer_control_element. For cases where there
 * are separate control elements for left/right channels (for example),
 * additional mixer_control_elements are added.
 *
 * For controls with volume it is assumed that all elements have the same
 * range.
 */
struct mixer_control {
  // Name of the control (typicially this is the same as the name of the
  // mixer_control_element when there is one, or the name of the UCM
  // parent when there are multiple).
  const char* name;
  // Control direction, OUTPUT or INPUT only.
  enum CRAS_STREAM_DIRECTION dir;
  // The mixer_control_elements that are driven by this control.
  struct mixer_control_element* elements;
  // non-zero indicates there is a volume control.
  int has_volume;
  // non-zero indicates there is a mute switch.
  int has_mute;
  // Maximum volume available in the volume control.
  long max_volume_dB;
  // Minimum volume available in the volume control.
  long min_volume_dB;
  // number of volume steps in the volume control, or
  // MIXER_CONTROL_STEP_INVALID.
  int number_of_volume_steps;
  struct mixer_control *prev, *next;
};

// Holds a reference to the opened mixer and the volume controls.
struct cras_alsa_mixer {
  // Pointer to the opened alsa mixer.
  snd_mixer_t* mixer;
  // List of volume controls (normally 'Master' and 'PCM').
  struct mixer_control* main_volume_controls;
  struct mixer_control* output_controls;
  // Switch used to mute the device.
  snd_mixer_elem_t* playback_switch;
  // List of capture gain controls (normally 'Capture').
  struct mixer_control* main_capture_controls;
  struct mixer_control* input_controls;
  // Switch used to mute the capture stream.
  snd_mixer_elem_t* capture_switch;
  // Maximum volume available in main volume controls.  The dBFS
  // value setting will be applied relative to this.
  long max_volume_dB;
  // Minimum volume available in main volume controls.
  long min_volume_dB;
};

/* Wrapper for snd_mixer_open and helpers.
 * Args:
 *    mixdev - Name of the device to open the mixer for.
 *    mixer - Pointer filled with the opened mixer on success, NULL on failure.
 */
static void alsa_mixer_open(const char* mixdev, snd_mixer_t** mixer) {
  int rc;

  *mixer = NULL;
  rc = snd_mixer_open(mixer, 0);
  if (rc < 0) {
    syslog(LOG_ERR, "snd_mixer_open: %d: %s", rc, cras_strerror(-rc));
    return;
  }
  rc = snd_mixer_attach(*mixer, mixdev);
  if (rc < 0) {
    syslog(LOG_ERR, "snd_mixer_attach: %d: %s", rc, cras_strerror(-rc));
    goto fail_after_open;
  }
  rc = snd_mixer_selem_register(*mixer, NULL, NULL);
  if (rc < 0) {
    syslog(LOG_ERR, "snd_mixer_selem_register: %d: %s", rc, cras_strerror(-rc));
    goto fail_after_open;
  }
  rc = snd_mixer_load(*mixer);
  if (rc < 0) {
    syslog(LOG_ERR, "snd_mixer_load: %d: %s", rc, cras_strerror(-rc));
    goto fail_after_open;
  }
  return;

fail_after_open:
  snd_mixer_close(*mixer);
  *mixer = NULL;
}

static struct mixer_control_element* mixer_control_element_create(
    snd_mixer_elem_t* elem,
    enum CRAS_STREAM_DIRECTION dir) {
  struct mixer_control_element* c;
  long min, max;
  long min_step, max_step;

  if (!elem) {
    return NULL;
  }

  c = (struct mixer_control_element*)calloc(1, sizeof(*c));
  if (!c) {
    syslog(LOG_ERR, "No memory for mixer_control_elem.");
    return NULL;
  }

  c->elem = elem;
  c->max_volume_dB = MIXER_CONTROL_VOLUME_DB_INVALID;
  c->min_volume_dB = MIXER_CONTROL_VOLUME_DB_INVALID;
  c->number_of_volume_steps = MIXER_CONTROL_STEP_INVALID;

  if (dir == CRAS_STREAM_OUTPUT) {
    c->has_mute = snd_mixer_selem_has_playback_switch(elem);

    if (snd_mixer_selem_has_playback_volume(elem)) {
      if (snd_mixer_selem_get_playback_dB_range(elem, &min, &max) == 0) {
        c->max_volume_dB = max;
        c->min_volume_dB = min;
        c->has_volume = 1;
      }

      if (snd_mixer_selem_get_playback_volume_range(elem, &min_step,
                                                    &max_step) == 0 &&
          0 < (max_step - min_step) && (max_step - min_step) <= INT_MAX) {
        c->number_of_volume_steps = max_step - min_step;
      }

      if (c->number_of_volume_steps == MIXER_CONTROL_STEP_INVALID) {
        syslog(LOG_WARNING, "Name: [%s] Get invaild volume range [%ld:%ld]",
               snd_mixer_selem_get_name(elem), min_step, max_step);
      }
    }
  } else if (dir == CRAS_STREAM_INPUT) {
    c->has_mute = snd_mixer_selem_has_capture_switch(elem);

    if (snd_mixer_selem_has_capture_volume(elem) &&
        snd_mixer_selem_get_capture_dB_range(elem, &min, &max) == 0) {
      c->max_volume_dB = max;
      c->min_volume_dB = min;
      c->has_volume = 1;
    }
  }

  return c;
}

static void mixer_control_destroy(struct mixer_control* control) {
  struct mixer_control_element* elem;

  if (!control) {
    return;
  }

  DL_FOREACH (control->elements, elem) {
    DL_DELETE(control->elements, elem);
    free(elem);
  }
  if (control->name) {
    free((void*)control->name);
  }
  free(control);
}

static void mixer_control_destroy_list(struct mixer_control* control_list) {
  struct mixer_control* control;
  if (!control_list) {
    return;
  }
  DL_FOREACH (control_list, control) {
    DL_DELETE(control_list, control);
    mixer_control_destroy(control);
  }
}

static int mixer_control_add_element(struct mixer_control* control,
                                     snd_mixer_elem_t* snd_elem) {
  struct mixer_control_element* elem;

  if (!control) {
    return -EINVAL;
  }

  elem = mixer_control_element_create(snd_elem, control->dir);
  if (!elem) {
    return -ENOMEM;
  }

  DL_APPEND(control->elements, elem);

  if (elem->has_volume) {
    if (!control->has_volume) {
      control->has_volume = 1;
    }

    /* Assume that all elements have a common volume range, and
     * that both min and max values are valid if one of the two
     * is valid. */
    if (control->min_volume_dB == MIXER_CONTROL_VOLUME_DB_INVALID) {
      control->min_volume_dB = elem->min_volume_dB;
      control->max_volume_dB = elem->max_volume_dB;
      control->number_of_volume_steps = elem->number_of_volume_steps;
    } else if (control->min_volume_dB != elem->min_volume_dB ||
               control->max_volume_dB != elem->max_volume_dB) {
      syslog(LOG_WARNING,
             "Element '%s' of control '%s' has different"
             "volume range: [%ld:%ld] ctrl: [%ld:%ld]"
             "number_of_volume_steps[%d:%d]",
             snd_mixer_selem_get_name(elem->elem), control->name,
             elem->min_volume_dB, elem->max_volume_dB, control->min_volume_dB,
             control->max_volume_dB, control->number_of_volume_steps,
             elem->number_of_volume_steps);
    }
  }

  if (elem->has_mute && !control->has_mute) {
    control->has_mute = 1;
  }
  return 0;
}

static int mixer_control_create(struct mixer_control** control,
                                const char* name,
                                snd_mixer_elem_t* elem,
                                enum CRAS_STREAM_DIRECTION dir) {
  struct mixer_control* c;
  int rc = 0;

  if (!control) {
    return -EINVAL;
  }

  c = (struct mixer_control*)calloc(1, sizeof(*c));
  if (!c) {
    syslog(LOG_ERR, "No memory for mixer_control: %s", name);
    rc = -ENOMEM;
    goto error;
  }

  c->dir = dir;
  c->min_volume_dB = MIXER_CONTROL_VOLUME_DB_INVALID;
  c->max_volume_dB = MIXER_CONTROL_VOLUME_DB_INVALID;
  c->number_of_volume_steps = MIXER_CONTROL_STEP_INVALID;

  if (!name && elem) {
    name = snd_mixer_selem_get_name(elem);
  }
  if (!name) {
    syslog(LOG_WARNING, "Control does not have a name.");
    rc = -EINVAL;
    goto error;
  }

  c->name = strdup(name);
  if (!c->name) {
    syslog(LOG_ERR, "No memory for control's name: %s", name);
    rc = -ENOMEM;
    goto error;
  }

  if (elem && (rc = mixer_control_add_element(c, elem))) {
    goto error;
  }

  *control = c;
  return 0;

error:
  mixer_control_destroy(c);
  *control = NULL;
  return rc;
}

/* Creates a mixer_control by finding mixer element names in simple mixer
 * interface.
 * Args:
 *    control[out] - Storage for resulting pointer to mixer_control.
 *    cmix[in] - Parent alsa mixer.
 *    name[in] - Optional name of the control. Input NULL to take the name of
 *               the first element from mixer_names.
 *    mixer_names[in] - Names of the ASLA mixer control elements. Must not
 *                      be empty.
 *    dir[in] - Control direction: CRAS_STREAM_OUTPUT or CRAS_STREAM_INPUT.
 * Returns:
 *    Returns 0 for success, negative error code otherwise. *control is
 *    initialized to NULL on error, or has a valid pointer for success.
 */
static int mixer_control_create_by_name(struct mixer_control** control,
                                        struct cras_alsa_mixer* cmix,
                                        const char* name,
                                        struct mixer_name* mixer_names,
                                        enum CRAS_STREAM_DIRECTION dir) {
  snd_mixer_selem_id_t* sid;
  snd_mixer_elem_t* elem;
  struct mixer_control* c;
  struct mixer_name* m_name;
  int rc;

  if (!control) {
    return -EINVAL;
  }
  *control = NULL;
  if (!mixer_names) {
    return -EINVAL;
  }
  if (!name) {
    /* Assume that we're using the first name in the list of mixer
     * names. */
    name = mixer_names->name;
  }

  rc = mixer_control_create(&c, name, NULL, dir);
  if (rc) {
    return rc;
  }

  snd_mixer_selem_id_malloc(&sid);

  DL_FOREACH (mixer_names, m_name) {
    snd_mixer_selem_id_set_index(sid, m_name->index);
    snd_mixer_selem_id_set_name(sid, m_name->name);
    elem = snd_mixer_find_selem(cmix->mixer, sid);
    if (!elem) {
      mixer_control_destroy(c);
      snd_mixer_selem_id_free(sid);
      syslog(LOG_WARNING, "Unable to find simple control %s, %d", m_name->name,
             m_name->index);
      return -ENOENT;
    }
    rc = mixer_control_add_element(c, elem);
    if (rc) {
      mixer_control_destroy(c);
      snd_mixer_selem_id_free(sid);
      return rc;
    }
  }

  snd_mixer_selem_id_free(sid);
  *control = c;
  return 0;
}

static int mixer_control_set_dBFS(const struct mixer_control* control,
                                  long to_set) {
  const struct mixer_control_element* elem = NULL;
  int rc = -EINVAL;
  if (!control) {
    return rc;
  }
  DL_FOREACH (control->elements, elem) {
    if (elem->has_volume) {
      if (control->dir == CRAS_STREAM_OUTPUT) {
        rc = snd_mixer_selem_set_playback_dB_all(elem->elem, to_set, 1);
      } else if (control->dir == CRAS_STREAM_INPUT) {
        rc = snd_mixer_selem_set_capture_dB_all(elem->elem, to_set, 1);
      }
      if (rc) {
        break;
      }
      syslog(LOG_DEBUG, "%s:%s volume set to %ld", control->name,
             snd_mixer_selem_get_name(elem->elem), to_set);
    }
  }
  if (rc && elem) {
    syslog(LOG_WARNING, "Failed to set volume of '%s:%s': %d", control->name,
           snd_mixer_selem_get_name(elem->elem), rc);
  }
  return rc;
}

static int mixer_control_get_dBFS(const struct mixer_control* control,
                                  long* to_get) {
  const struct mixer_control_element* elem = NULL;
  int rc = -EINVAL;
  if (!control || !to_get) {
    return -EINVAL;
  }
  DL_FOREACH (control->elements, elem) {
    if (elem->has_volume) {
      if (control->dir == CRAS_STREAM_OUTPUT) {
        rc = snd_mixer_selem_get_playback_dB(elem->elem,
                                             SND_MIXER_SCHN_FRONT_LEFT, to_get);
      } else if (control->dir == CRAS_STREAM_INPUT) {
        rc = snd_mixer_selem_get_capture_dB(elem->elem,
                                            SND_MIXER_SCHN_FRONT_LEFT, to_get);
      }
      /* Assume all of the elements of this control have
       * the same value. */
      break;
    }
  }
  if (rc && elem) {
    syslog(LOG_WARNING, "Failed to get volume of '%s:%s': %d", control->name,
           snd_mixer_selem_get_name(elem->elem), rc);
  }
  return rc;
}

static int mixer_control_set_mute(const struct mixer_control* control,
                                  int muted) {
  const struct mixer_control_element* elem = NULL;
  int rc = -EINVAL;
  if (!control) {
    return -EINVAL;
  }
  DL_FOREACH (control->elements, elem) {
    if (elem->has_mute) {
      if (control->dir == CRAS_STREAM_OUTPUT) {
        rc = snd_mixer_selem_set_playback_switch_all(elem->elem, !muted);
      } else if (control->dir == CRAS_STREAM_INPUT) {
        rc = snd_mixer_selem_set_capture_switch_all(elem->elem, !muted);
      }
      if (rc) {
        break;
      }
    }
  }
  if (rc && elem) {
    syslog(LOG_WARNING, "Failed to mute '%s:%s': %d", control->name,
           snd_mixer_selem_get_name(elem->elem), rc);
  }
  return rc;
}

/* Adds the main volume control to the list and grabs the first seen playback
 * switch to use for mute. */
static int add_main_volume_control(struct cras_alsa_mixer* cmix,
                                   snd_mixer_elem_t* elem) {
  if (snd_mixer_selem_has_playback_volume(elem)) {
    long range;
    struct mixer_control *c, *next;
    int rc = mixer_control_create(&c, NULL, elem, CRAS_STREAM_OUTPUT);
    if (rc) {
      return rc;
    }

    if (c->has_volume) {
      cmix->max_volume_dB += c->max_volume_dB;
      cmix->min_volume_dB += c->min_volume_dB;
    }

    range = c->max_volume_dB - c->min_volume_dB;
    DL_FOREACH (cmix->main_volume_controls, next) {
      if (range > next->max_volume_dB - next->min_volume_dB) {
        break;
      }
    }

    syslog(LOG_DEBUG, "Add main volume control %s\n", c->name);
    DL_INSERT(cmix->main_volume_controls, next, c);
  }

  /* If cmix doesn't yet have a playback switch and this is a playback
   * switch, use it. */
  if (cmix->playback_switch == NULL &&
      snd_mixer_selem_has_playback_switch(elem)) {
    syslog(LOG_DEBUG, "Using '%s' as playback switch.",
           snd_mixer_selem_get_name(elem));
    cmix->playback_switch = elem;
  }

  return 0;
}

/* Adds the main capture control to the list and grabs the first seen capture
 * switch to mute input. */
static int add_main_capture_control(struct cras_alsa_mixer* cmix,
                                    snd_mixer_elem_t* elem) {
  // TODO(dgreid) handle index != 0, map to correct input.
  if (snd_mixer_selem_get_index(elem) > 0) {
    return 0;
  }

  if (snd_mixer_selem_has_capture_volume(elem)) {
    struct mixer_control* c;
    int rc = mixer_control_create(&c, NULL, elem, CRAS_STREAM_INPUT);
    if (rc) {
      return rc;
    }

    syslog(LOG_DEBUG, "Add main capture control %s\n", c->name);
    DL_APPEND(cmix->main_capture_controls, c);
  }

  /* If cmix doesn't yet have a capture switch and this is a capture
   * switch, use it. */
  if (cmix->capture_switch == NULL &&
      snd_mixer_selem_has_capture_switch(elem)) {
    syslog(LOG_DEBUG, "Using '%s' as capture switch.",
           snd_mixer_selem_get_name(elem));
    cmix->capture_switch = elem;
  }

  return 0;
}

static int add_control(struct cras_alsa_mixer* cmix,
                       enum CRAS_STREAM_DIRECTION dir,
                       snd_mixer_elem_t* elem) {
  int index;  // Index part of mixer simple element
  const char* name;
  struct mixer_control* c;
  int rc;

  index = snd_mixer_selem_get_index(elem);
  name = snd_mixer_selem_get_name(elem);
  syslog(LOG_DEBUG, "Add %s control: %s,%d\n",
         dir == CRAS_STREAM_OUTPUT ? "output" : "input", name, index);

  rc = mixer_control_create(&c, name, elem, dir);
  if (rc) {
    return rc;
  }

  if (c->has_volume) {
    syslog(LOG_DEBUG, "Control '%s' volume range: [%ld:%ld]", c->name,
           c->min_volume_dB, c->max_volume_dB);
  }

  if (dir == CRAS_STREAM_OUTPUT) {
    DL_APPEND(cmix->output_controls, c);
  } else if (dir == CRAS_STREAM_INPUT) {
    DL_APPEND(cmix->input_controls, c);
  }
  return 0;
}

static void list_controls(struct mixer_control* control_list,
                          cras_alsa_mixer_control_callback cb,
                          void* cb_arg) {
  struct mixer_control* control;

  DL_FOREACH (control_list, control) {
    cb(control, cb_arg);
  }
}

static struct mixer_control* get_control_matching_name(
    struct mixer_control* control_list,
    const char* name) {
  struct mixer_control* c;

  DL_FOREACH (control_list, c) {
    if (strstr(name, c->name)) {
      return c;
    }
  }
  return NULL;
}

// Creates a mixer_control with multiple control elements.
static int add_control_with_coupled_mixers(
    struct cras_alsa_mixer* cmix,
    enum CRAS_STREAM_DIRECTION dir,
    const char* name,
    struct mixer_name* coupled_controls) {
  struct mixer_control* c;
  int rc;

  rc = mixer_control_create_by_name(&c, cmix, name, coupled_controls, dir);
  if (rc) {
    return rc;
  }
  syslog(LOG_DEBUG, "Add %s control: %s\n",
         dir == CRAS_STREAM_OUTPUT ? "output" : "input", c->name);
  mixer_name_dump(coupled_controls, "  elements");

  if (c->has_volume) {
    syslog(LOG_DEBUG, "Control '%s' volume range: [%ld:%ld]", c->name,
           c->min_volume_dB, c->max_volume_dB);
  }

  if (dir == CRAS_STREAM_OUTPUT) {
    DL_APPEND(cmix->output_controls, c);
  } else if (dir == CRAS_STREAM_INPUT) {
    DL_APPEND(cmix->input_controls, c);
  }
  return 0;
}

static int add_control_by_name(struct cras_alsa_mixer* cmix,
                               enum CRAS_STREAM_DIRECTION dir,
                               const char* name) {
  struct mixer_control* c;
  struct mixer_name* m_name;
  int rc;

  m_name = mixer_name_add(NULL, name, dir, MIXER_NAME_VOLUME);
  if (!m_name) {
    return -ENOMEM;
  }

  rc = mixer_control_create_by_name(&c, cmix, name, m_name, dir);
  mixer_name_free(m_name);
  if (rc) {
    return rc;
  }
  syslog(LOG_DEBUG, "Add %s control: %s\n",
         dir == CRAS_STREAM_OUTPUT ? "output" : "input", c->name);

  if (c->has_volume) {
    syslog(LOG_DEBUG, "Control '%s' volume range: [%ld:%ld]", c->name,
           c->min_volume_dB, c->max_volume_dB);
  }

  if (dir == CRAS_STREAM_OUTPUT) {
    DL_APPEND(cmix->output_controls, c);
  } else if (dir == CRAS_STREAM_INPUT) {
    DL_APPEND(cmix->input_controls, c);
  }
  return 0;
}

/* Combine multiple headphone controls into one.
 *
 * Most devices have just one headphone jack with a corresponding
 * volume control and mute switch. Looking at the output of `amixer
 * controls` you might see something like this:
 *
 *     numid=25,iface=CARD,name='Headphone Jack'
 *     numid=4,iface=MIXER,name='Headphone Playback Switch'
 *     numid=3,iface=MIXER,name='Headphone Playback Volume'
 *
 * This gets further simplified in the simple controls, so `amixer
 * scontrols` will have a single control combining the playback switch
 * and volume:
 *
 *     Simple mixer control 'Headphone',0
 *
 * Some devices have an optional dock that has its own headphone
 * jack. Now things get more complicated, because the two headphone
 * jacks can have separate mute switches but a shared volume
 * control. Now the `amixer controls` look like this:
 *
 *     numid=24,iface=CARD,name='Dock Headphone Jack'
 *     numid=25,iface=CARD,name='Headphone Jack'
 *     numid=4,iface=MIXER,name='Headphone Playback Switch'
 *     numid=5,iface=MIXER,name='Headphone Playback Switch',index=1
 *     numid=3,iface=MIXER,name='Headphone Playback Volume'
 *
 * And the `amixer scontrols` look like this:
 *
 *     Simple mixer control 'Headphone',0
 *     Simple mixer control 'Headphone',1
 *
 * Both of the simple mixer controls have a mute switch, but only the
 * first one has a volume control, but that volume control actually
 * controls both the dock and regular headphone outputs.
 *
 * When a headphone is plugged in, cras is supposed to mute the
 * speaker control, unmute the headphone control, and raise the
 * headphone volume. It uses a simple substring search to match jacks
 * with outputs. See `find_jack_controls` where it calls
 * `cras_alsa_mixer_get_output_matching_name`. This is basically
 * matching the "Headphone Jack" control (which reports whether the
 * headphones are plugged in or not) with the first simple mixer
 * control that contains the string "Headphone". So now whichever
 * headphone jack is plugged in, the first simple Headphone control
 * will get unmuted and volume raised. On at least some models (such
 * as the Lenovo x240) the first headphone control is for the dock
 * headphone control, so when plugging into the regular headphone jack
 * you don't get any sound.
 *
 * To fix this, search all of the mixer output controls for the ones
 * named "Headphone". If there's more than one, take all of the mixer
 * control elements (these are things like left and right channel
 * volume and mute switch) and add them to the first headphone
 * control. Then, delete the additional control.
 *
 * This fix does result in a little less control for the user compared
 * to some ideal fix. If they have a dock, they won't be able to see
 * separate Headphone and Dock Headphone outputs in the audio menu;
 * they'll just get a single Headphone output that controls both. We
 * could potentially fix this by modifying the kernel such that the
 * dock headphone controls were named with a "Dock" prefix, then
 * modifying cras to know about Dock Headphones, and also tie the
 * volume controls to both the Dock Headphone output and the regular
 * Headphone output. This is probably more complication than we need
 * to handle for now though, considering that although we do support
 * devices that have optional docks, we don't actually support any
 * docks right now.  */
static void combine_headphone_controls(struct cras_alsa_mixer* cmix) {
  struct mixer_control* control;
  struct mixer_control* first_headphone_control = NULL;

  DL_FOREACH (cmix->output_controls, control) {
    // Skip non-headphone controls
    if (strcmp(control->name, "Headphone") != 0) {
      continue;
    }

    if (first_headphone_control) {
      syslog(LOG_INFO, "Removing additional headphone control\n");

      /* Move the control elements from this control to the first
       * headphone control */
      DL_CONCAT(first_headphone_control->elements, control->elements);
      control->elements = NULL;

      /* Remove this control from the mixer. According to comments in
       * cras/src/common/utlist.h it is safe to do this during
       * iteration. */
      DL_DELETE(cmix->output_controls, control);
      mixer_control_destroy(control);
    } else {
      first_headphone_control = control;
    }
  }
}

/*
 * Exported interface.
 */

struct cras_alsa_mixer* cras_alsa_mixer_create(const char* card_name) {
  struct cras_alsa_mixer* cmix;

  cmix = (struct cras_alsa_mixer*)calloc(1, sizeof(*cmix));
  if (cmix == NULL) {
    return NULL;
  }

  syslog(LOG_DEBUG, "Add mixer for device %s", card_name);

  alsa_mixer_open(card_name, &cmix->mixer);

  return cmix;
}

// Names of controls for main system volume.
static const char* const main_volume_names[] = {
    "Master",
    "Digital",
    "PCM",
};
// Names of controls for individual outputs.
static const char* const output_names[] = {
    "Headphone", "Headset", "Headset Earphone", "HDMI", "Speaker",
};
// Names of controls for capture gain/attenuation and mute.
static const char* const main_capture_names[] = {
    "Capture",
    "Digital Capture",
};
// Names of controls for individual inputs.
static const char* const input_names[] = {"Mic", "Microphone", "Headset"};

int cras_alsa_mixer_add_controls_by_name_matching_usb(
    struct cras_alsa_mixer* cmix) {
  struct mixer_name* default_controls = NULL;
  snd_mixer_elem_t* elem;
  const char* name;
  struct mixer_name* control;
  int rc = 0;
  bool output_control_found = false;

  // Note that there is no mixer on some usb soundcards.
  if (cmix->mixer == NULL) {
    syslog(LOG_WARNING, "No mixer on this soundcard");
    return 0;
  }

  default_controls = mixer_name_add_array(
      default_controls, output_names, ARRAY_SIZE(output_names),
      CRAS_STREAM_OUTPUT, MIXER_NAME_VOLUME);
  default_controls = mixer_name_add_array(
      default_controls, main_volume_names, ARRAY_SIZE(main_volume_names),
      CRAS_STREAM_OUTPUT, MIXER_NAME_VOLUME);

  // Find output volume control.
  for (elem = snd_mixer_first_elem(cmix->mixer); elem != NULL;
       elem = snd_mixer_elem_next(elem)) {
    name = snd_mixer_selem_get_name(elem);
    control = mixer_name_find(default_controls, name, CRAS_STREAM_OUTPUT,
                              MIXER_NAME_UNDEFINED);
    if (control && snd_mixer_selem_has_playback_volume(elem)) {
      rc = add_control(cmix, CRAS_STREAM_OUTPUT, elem);
      if (rc) {
        syslog(LOG_WARNING,
               "Failed to add playback mixer control '%s'"
               " with type '%d' rc '%d'",
               control->name, control->type, rc);
        goto out;
      }
      output_control_found = true;
    }
  }

  default_controls = mixer_name_add_array(default_controls, input_names,
                                          ARRAY_SIZE(input_names),
                                          CRAS_STREAM_INPUT, MIXER_NAME_VOLUME);
  default_controls = mixer_name_add_array(
      default_controls, main_capture_names, ARRAY_SIZE(main_capture_names),
      CRAS_STREAM_INPUT, MIXER_NAME_MAIN_VOLUME);

  // Find input volume control.
  for (elem = snd_mixer_first_elem(cmix->mixer); elem != NULL;
       elem = snd_mixer_elem_next(elem)) {
    name = snd_mixer_selem_get_name(elem);
    control = mixer_name_find(default_controls, name, CRAS_STREAM_INPUT,
                              MIXER_NAME_UNDEFINED);

    if (control && snd_mixer_selem_has_capture_volume(elem)) {
      switch (control->type) {
        case MIXER_NAME_MAIN_VOLUME:
          rc = add_main_capture_control(cmix, elem);
          break;
        case MIXER_NAME_VOLUME:
          rc = add_control(cmix, CRAS_STREAM_INPUT, elem);
          break;
        case MIXER_NAME_UNDEFINED:
          rc = -EINVAL;
          break;
      }
      if (rc) {
        syslog(LOG_WARNING,
               "Failed to add capture mixer control '%s'"
               " with type '%d' rc '%d'",
               control->name, control->type, rc);
        goto out;
      }
    }
  }

  /* If there is no volume control and output control found,
   * use the volume control which has the largest volume range
   * in the mixer as volume control. */
  if (!output_control_found) {
    snd_mixer_elem_t* max_range_elem = NULL;
    long max_range = 0;

    for (elem = snd_mixer_first_elem(cmix->mixer); elem != NULL;
         elem = snd_mixer_elem_next(elem)) {
      long min, max, range;

      if (!snd_mixer_selem_has_playback_volume(elem) ||
          snd_mixer_selem_get_playback_dB_range(elem, &min, &max) != 0) {
        continue;
      }

      range = max - min;
      if (max_range < range) {
        max_range = range;
        max_range_elem = elem;
      }
    }
    if (max_range_elem) {
      rc = add_control(cmix, CRAS_STREAM_OUTPUT, max_range_elem);
      if (rc) {
        syslog(LOG_WARNING,
               "Failed to add largest volume range mixer control '%s'"
               " rc '%d'",
               snd_mixer_selem_get_name(max_range_elem), rc);
        goto out;
      }
    }
  }

out:
  mixer_name_free(default_controls);
  return rc;
}

int cras_alsa_mixer_add_controls_by_name_matching_internal(
    struct cras_alsa_mixer* cmix,
    struct mixer_name* extra_controls,
    struct mixer_name* coupled_controls) {
  // Names of controls for main system volume.

  struct mixer_name* default_controls = NULL;
  snd_mixer_elem_t* elem;
  int extra_main_volume = 0;
  snd_mixer_elem_t* other_elem = NULL;
  long other_dB_range = 0;
  int rc = 0;

  // Note that there is no mixer on some cards. This is acceptable.
  if (cmix->mixer == NULL) {
    syslog(LOG_DEBUG, "Couldn't open mixer.");
    return 0;
  }

  default_controls = mixer_name_add_array(
      default_controls, output_names, ARRAY_SIZE(output_names),
      CRAS_STREAM_OUTPUT, MIXER_NAME_VOLUME);
  default_controls = mixer_name_add_array(default_controls, input_names,
                                          ARRAY_SIZE(input_names),
                                          CRAS_STREAM_INPUT, MIXER_NAME_VOLUME);
  default_controls = mixer_name_add_array(
      default_controls, main_volume_names, ARRAY_SIZE(main_volume_names),
      CRAS_STREAM_OUTPUT, MIXER_NAME_MAIN_VOLUME);
  default_controls = mixer_name_add_array(
      default_controls, main_capture_names, ARRAY_SIZE(main_capture_names),
      CRAS_STREAM_INPUT, MIXER_NAME_MAIN_VOLUME);
  extra_main_volume = mixer_name_find(extra_controls, NULL, CRAS_STREAM_OUTPUT,
                                      MIXER_NAME_MAIN_VOLUME) != NULL;

  // Find volume and mute controls.
  for (elem = snd_mixer_first_elem(cmix->mixer); elem != NULL;
       elem = snd_mixer_elem_next(elem)) {
    const char* name;
    struct mixer_name* control;
    int found = 0;

    name = snd_mixer_selem_get_name(elem);
    if (name == NULL) {
      continue;
    }

    // Find a matching control.
    control = mixer_name_find(default_controls, name, CRAS_STREAM_OUTPUT,
                              MIXER_NAME_UNDEFINED);

    /* If our extra controls contain a main volume
     * entry, and we found a main volume entry, then
     * skip it. */
    if (extra_main_volume && control &&
        control->type == MIXER_NAME_MAIN_VOLUME) {
      control = NULL;
    }

    /* If we didn't match any of the defaults, match
     * the extras list. */
    if (!control) {
      control = mixer_name_find(extra_controls, name, CRAS_STREAM_OUTPUT,
                                MIXER_NAME_UNDEFINED);
    }

    if (control) {
      int rc = -1;
      switch (control->type) {
        case MIXER_NAME_MAIN_VOLUME:
          rc = add_main_volume_control(cmix, elem);
          break;
        case MIXER_NAME_VOLUME:
          rc = add_control(cmix, CRAS_STREAM_OUTPUT, elem);
          break;
        case MIXER_NAME_UNDEFINED:
          rc = -EINVAL;
          break;
      }
      if (rc) {
        syslog(LOG_WARNING,
               "Failed to add mixer control '%s'"
               " with type '%d'",
               control->name, control->type);
        goto out;
      }
      found = 1;
    }

    // Find a matching input control.
    control = mixer_name_find(default_controls, name, CRAS_STREAM_INPUT,
                              MIXER_NAME_UNDEFINED);

    /* If we didn't match any of the defaults, match
       the extras list */
    if (!control) {
      control = mixer_name_find(extra_controls, name, CRAS_STREAM_INPUT,
                                MIXER_NAME_UNDEFINED);
    }

    if (control) {
      int rc = -1;
      switch (control->type) {
        case MIXER_NAME_MAIN_VOLUME:
          rc = add_main_capture_control(cmix, elem);
          break;
        case MIXER_NAME_VOLUME:
          rc = add_control(cmix, CRAS_STREAM_INPUT, elem);
          break;
        case MIXER_NAME_UNDEFINED:
          rc = -EINVAL;
          break;
      }
      if (rc) {
        syslog(LOG_WARNING,
               "Failed to add mixer control '%s'"
               " with type '%d'",
               control->name, control->type);
        goto out;
      }
      found = 1;
    }

    if (!found && snd_mixer_selem_has_playback_volume(elem)) {
      /* Temporarily cache one elem whose name is not
       * in the list above, but has a playback volume
       * control and the largest volume range. */
      long min, max, range;
      if (snd_mixer_selem_get_playback_dB_range(elem, &min, &max) != 0) {
        continue;
      }

      range = max - min;
      if (other_dB_range < range) {
        other_dB_range = range;
        other_elem = elem;
      }
    }
  }

  combine_headphone_controls(cmix);

  // Handle coupled output names for speaker
  if (coupled_controls) {
    rc = add_control_with_coupled_mixers(cmix, CRAS_STREAM_OUTPUT, "Speaker",
                                         coupled_controls);
    if (rc) {
      syslog(LOG_WARNING, "Could not add coupled output");
      goto out;
    }
  }

  /* If there is no volume control and output control found,
   * use the volume control which has the largest volume range
   * in the mixer as a main volume control. */
  if (!cmix->main_volume_controls && !cmix->output_controls && other_elem) {
    rc = add_main_volume_control(cmix, other_elem);
    if (rc) {
      syslog(LOG_WARNING, "Could not add other volume control");
      goto out;
    }
  }

out:
  mixer_name_free(default_controls);
  return rc;
}

int cras_alsa_mixer_add_main_volume_control_by_name(
    struct cras_alsa_mixer* cmix,
    struct mixer_name* mixer_names) {
  snd_mixer_elem_t* elem;
  struct mixer_name* m_name;
  int rc = 0;
  snd_mixer_selem_id_t* sid;

  if (!mixer_names) {
    return -EINVAL;
  }

  snd_mixer_selem_id_malloc(&sid);

  DL_FOREACH (mixer_names, m_name) {
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, m_name->name);
    elem = snd_mixer_find_selem(cmix->mixer, sid);
    if (!elem) {
      rc = -ENOENT;
      syslog(LOG_WARNING, "Unable to find simple control %s, 0", m_name->name);
      break;
    }
    rc = add_main_volume_control(cmix, elem);
    if (rc) {
      break;
    }
  }

  snd_mixer_selem_id_free(sid);
  return rc;
}

int cras_alsa_mixer_add_controls_in_section(struct cras_alsa_mixer* cmix,
                                            struct ucm_section* section) {
  int rc;

  // Note that there is no mixer on some cards. This is acceptable.
  if (cmix->mixer == NULL) {
    syslog(LOG_DEBUG, "Couldn't open mixer.");
    return 0;
  }

  if (!section) {
    syslog(LOG_ERR, "No UCM SectionDevice specified.");
    return -EINVAL;
  }

  // TODO(muirj) - Extra main volume controls when fully-specified.

  if (section->mixer_name) {
    rc = add_control_by_name(cmix, section->dir, section->mixer_name);
    if (rc) {
      syslog(LOG_WARNING, "Could not add mixer control '%s': %s",
             section->mixer_name, cras_strerror(-rc));
      return rc;
    }
  }

  if (section->coupled) {
    rc = add_control_with_coupled_mixers(cmix, section->dir, section->name,
                                         section->coupled);
    if (rc) {
      syslog(LOG_WARNING, "Could not add coupled control: %s",
             cras_strerror(-rc));
      return rc;
    }
  }
  return 0;
}

void cras_alsa_mixer_destroy(struct cras_alsa_mixer* cras_mixer) {
  assert(cras_mixer);

  mixer_control_destroy_list(cras_mixer->main_volume_controls);
  mixer_control_destroy_list(cras_mixer->main_capture_controls);
  mixer_control_destroy_list(cras_mixer->output_controls);
  mixer_control_destroy_list(cras_mixer->input_controls);
  if (cras_mixer->mixer) {
    snd_mixer_close(cras_mixer->mixer);
  }
  free(cras_mixer);
}

int cras_alsa_mixer_has_main_volume(const struct cras_alsa_mixer* cras_mixer) {
  return !!cras_mixer->main_volume_controls;
}

int cras_alsa_mixer_has_volume(const struct mixer_control* mixer_control) {
  return mixer_control && mixer_control->has_volume;
}

void cras_alsa_mixer_set_dBFS(struct cras_alsa_mixer* cras_mixer,
                              long dBFS,
                              struct mixer_control* mixer_output) {
  struct mixer_control* c;
  long to_set;

  assert(cras_mixer);

  if (dBFS > 0) {
    syslog(LOG_WARNING, "dBFS to set should <= 0 but instead %ld", dBFS);
  }
  /* dBFS is normally < 0 to specify the attenuation from max. max is the
   * combined max of the main controls and the current output.
   */
  to_set = dBFS + cras_mixer->max_volume_dB;
  if (cras_alsa_mixer_has_volume(mixer_output)) {
    to_set += mixer_output->max_volume_dB;
  }
  /* Go through all the controls, set the volume level for each,
   * taking the value closest but greater than the desired volume.  If the
   * entire volume can't be set on the current control, move on to the
   * next one until we have the exact volume, or gotten as close as we
   * can. Once all of the volume is set the rest of the controls should be
   * set to 0dB. */
  DL_FOREACH (cras_mixer->main_volume_controls, c) {
    long actual_dB;

    if (!c->has_volume) {
      continue;
    }
    if (mixer_control_set_dBFS(c, to_set) == 0 &&
        mixer_control_get_dBFS(c, &actual_dB) == 0) {
      to_set -= actual_dB;
    }
  }
  // Apply the rest to the output-specific control.
  if (cras_alsa_mixer_has_volume(mixer_output)) {
    mixer_control_set_dBFS(mixer_output, to_set);
  }
}

void cras_alsa_mixer_get_playback_dBFS_range(struct cras_alsa_mixer* cras_mixer,
                                             struct mixer_control* mixer_output,
                                             long* max_volume_dB,
                                             long* min_volume_dB) {
  *max_volume_dB = 0;
  *min_volume_dB = 0;

  if (cras_alsa_mixer_has_main_volume(cras_mixer)) {
    *max_volume_dB += cras_mixer->max_volume_dB;
    *min_volume_dB += cras_mixer->min_volume_dB;
  }

  if (cras_alsa_mixer_has_volume(mixer_output) &&
      mixer_output->max_volume_dB != MIXER_CONTROL_VOLUME_DB_INVALID &&
      mixer_output->min_volume_dB != MIXER_CONTROL_VOLUME_DB_INVALID) {
    *max_volume_dB += mixer_output->max_volume_dB;
    *min_volume_dB += mixer_output->min_volume_dB;
  }
}

int cras_alsa_mixer_get_playback_step(struct mixer_control* mixer_output) {
  if (cras_alsa_mixer_has_volume(mixer_output)) {
    return mixer_output->number_of_volume_steps;
  }
  return MIXER_CONTROL_STEP_INVALID;
}

void cras_alsa_mixer_set_capture_dBFS(struct cras_alsa_mixer* cras_mixer,
                                      long dBFS,
                                      struct mixer_control* mixer_input) {
  assert(cras_mixer);

  // Ensure the mixer is _not_ muted.
  if (cras_mixer->capture_switch) {
    snd_mixer_selem_set_capture_switch_all(cras_mixer->capture_switch, true);
  } else if (mixer_input && mixer_input->has_mute) {
    mixer_control_set_mute(mixer_input, false);
  }

  /* Go through all the controls, set the gain for each, taking the value
   * closest but greater than the desired gain.  If the entire gain can't
   * be set on the current control, move on to the next one until we have
   * the exact gain, or gotten as close as we can. Once all of the gain is
   * set the rest of the controls should be set to 0dB. */
  long to_set = dBFS;
  struct mixer_control* c;
  DL_FOREACH (cras_mixer->main_capture_controls, c) {
    long actual_dB;

    if (!c->has_volume) {
      continue;
    }
    if (mixer_control_set_dBFS(c, to_set) == 0 &&
        mixer_control_get_dBFS(c, &actual_dB) == 0) {
      to_set -= actual_dB;
    }
  }

  // Apply the reset to input specific control
  if (cras_alsa_mixer_has_volume(mixer_input)) {
    mixer_control_set_dBFS(mixer_input, to_set);
  }
}

long cras_alsa_mixer_get_minimum_capture_gain(
    struct cras_alsa_mixer* cmix,
    struct mixer_control* mixer_input) {
  struct mixer_control* c;
  long total_min = 0;

  assert(cmix);
  DL_FOREACH (cmix->main_capture_controls, c) {
    if (c->has_volume) {
      total_min += c->min_volume_dB;
    }
  }
  if (mixer_input && mixer_input->has_volume) {
    total_min += mixer_input->min_volume_dB;
  }

  return total_min;
}

long cras_alsa_mixer_get_maximum_capture_gain(
    struct cras_alsa_mixer* cmix,
    struct mixer_control* mixer_input) {
  struct mixer_control* c;
  long total_max = 0;

  assert(cmix);
  DL_FOREACH (cmix->main_capture_controls, c) {
    if (c->has_volume) {
      total_max += c->max_volume_dB;
    }
  }

  if (mixer_input && mixer_input->has_volume) {
    total_max += mixer_input->max_volume_dB;
  }

  return total_max;
}

void cras_alsa_mixer_set_mute(struct cras_alsa_mixer* cras_mixer,
                              int muted,
                              struct mixer_control* mixer_output) {
  assert(cras_mixer);

  if (cras_mixer->playback_switch) {
    snd_mixer_selem_set_playback_switch_all(cras_mixer->playback_switch,
                                            !muted);
  }
  if (mixer_output && mixer_output->has_mute) {
    mixer_control_set_mute(mixer_output, muted);
  }
}

void cras_alsa_mixer_list_outputs(struct cras_alsa_mixer* cras_mixer,
                                  cras_alsa_mixer_control_callback cb,
                                  void* cb_arg) {
  assert(cras_mixer);
  list_controls(cras_mixer->output_controls, cb, cb_arg);
}

void cras_alsa_mixer_list_inputs(struct cras_alsa_mixer* cras_mixer,
                                 cras_alsa_mixer_control_callback cb,
                                 void* cb_arg) {
  assert(cras_mixer);
  list_controls(cras_mixer->input_controls, cb, cb_arg);
}

const char* cras_alsa_mixer_get_control_name(
    const struct mixer_control* control) {
  if (!control) {
    return NULL;
  }
  return control->name;
}

struct mixer_control* cras_alsa_mixer_get_control_matching_name(
    struct cras_alsa_mixer* cras_mixer,
    enum CRAS_STREAM_DIRECTION dir,
    const char* name,
    int create_missing) {
  struct mixer_control* c;

  assert(cras_mixer);
  if (!name) {
    return NULL;
  }

  if (dir == CRAS_STREAM_OUTPUT) {
    c = get_control_matching_name(cras_mixer->output_controls, name);
  } else if (dir == CRAS_STREAM_INPUT) {
    c = get_control_matching_name(cras_mixer->input_controls, name);
  } else {
    return NULL;
  }

  /* TODO: Allowing creation of a new control is a workaround: we
   * should pass the input names in ucm config to
   * cras_alsa_mixer_create. */
  if (!c && cras_mixer->mixer && create_missing) {
    int rc = add_control_by_name(cras_mixer, dir, name);
    if (rc) {
      return NULL;
    }
    c = cras_alsa_mixer_get_control_matching_name(cras_mixer, dir, name, 0);
  }
  return c;
}

struct mixer_control* cras_alsa_mixer_get_control_for_section(
    struct cras_alsa_mixer* cras_mixer,
    const struct ucm_section* section) {
  assert(cras_mixer && section);
  if (section->mixer_name) {
    return cras_alsa_mixer_get_control_matching_name(cras_mixer, section->dir,
                                                     section->mixer_name, 0);
  } else if (section->coupled) {
    return cras_alsa_mixer_get_control_matching_name(cras_mixer, section->dir,
                                                     section->name, 0);
  }
  return NULL;
}

struct mixer_control* cras_alsa_mixer_get_output_matching_name(
    struct cras_alsa_mixer* cras_mixer,
    const char* const name) {
  return cras_alsa_mixer_get_control_matching_name(cras_mixer,
                                                   CRAS_STREAM_OUTPUT, name, 0);
}

struct mixer_control* cras_alsa_mixer_get_input_matching_name(
    struct cras_alsa_mixer* cras_mixer,
    const char* name) {
  /* TODO: Allowing creation of a new control is a workaround: we
   * should pass the input names in ucm config to
   * cras_alsa_mixer_create. */
  return cras_alsa_mixer_get_control_matching_name(cras_mixer,
                                                   CRAS_STREAM_INPUT, name, 1);
}

int cras_alsa_mixer_set_output_active_state(struct mixer_control* output,
                                            int active) {
  assert(output);
  if (!output->has_mute) {
    return -EINVAL;
  }
  return mixer_control_set_mute(output, !active);
}
