/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_alsa_config.h"

#include <alsa/asoundlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cras/src/common/blob_wrapper.h"
#include "cras/src/server/cras_alsa_card.h"
#include "third_party/strlcpy/strlcpy.h"
#include "third_party/utlist/utlist.h"

#define MAX_ALSA_CTL_ELEM_NAME_LENGTH 100

// ctl_elem is a handler for config read/write to given config control name.
struct ctl_elem {
  // ALSA sound card CTL handle.
  snd_ctl_t* handle;
  // Properties for ALSA CTL element for the config control.
  snd_ctl_elem_id_t* id;
  snd_ctl_elem_info_t* info;
  snd_ctl_elem_value_t* value;

  // The pointer of applied blob wrapper for byte-typed configuration.
  struct blob_wrapper* bw;
  // If true, the preliminary read is required for writing bytes.
  bool bw_update_needed;

  // The mixer control name.
  char name[MAX_ALSA_CTL_ELEM_NAME_LENGTH];
  // The sound card index it belongs.
  size_t card_index;

  struct ctl_elem *next, *prev;
};

struct ctl_elem* connected_ctl_elems;

static void ctl_elem_free(struct ctl_elem* ctl_elem) {
  if (!ctl_elem) {
    return;
  }

  snd_ctl_elem_value_free(ctl_elem->value);
  snd_ctl_elem_id_free(ctl_elem->id);
  snd_ctl_elem_info_free(ctl_elem->info);
  if (ctl_elem->handle != NULL) {
    snd_ctl_close(ctl_elem->handle);
  }

  free(ctl_elem->bw);
  free(ctl_elem);
}

static struct ctl_elem* ctl_elem_allocate() {
  struct ctl_elem* ctl_elem;
  int rc;

  ctl_elem = (struct ctl_elem*)calloc(1, sizeof(*ctl_elem));
  if (!ctl_elem) {
    return NULL;
  }

  /* Allocate buffers for pointers info, id, and value. */
  rc = snd_ctl_elem_info_malloc(&ctl_elem->info);
  if (rc) {
    syslog(LOG_ERR, "ctl_elem_allocate: Could not malloc elem_info: %d", rc);
    goto error;
  }

  rc = snd_ctl_elem_id_malloc(&ctl_elem->id);
  if (rc) {
    syslog(LOG_ERR, "ctl_elem_allocate: Could not malloc elem_id: %d", rc);
    goto error;
  }

  rc = snd_ctl_elem_value_malloc(&ctl_elem->value);
  if (rc) {
    syslog(LOG_ERR, "ctl_elem_allocate: Could not malloc elem_value: %d", rc);
    goto error;
  }

  return ctl_elem;

error:
  ctl_elem_free(ctl_elem);
  return NULL;
}

static int ctl_elem_create(size_t card_index,
                           const char* elem_name,
                           struct ctl_elem** ctl_elem_p) {
  struct ctl_elem* ctl_elem;
  alsa_card_name_t card_name = cras_alsa_card_get_name(card_index);
  int rc;

  ctl_elem = ctl_elem_allocate();
  if (!ctl_elem) {
    syslog(LOG_ERR, "ctl_elem_create: Failed allocating memory");
    return -ENOMEM;
  }

  rc = snd_ctl_open(&ctl_elem->handle, card_name.str, SND_CTL_NONBLOCK);
  if (rc) {
    syslog(LOG_ERR, "ctl_elem_create: Failed opening card %s.", card_name.str);
    goto error;
  }

  /* Parse elem id from the ascii control name. */
  rc = snd_ctl_ascii_elem_id_parse(ctl_elem->id, elem_name);
  if (rc) {
    syslog(LOG_ERR, "ctl_elem_create: Failed parsing id from %s.", elem_name);
    goto error;
  }

  /* Get element info from id. */
  snd_ctl_elem_info_set_id(ctl_elem->info, ctl_elem->id);
  rc = snd_ctl_elem_info(ctl_elem->handle, ctl_elem->info);
  if (rc) {
    /* snd_ctl_elem_info() returns -ENOENT when the control name is not found on
     * this card. */
    goto error;
  }

  *ctl_elem_p = ctl_elem;
  return 0;

error:
  ctl_elem_free(ctl_elem);
  return rc;
}

