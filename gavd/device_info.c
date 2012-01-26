/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <regex.h>

#define __USE_UNIX98            /* For pthread_mutexattr_settype et al. */
#include <pthread.h>

#include "thread_management.h"
#include "initialization.h"
#include "verbose.h"
#include "chrome_card_info_fifo.h"
#include "device_info.h"

static char const * const device_kinds[DK_NUM_KINDS] = {
#define DK(_id_, _text_) _text_,
        DEVICE_KIND_LIST
#undef DK
};

static char const * const device_directions[D_NUM_DIRECTION] = {
#define DD(_id_, _text_) _text_,
    DEVICE_DIRECTION_LIST
#undef DD
};

typedef struct alsa_device_t {
    device_t    header;
    unsigned    card_number;
    unsigned    device_number;
    const char *sysname;
} alsa_device_t;

typedef struct devices_t {
    device_t        *device_list; /* list w/ dummy head */
    pthread_mutex_t  device_mutex;
} devices_t;

static devices_t devices;

/* DEVICES_ITERATE: Iterate over the set of devices
 *
 *   _v   : This identifier is used to create a local variable which
 *          should be used by '_code' to access the current device_t.
 *
 *   _code: A block of code which can manipulate '_v'.
 *
 *  This macro iterates over every element in the set of devices.
 *  '_code' can operate upon '_v', including removing '_v' from the
 *  set.  '_code' should not manipulate the list other than possibly
 *  removing '_v'.
 *
 *
 *
 *
 */
#define DEVICES_ITERATE(_v, _code)                              \
    do {                                                        \
        device_t *_v = devices.device_list->next;               \
        while (_v != devices.device_list) {                     \
            device_t *__n = _v->next;                           \
            _code                                               \
            _v = __n;                                           \
        }                                                       \
    } while (0)

#define LOCK()                                                          \
    do {                                                                \
        verbose_log(8, LOG_INFO, "%s: lock devices", __FUNCTION__);     \
        devices_lock();                                                 \
    } while (0)

#define UNLOCK()                                                        \
    do {                                                                \
        verbose_log(8, LOG_INFO, "%s: unlock devices", __FUNCTION__);   \
        devices_unlock();                                               \
    } while (0)


static void devices_lock(void)
{
    int result;

    result         = pthread_mutex_lock(&devices.device_mutex);
    assert(result != EDEADLK);  /* Locked by current thread already. */
    assert(result == 0);
}

static void devices_unlock(void)
{
    int result;

    result         = pthread_mutex_unlock(&devices.device_mutex);
    assert(result != EPERM);  /* Not owned by current thread. */
    assert(result == 0);
}

static device_t *next_device_with_direction(device_t *d)
{
    device_t *n = d->next;

    /* This will return the input, 'd', if there is only one device on
     * the list; this is ok.  In other words, the dummy head node is
     * never returned.
     *
     * This code is specifically written to never use the the dummy
     * head's data in comparisons.
     */
    while (n != d) {
        if (n == devices.device_list) {
            n = n->next;
        }
        if (n->direction == d->direction) {
            break;
        }
        n = n->next;
    }
    return n;
}

static void add_device(device_t *d)
{
    d->next                         = devices.device_list->next;
    d->prev                         = devices.device_list->next->prev;
    devices.device_list->next->prev = d;
    devices.device_list->next       = d;
}

static void remove_device(device_t *d)
{
    assert(d->kind != DK_NUM_KINDS); /* Do not remove dummy head. */
    d->prev->next = d->next;
    d->next->prev = d->prev;
    d->next       = NULL;
    d->prev       = NULL;
}

static void free_device(device_t *dev)
{
    free(dev);
}

static void free_alsa_device(alsa_device_t *dev)
{
    alsa_device_t *d = (alsa_device_t *)dev;
    char          *s = (char *)d->sysname;
    d->sysname       = NULL;
    free(s);
    free_device((device_t *)dev);
}

static void log_device_info(const device_t *dev,
                            const char     *action)
{
    switch (dev->kind) {
    case DK_ALSA: {
        const alsa_device_t *ad = (alsa_device_t *)dev;
        verbose_log(5, LOG_INFO,
                    "%s: [%s, %s]: '%s' "
                    "PIA: %.1u%.1u%.1u",
                    __FUNCTION__, device_kinds[DK_ALSA], action,
                    ad->sysname, dev->primary, dev->internal, dev->active);
        break;
    }

    case DK_NUM_KINDS:
    default:
        assert(0);
    }
}

