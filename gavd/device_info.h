/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_DEVICE_INFO_H_)
#define _DEVICE_INFO_H_

#define DEVICE_SPEED_LIST                       \
    DS(SLOW, "up to 12 mb/s")                   \
    DS(FAST, "more than 12 mb/s")

#define DEVICE_DIRECTION_LIST                   \
    DD(PLAYBACK, "playback")                    \
    DD(CAPTURE,  "capture")

#define DEVICE_KIND_LIST                        \
    DK(ALSA,  "alsa")

typedef enum device_speed_t {
#define DS(_id_, _text_) DS_##_id_,
    DEVICE_SPEED_LIST
#undef DS
    D_NUM_SPEED
} device_speed_t;

typedef enum direction_t {
#define DD(_id_, _text_) D_##_id_,
    DEVICE_DIRECTION_LIST
#undef DD
    D_NUM_DIRECTION
} direction_t;

typedef enum device_kind_t {
#define DK(_id_, _text_) DK_##_id_,
    DEVICE_KIND_LIST
#undef DK
    DK_NUM_KINDS
} device_kind_t;

typedef struct device_t device_t;
struct device_t {
    device_t       *prev;
    device_t       *next;
    device_kind_t   kind;
    direction_t     direction;
    device_speed_t  speed;
    unsigned        active;
    unsigned        primary;
    unsigned        internal;
};

/* device_set_primary_playback_and_capture:
 *
 *   At startup, and after enumeration, scans list of devices and
 *   selects one playback & one capture device to be the 'primary'.
 */
void device_set_primary_playback_and_capture(void);

void device_add_alsa(const char     *sysname,
                     unsigned        internal,
                     unsigned        card,
                     unsigned        device,
                     device_speed_t  speed,
                     direction_t     direction);
void device_remove_alsa(const char     *sysname,
                        unsigned        card,
                        unsigned        device);
#endif