/* Create ctl_elem for the given control name. It will iterate over ALSA cards
 * until the target control is detected on one of them and ctl_elem is created
 * successfully. Or return with error code while reaching the iteration end.
 *
 * This should be used under the premise that the control name is unique over
 * all ALSA cards.
 */
static int ctl_elem_create_for_control_name(const char* name,
                                            struct ctl_elem** ctl_elem_p) {
  int card_index = -1;
  char elem_name[MAX_ALSA_CTL_ELEM_NAME_LENGTH];
  int rc;

  rc = snprintf(elem_name, MAX_ALSA_CTL_ELEM_NAME_LENGTH, "name='%s'", name);
  if (rc >= MAX_ALSA_CTL_ELEM_NAME_LENGTH) {
    syslog(LOG_ERR,
           "ctl_elem_create: elem_name truncated due to length exceeded");
    return -ERANGE;
  }

  /* snd_card_next() takes the index and sets it to the next index. To iterate
   * over cards, set the initial index to -1 and make repetitively calls until
   * the next index gets -1, e.g. -1 -> 0 -> 1 -> 2 -> -1.
   */
  rc = -EIO; /* error code for no available card */
  while (snd_card_next(&card_index) == 0 && card_index >= 0) {
    rc = ctl_elem_create(card_index, elem_name, ctl_elem_p);
    if (rc) {
      /* The error code -ENOENT is received when the control name is not
       * detected on the iterating card (reported by snd_crl_elem_info()), which
       * is our intention to try on the next card.
       *
       * Instead, other error codes might indicate the real problems once
       * received so they should not be waived here. (Now we only report them in
       * logs, may consider making assertions in the future.)
       */
      if (rc == -ENOENT) {
        syslog(LOG_DEBUG, "ctl_elem_create: %s not found on card %d", name,
               card_index);
      } else {
        syslog(LOG_ERR,
               "ctl_elem_create: Unexpected error code %d from creating %s on "
               "card %d",
               rc, elem_name, card_index);
      }
      continue;
    }
    syslog(LOG_DEBUG, "ctl_elem_create: %s found on card %d", name, card_index);

    /* Fill the matched keys, i.e. name and matched card index. */
    strlcpy((*ctl_elem_p)->name, name, MAX_ALSA_CTL_ELEM_NAME_LENGTH);
    (*ctl_elem_p)->card_index = card_index;
    return 0;
  }
  return rc;
}

static bool ctl_elem_is_bytes_type(struct ctl_elem* ctl_elem) {
  return snd_ctl_elem_info_get_type(ctl_elem->info) == SND_CTL_ELEM_TYPE_BYTES;
}

static bool ctl_elem_is_switch_type(struct ctl_elem* ctl_elem) {
  return snd_ctl_elem_info_get_type(ctl_elem->info) ==
         SND_CTL_ELEM_TYPE_BOOLEAN;
}

static int get_ctl_elem_by_name(const char* name,
                                struct ctl_elem** ctl_elem_p) {
  struct ctl_elem* ctl_elem;
  struct blob_wrapper* wrapper;
  int rc;

  /* Find control name in the list of connected control elements. */
  DL_FOREACH (connected_ctl_elems, ctl_elem) {
    if (!strcmp(ctl_elem->name, name)) {
      *ctl_elem_p = ctl_elem;
      return 0;
    }
  }

  /* Create the control element (and connect) if not matched in list. */
  rc = ctl_elem_create_for_control_name(name, &ctl_elem);
  if (rc) {
    syslog(LOG_WARNING, "get_ctl_elem_by_name: %s is not detected ", name);
    return rc;
  }

  /* Create the blob wrapper for bytes type. */
  if (ctl_elem_is_bytes_type(ctl_elem)) {
    /* Use SOF-typed blob wrapper by default.
     * TODO(b/292231234): revisit while more types are required for support.
     */
    wrapper = sof_blob_wrapper_create();
    if (!wrapper) {
      syslog(LOG_ERR, "get_ctl_elem_by_name: Failed creating wrapper");
      ctl_elem_free(ctl_elem);
      return -ENOMEM;
    }
    ctl_elem->bw = wrapper;
    ctl_elem->bw_update_needed = true;
  }

  /* Append the control element to the list. */
  DL_APPEND(connected_ctl_elems, ctl_elem);

  *ctl_elem_p = ctl_elem;
  return 0;
}