static void send_card_added_message(const device_t *d)
{
    log_device_info(d, "add");
    switch (d->kind) {
    case DK_ALSA: {
        alsa_device_t *ad = (alsa_device_t *)d;
        chrome_card_added(ad->sysname, ad->card_number, ad->device_number);
        break;
    }

    case DK_NUM_KINDS:
    default:
        assert(0);
    }
}

static void send_card_removed_message(const device_t *d)
{
    log_device_info(d, "rem");
    switch (d->kind) {
    case DK_ALSA: {
        alsa_device_t *ad = (alsa_device_t *)d;
        chrome_card_removed(ad->sysname, ad->card_number, ad->device_number);
        break;
    }

    case DK_NUM_KINDS:
    default:
        assert(0);
    }
}

static void send_card_changed_message(const device_t *d)
{
    log_device_info(d, "chg");
    switch (d->kind) {
    case DK_ALSA: {
        alsa_device_t *ad = (alsa_device_t *)d;
        chrome_card_changed(ad->sysname, ad->card_number, ad->device_number,
                            d->active, d->internal, d->primary);
        break;
    }

    case DK_NUM_KINDS:
    default:
        assert(0);
    }
}

static void set_active_device_with_direction(device_t *d)
{
    /* Sets 'd' to be the active device for its 'direction'.  All
     * other devices of the same 'direction' will be set to inactive.
     */
    DEVICES_ITERATE(dl, {
            if (dl->active && dl->direction == d->direction) {
                dl->active = 0;
                send_card_changed_message(dl);
                break;          /* invariant: Only one active device. */
            }
        });
    d->active = 1;
    send_card_changed_message(d);
}

static void set_next_device_active_with_direction(device_t *d)
{
    device_t *n = next_device_with_direction(d);

    /* All devices are put onto the list of devices in reverse order.
     * The most recently inserted device will be first in the list.
     * The next most recently inserted device will be second on the
     * list, etc.  We want to move the 'active' flag to the next
     * device when the active device is removed.
     *
     * If there is only one device for 'direction' in the system, then
     * no device will be marked as active.
     */
    assert(d->active);
    d->active = 0;
    send_card_changed_message(d);
    if (n != d) {
        n->active = 1;
        send_card_changed_message(n);
    }
}

static void set_next_device_primary_with_direction(device_t *d)
{
    device_t *n = next_device_with_direction(d);

    /* All devices are put onto the list of devices in reverse order.
     * The most recently inserted device will be first in the list.
     * The next most recently inserted device will be second on the
     * list, etc.  We want to move the 'primary' flag to the next
     * device when the primary device is removed.
     *
     * If there is only one device for 'direction' in the system, then
     * no device will be marked as the primary.
     */
    assert(d->primary);
    d->primary = 0;
    send_card_changed_message(d);
    if (n != d) {
        n->primary = 1;
        send_card_changed_message(n);
    }
}

static device_t *find_device_with_direction(direction_t direction)
{
    DEVICES_ITERATE(d, {
            if (d->direction == direction) {
                return d;
            }
        });
    return NULL;
}

static device_t *find_internal_device_with_direction(direction_t direction)
{
    DEVICES_ITERATE(d, {
            if (d->internal && d->direction == direction) {
                return d;
            }
        });
    return NULL;
}

/* set_primary_device: Sets a device with 'direction' to be the primary.
 *
 *  This function sets a device to be the primary (aka 'default')
 *  device.
 *
 *  An internal device is preferentially used, but if no suitable
 *  device can be found, an external device will be used.  If no
 *  suitable device is found, there will be no default for
 *  'direction'.
 */
static void set_primary_device(direction_t direction)
{
    device_t *d;

    d = find_internal_device_with_direction(direction);
    if (d == NULL) {
        d = find_device_with_direction(direction);
    }
    if (d != NULL) {
        d->primary = 1;
        send_card_changed_message(d);
    }
}

/* device_set_primary_playback_and_capture: Set up primary devices.
 *
 * A device needs to be marked as 'primary' device for 'capture' and
 * 'playback'; this is used as a fallback device when there are no
 * other devices present in the system, or when a 'reset to default'
 * is used.
 *
 * First, an internal device is tried.  If it cannot be found, a
 * non-internal device is attempted.  If a device is found, it is
 * marked as the primary device.  If no device can be found, there
 * will be no primary device.
 *
 * If there is no primary device, there never will be a primary
 * device.  In the case of selecting the 'primary' device when there
 * is no such device, no input or output will occur.  It's equivalent
 * to using /dev/null.
 *
 * If the device selected as the primary happens to be an external
 * device, and it is removed, the 'primary' flag will move to the next
 * device with the same 'direction'.
 */
void device_set_primary_playback_and_capture(void)
{
    LOCK();
    set_primary_device(D_PLAYBACK);
    set_primary_device(D_CAPTURE);
    UNLOCK();
}

