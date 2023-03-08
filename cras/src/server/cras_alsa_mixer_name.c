/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_alsa_mixer_name.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "third_party/utlist/utlist.h"

static void mixer_control_get_name_and_index(const char* name,
                                             char* mixer_control_name,
                                             int* mixer_control_index) {
  size_t name_size = strlen(name);
  size_t mixer_control_name_len = 0;
  char* pos = strchr(name, ',');
  if (!pos) {
    mixer_control_name_len = name_size;
    *mixer_control_index = 0;
  } else {
    mixer_control_name_len = (size_t)(pos - name);
    *mixer_control_index = atoi(pos + 1);
  }
  strncpy(mixer_control_name, name, mixer_control_name_len);

  // clean up remaining byte
  mixer_control_name += mixer_control_name_len;
  memset(mixer_control_name, 0, name_size - mixer_control_name_len);
}

struct mixer_name* mixer_name_add(struct mixer_name* names,
                                  const char* name,
                                  enum CRAS_STREAM_DIRECTION dir,
                                  mixer_name_type type) {
  struct mixer_name* m_name;
  int mixer_control_index;
  char* mixer_control_name;
  if (!name) {
    return names;
  }

  m_name = (struct mixer_name*)calloc(1, sizeof(struct mixer_name));
  if (!m_name) {
    return names;
  }
  mixer_control_name = strdup(name);
  if (!mixer_control_name) {
    free(m_name);
    return names;
  }
  mixer_control_get_name_and_index(name, mixer_control_name,
                                   &mixer_control_index);

  m_name->name = mixer_control_name;
  m_name->index = mixer_control_index;
  m_name->dir = dir;
  m_name->type = type;

  DL_APPEND(names, m_name);
  return names;
}

struct mixer_name* mixer_name_add_array(struct mixer_name* names,
                                        const char* const* name_array,
                                        size_t name_array_size,
                                        enum CRAS_STREAM_DIRECTION dir,
                                        mixer_name_type type) {
  size_t i;
  for (i = 0; i < name_array_size; i++) {
    names = mixer_name_add(names, name_array[i], dir, type);
  }
  return names;
}

void mixer_name_free(struct mixer_name* names) {
  struct mixer_name* m_name;
  DL_FOREACH (names, m_name) {
    DL_DELETE(names, m_name);
    free((void*)m_name->name);
    free(m_name);
  }
}

struct mixer_name* mixer_name_find(struct mixer_name* names,
                                   const char* name,
                                   enum CRAS_STREAM_DIRECTION dir,
                                   mixer_name_type type) {
  if (!name && type == MIXER_NAME_UNDEFINED) {
    return NULL;
  }

  struct mixer_name* m_name;
  DL_FOREACH (names, m_name) {
    // Match the direction.
    if (dir != m_name->dir) {
      continue;
    }
    // Match the type unless the type is UNDEFINED.
    if (type != MIXER_NAME_UNDEFINED && type != m_name->type) {
      continue;
    }
    /* Match the name if it is non-NULL, or return the first
     * item with the correct type when the name is not defined. */
    if ((type != MIXER_NAME_UNDEFINED && !name) ||
        (name && !strcmp(m_name->name, name))) {
      return m_name;
    }
  }
  return NULL;
}

static const char* mixer_name_type_str(enum CRAS_STREAM_DIRECTION dir,
                                       mixer_name_type type) {
  switch (dir) {
    case CRAS_STREAM_OUTPUT:
      switch (type) {
        case MIXER_NAME_VOLUME:
          return "output volume";
        case MIXER_NAME_MAIN_VOLUME:
          return "main volume";
        case MIXER_NAME_UNDEFINED:
          break;
      }
      break;
    case CRAS_STREAM_INPUT:
      switch (type) {
        case MIXER_NAME_VOLUME:
          return "input volume";
        case MIXER_NAME_MAIN_VOLUME:
          return "main capture";
        case MIXER_NAME_UNDEFINED:
          break;
      }
      break;
    case CRAS_STREAM_UNDEFINED:
    case CRAS_STREAM_POST_MIX_PRE_DSP:
    case CRAS_NUM_DIRECTIONS:
      break;
  }
  return "undefined";
}

void mixer_name_dump(struct mixer_name* names, const char* message) {
  struct mixer_name* m_name;

  if (!names) {
    syslog(LOG_DEBUG, "%s: empty", message);
    return;
  }

  syslog(LOG_DEBUG, "%s:", message);
  DL_FOREACH (names, m_name) {
    const char* type_str = mixer_name_type_str(m_name->dir, m_name->type);
    syslog(LOG_DEBUG, "    %s %s", m_name->name, type_str);
  }
}