static int ctl_elem_get_blob_data(struct ctl_elem* ctl_elem,
                                  uint8_t* buf,
                                  size_t buf_size) {
  struct blob_wrapper* wrapper = ctl_elem->bw;
  uint8_t* read_buf;
  size_t read_buf_size;
  int rc;

  rc = blob_wrapper_get_wrapped_size(wrapper, buf, buf_size);
  if (rc < 0) {
    syslog(LOG_ERR, "ctl_elem_get_blob_data: Failed getting wrapped size");
    return rc;
  }

  read_buf_size = rc;
  read_buf = (uint8_t*)calloc(1, read_buf_size);
  if (!read_buf) {
    syslog(LOG_ERR,
           "cras_alsa_config_get_tlv_bytes_data: Failed allocating buffer");
    return -ENOMEM;
  }

  /* TLV read buffer needs to be formatted as the wrapped blob. */
  rc = blob_wrapper_wrap(wrapper, read_buf, read_buf_size, buf, buf_size);
  if (rc < 0) {
    syslog(LOG_ERR,
           "ctl_elem_get_blob_data: Failed wrapping blob for config read");
    goto exit;
  }

  rc = snd_ctl_elem_tlv_read(ctl_elem->handle, ctl_elem->id,
                             (uint32_t*)read_buf, read_buf_size);
  if (rc < 0) {
    syslog(LOG_ERR, "ctl_elem_get_blob_data: Failed TLV read");
    goto exit;
  }

  rc = blob_wrapper_unwrap(wrapper, buf, buf_size, read_buf, read_buf_size);
  if (rc < 0) {
    syslog(LOG_ERR, "ctl_elem_get_blob_data: Failed unwrapping blob");
    goto exit;
  }

  ctl_elem->bw_update_needed = false;

exit:
  free(read_buf);
  return rc;
}

static int ctl_elem_read_tlv_bytes_internal(struct ctl_elem* ctl_elem) {
  uint8_t* buf;
  size_t buf_size;
  int rc;

  /* Check if the control is readable.
   * The function returns 1 if readable; 0 otherwise.
   */
  if (!snd_ctl_elem_info_is_tlv_readable(ctl_elem->info)) {
    syslog(LOG_ERR, "ctl_elem_read_tlv_bytes_internal: Not a readable control");
    return -EACCES;
  }

  /* Get the biggest possible blob size (unwrapped) from info. */
  buf_size = snd_ctl_elem_info_get_count(ctl_elem->info);

  buf = (uint8_t*)calloc(1, buf_size);
  if (!buf) {
    syslog(LOG_ERR,
           "ctl_elem_read_tlv_bytes_internal: Could not allocate buffer");
    return -ENOMEM;
  }

  rc = ctl_elem_get_blob_data(ctl_elem, buf, buf_size);
  if (rc < 0) {
    syslog(LOG_ERR, "ctl_elem_read_tlv_bytes_internal: Failed TLV read");
  }

  free(buf);
  return rc;
}

/*
 * Exported Interface.
 */

int cras_alsa_config_probe(const char* name) {
  struct ctl_elem* ctl_elem;
  return get_ctl_elem_by_name(name, &ctl_elem);
}