static alsa_device_t *find_alsa_device(const char *sysname,
                                       unsigned    card_number,
                                       unsigned    device_number)
{
    /* invariant: device list is locked */
    assert(sysname != NULL);
    DEVICES_ITERATE(hdr, {
            if (hdr->kind == DK_ALSA) {
                alsa_device_t *d = (alsa_device_t *)hdr;
                if (strcmp(d->sysname, sysname) == 0 &&
                    d->card_number == card_number    &&
                    d->device_number == device_number) {
                    return d;
                }
            }
        });
    return NULL;
}

static alsa_device_t *allocate_alsa_device(const char  *sysname,
                                           unsigned     internal,
                                           unsigned     card,
                                           unsigned     device,
                                           direction_t  direction)
{
    /* Allocate a device.  If the device cannot be allocated, then it
     * will be ignored by the sound system.  This is most likely due
     * to out-of-memory conditions; removing the device and
     * re-inserting it at a later time might rectify the problem.
     */
    alsa_device_t *p = calloc(1, sizeof(alsa_device_t));

    assert(sysname != NULL);    /* precondition */
    if (p) {
        p->sysname = strdup(sysname);
        if (p->sysname) {
            p->header.kind      = DK_ALSA;
            p->header.primary   = 0;
            p->header.internal  = internal;
            p->header.direction = direction;
            p->header.active    = 0;
            p->card_number      = card;
            p->device_number    = device;
        } else {
            free(p);
            p = NULL;
        }
    }
    if (p == NULL) {
        verbose_log(0, LOG_WARNING, "%s: out of memory: '%s' ignored",
                    __FUNCTION__, sysname);
    }
    return p;
}

/* device_find_alsa: Find an Alsa device
 *
 *   Searches the list of attached devices for an Alsa device which
 *   matches all the arguments.
 *
 *   If such a device is found, it is returned.
 *   If no such device is found, NULL is returned.
 */
static device_t *device_find_alsa(unsigned        card,
                                  unsigned        device,
                                  direction_t     direction)
{
    DEVICES_ITERATE(dev, {
            if (dev->kind == DK_ALSA) {
                alsa_device_t *ad = (alsa_device_t *)dev;
                if (ad->card_number   == card   &&
                    ad->device_number == device &&
                    dev->direction    == direction) {
                    return dev;
                }
            }
        });
    return NULL;
}

void device_add_alsa(const char     *sysname,
                     unsigned        internal,
                     unsigned        card,
                     unsigned        device,
                     direction_t     direction)
{
    LOCK();
    /* If this device already exists in the list of devices, ignore
     * the request to add it.
     *
     * This can happen because we start to enumerate after beginning
     * to listen on the udev connection.  A device may appear in the
     * enumeration list, and also appear on the device which reports
     * udev events.
     */
    if (device_find_alsa(card, device, direction) == NULL) {
        alsa_device_t *p = allocate_alsa_device(sysname, internal, card,
                                                device, direction);
        if (p) {
            add_device(&p->header);
            send_card_added_message(&p->header);
            set_active_device_with_direction(&p->header);
        }
    }
    UNLOCK();
}

void device_remove_alsa(const char     *sysname,
                        unsigned        card,
                        unsigned        device)
{
    alsa_device_t *d;

    LOCK();
    d = find_alsa_device(sysname, card, device);

    if (d) {
        if (d->header.active) {
            set_next_device_active_with_direction(&d->header);
        }
        if (d->header.primary) {
            set_next_device_primary_with_direction(&d->header);
        }
        send_card_removed_message(&d->header);
        remove_device(&d->header);
        free_alsa_device(d);
    }
    UNLOCK();
}

static void initialize(void)
{
    device_t            *head = calloc(1, sizeof(device_t));
    pthread_mutexattr_t  attr;

    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&devices.device_mutex, &attr);

    assert(head != NULL);
    head->next          = head;
    head->prev          = head;
    head->kind          = DK_NUM_KINDS;
    head->primary       = 0;
    head->internal      = 0;
    head->direction     = D_PLAYBACK;
    head->active        = 0;
    devices.device_list = head;
}


static void finalize(void)
{
    LOCK();
    DEVICES_ITERATE(d, {
            remove_device(d);
            switch (d->kind) {
            case DK_ALSA:
                free_alsa_device((alsa_device_t *)d);
                break;

            case DK_NUM_KINDS:
            default:
                assert(0);      /* Internal error. */
            }
        });
    free(devices.device_list); /* Release dummy head. */
    UNLOCK();
    pthread_mutex_destroy(&devices.device_mutex);
}

INITIALIZER("device info", initialize, finalize);