int cras_alsa_config_set_switch(const char* name, bool enabled) {
  struct ctl_elem* ctl_elem;
  int rc;

  syslog(LOG_DEBUG, "cras_alsa_config: Set switch %s to %d", name, enabled);

  rc = get_ctl_elem_by_name(name, &ctl_elem);
  if (rc) {
    syslog(LOG_ERR, "cras_alsa_config_set_switch: Failed creating ctl_elem");
    return rc;
  }

  /* Check if the control type has boolean support. */
  if (!ctl_elem_is_switch_type(ctl_elem)) {
    syslog(LOG_ERR,
           "cras_alsa_config_set_switch: Control type has no boolean support");
    return -EINVAL;
  }

  /* Set id and read from control for handle value. */
  snd_ctl_elem_value_set_id(ctl_elem->value, ctl_elem->id);
  rc = snd_ctl_elem_read(ctl_elem->handle, ctl_elem->value);
  if (rc) {
    syslog(LOG_ERR,
           "cras_alsa_config_set_switch: Failed to read control value");
    return rc;
  }

  /* Set switch boolean to handle value. */
  snd_ctl_elem_value_set_boolean(ctl_elem->value, 0, enabled);

  /* Write value to control. */
  rc = snd_ctl_elem_write(ctl_elem->handle, ctl_elem->value);
  if (rc < 0) {
    syslog(LOG_ERR,
           "cras_alsa_config_set_switch: Failed to write control value");
    return rc;
  }

  return 0;
}

int cras_alsa_config_get_switch(const char* name, bool* enabled) {
  struct ctl_elem* ctl_elem;
  int rc;

  syslog(LOG_DEBUG, "cras_alsa_config: Get switch %s", name);

  rc = get_ctl_elem_by_name(name, &ctl_elem);
  if (rc) {
    syslog(LOG_ERR, "cras_alsa_config_get_switch: Failed creating ctl_elem");
    return rc;
  }

  /* Check if the control type has boolean support. */
  if (!ctl_elem_is_switch_type(ctl_elem)) {
    syslog(LOG_ERR,
           "cras_alsa_config_get_switch: Control type has no boolean support");
    return -EINVAL;
  }

  /* Set id and read from control for handle value. */
  snd_ctl_elem_value_set_id(ctl_elem->value, ctl_elem->id);
  rc = snd_ctl_elem_read(ctl_elem->handle, ctl_elem->value);
  if (rc) {
    syslog(LOG_ERR,
           "cras_alsa_config_get_switch: Failed to read control value");
    return rc;
  }

  /* Get switch value. */
  *enabled = !!snd_ctl_elem_value_get_boolean(ctl_elem->value, 0);
  syslog(LOG_DEBUG, "cras_alsa_config: Got value %d", *enabled);
  return 0;
}

int cras_alsa_config_set_tlv_bytes(const char* name,
                                   const uint8_t* blob,
                                   size_t blob_size) {
  struct ctl_elem* ctl_elem;
  struct blob_wrapper* wrapper;
  uint8_t* buf = NULL;
  size_t buf_size;
  int rc;

  syslog(LOG_DEBUG, "cras_alsa_config: Set %s with blob size %zu", name,
         blob_size);

  rc = get_ctl_elem_by_name(name, &ctl_elem);
  if (rc) {
    syslog(LOG_ERR, "cras_alsa_config_set_tlv_bytes: Failed creating ctl_elem");
    return rc;
  }

  /* Check if the control type has bytes support. */
  if (!ctl_elem_is_bytes_type(ctl_elem)) {
    syslog(LOG_ERR,
           "cras_alsa_config_set_tlv_bytes: Control type has no bytes support");
    return -EINVAL;
  }

  /* Check if the control is writable.
   * The function returns 1 if writable; 0 otherwise.
   */
  if (!snd_ctl_elem_info_is_tlv_writable(ctl_elem->info)) {
    syslog(LOG_ERR, "cras_alsa_config_set_tlv_bytes: Not a writable control");
    return -EACCES;
  }

  /* Read the control configuration before write if needed. */
  if (ctl_elem->bw_update_needed) {
    rc = ctl_elem_read_tlv_bytes_internal(ctl_elem);
    if (rc < 0) {
      syslog(
          LOG_WARNING,
          "cras_alsa_config_set_tlv_bytes: Failed at preliminary read trial");
    }
  }

  wrapper = ctl_elem->bw;

  rc = blob_wrapper_get_wrapped_size(wrapper, blob, blob_size);
  if (rc < 0) {
    syslog(LOG_ERR,
           "cras_alsa_config_set_tlv_bytes: Failed getting wrapped size");
    return rc;
  }

  buf_size = rc;
  buf = (uint8_t*)calloc(1, buf_size);
  if (!buf) {
    syslog(LOG_ERR, "cras_alsa_config_set_tlv_bytes: Failed allocating buffer");
    rc = -ENOMEM;
    goto exit;
  }

  rc = blob_wrapper_wrap(wrapper, buf, buf_size, blob, blob_size);
  if (rc < 0) {
    syslog(LOG_ERR, "cras_alsa_config_set_tlv_bytes: Failed wrapping blob");
    goto exit;
  }

  /* Write TLV buffer to control. */
  rc = snd_ctl_elem_tlv_write(ctl_elem->handle, ctl_elem->id, (uint32_t*)buf);
  if (rc < 0) {
    syslog(LOG_ERR, "cras_alsa_config_set_tlv_bytes: Failed TLV write");
    goto exit;
  }
  rc = 0;

exit:
  free(buf);
  return rc;
}

int cras_alsa_config_get_tlv_bytes_maxcount(const char* name) {
  struct ctl_elem* ctl_elem;
  int rc;

  syslog(LOG_DEBUG, "cras_alsa_config: Get bytes count for control %s", name);

  rc = get_ctl_elem_by_name(name, &ctl_elem);
  if (rc) {
    syslog(LOG_ERR,
           "cras_alsa_config_get_tlv_bytes_maxcount: Failed creating ctl_elem");
    return rc;
  }

  /* Check if the control type has bytes support. */
  if (!ctl_elem_is_bytes_type(ctl_elem)) {
    syslog(LOG_ERR,
           "cras_alsa_config_get_tlv_bytes_maxcount: Control type has no bytes "
           "support");
    return -EINVAL;
  }

  /* Get the biggest possible blob size (unwrapped) from info. */
  rc = (int)snd_ctl_elem_info_get_count(ctl_elem->info);
  return rc;
}

int cras_alsa_config_get_tlv_bytes_data(const char* name,
                                        uint8_t* buf,
                                        size_t buf_size) {
  struct ctl_elem* ctl_elem;
  int rc;

  syslog(LOG_DEBUG, "cras_alsa_config: Get %s blob data", name);

  if (!buf) {
    syslog(
        LOG_ERR,
        "cras_alsa_config_get_tlv_bytes_data: Input buffer is not allocated");
    return -ENOMEM;
  }

  rc = get_ctl_elem_by_name(name, &ctl_elem);
  if (rc) {
    syslog(LOG_ERR,
           "cras_alsa_config_get_tlv_bytes_data: Failed creating ctl_elem");
    return rc;
  }

  /* Check if the control type has bytes support. */
  if (!ctl_elem_is_bytes_type(ctl_elem)) {
    syslog(LOG_ERR,
           "cras_alsa_config_get_tlv_bytes_data: Control type has no bytes "
           "support");
    return -EINVAL;
  }

  /* Check if the control is readable.
   * The function returns 1 if readable; 0 otherwise.
   */
  if (!snd_ctl_elem_info_is_tlv_readable(ctl_elem->info)) {
    syslog(LOG_ERR,
           "cras_alsa_config_get_tlv_bytes_data: not a readable control");
    return -EACCES;
  }

  rc = ctl_elem_get_blob_data(ctl_elem, buf, buf_size);
  if (rc < 0) {
    syslog(LOG_ERR,
           "cras_alsa_config_get_tlv_bytes_data: Failed to get blob data");
  }

  return rc;
}

void cras_alsa_config_release_controls_on_card(uint32_t card_index) {
  struct ctl_elem* ctl_elem;

  DL_FOREACH (connected_ctl_elems, ctl_elem) {
    if (ctl_elem->card_index == card_index) {
      DL_DELETE(connected_ctl_elems, ctl_elem);
      ctl_elem_free(ctl_elem);
    }
  }
}
