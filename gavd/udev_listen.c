/* Copyright (c) 2011, 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 *
 *
 *
 */
#include <assert.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define __USE_UNIX98            /* For pthread_mutexattr_settype et al. */
#include <pthread.h>

#include "thread_management.h"
#include "initialization.h"
#include "verbose.h"
#include "udev_listen.h"
#include "chrome_card_info_fifo.h"
static unsigned is_action(const char *desired,
                          const char *actual) __attribute__((nonnull(1)));

typedef struct device_t device_t;
struct device_t {
    device_t *prev;
    device_t *next;
    char     *sysname;
    unsigned  num;              /* card number */
    unsigned  primary;          /* 0         -> not primary device
                                 * 1         -> primary device
                                 * otherwise -> undefined
                                 *
                                 * When no other device is available,
                                 * the primary device will be set to
                                 * be the 'active' device.
                                 *
                                 * The will always be one, and only
                                 * one, 'primary' device.  This will
                                 * be set on startup, and will be an
                                 * 'internal' device.
                                 */
    unsigned  active;           /* 0         -> not active
                                 * 1         -> active
                                 * otherwise -> undefined
                                 *
                                 * There will always be one, and only
                                 * one, 'active' device.  On gavd
                                 * startup, this will be an internal
                                 * device.
                                 */
    unsigned  internal;         /* 0         -> internal device
                                 * 1         -> external device
                                 * otherwise -> undefined
                                 */
};

typedef struct devices_t {
    device_t        *device_list; /* list w/ dummy head */
    pthread_mutex_t  device_mutex;
} devices_t;


static char const * const  subsystem = "sound";
static devices_t           devices;
static struct udev        *udev;

#define DEVICES_ITERATE(_v, _code)                              \
    do {                                                        \
        device_t *_v = devices.device_list->next;               \
        while (_v != devices.device_list) {                     \
            _code                                               \
            _v = _v->next; /* Do not use device_next(). */      \
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


static void log_device_info(const device_t *d, const char *action)
{
    verbose_log(5, LOG_INFO, "%s: [%s]: "
                "'%s'  num: %u  active: %u  internal: %u  primary: %u",
                __FUNCTION__, action,
                d->sysname, d->num, d->active, d->internal, d->primary);
}

static void send_card_added_message(const device_t *d)
{
    log_device_info(d, "add");
    chrome_card_added(d->sysname, d->num);
}

static void send_card_removed_message(const device_t *d)
{
    log_device_info(d, "rem");
    chrome_card_removed(d->sysname, d->num);
}

static void send_card_changed_message(const device_t *d)
{
    log_device_info(d, "chg");
    chrome_card_changed(d->sysname, d->num, d->active, d->internal, d->primary);
}

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

static unsigned get_card_number(const char *sysname)
{
    unsigned    r;
    int         n;

    assert(strncmp(sysname, "card", 4) == 0);
    n = sscanf(&sysname[4], "%u", &r);
    assert(n == 1);
    return r;
}

static device_t *allocate_device(const char *sysname,
                                 unsigned internal)
{
    /* Allocate a device.  If the device cannot be allocated, then it
     * will be ignored by the sound system.  This is most likely due
     * to out-of-memory conditions; removing the device and
     * re-inserting it at a later time might rectify the problem.
     */
    device_t *p = malloc(sizeof(device_t));

    assert(strncmp(sysname, "card", 4) == 0);
    p->next     = NULL;
    p->prev     = NULL;
    p->sysname  = strdup(sysname);
    p->num      = get_card_number(sysname);
    p->primary  = 0;
    p->active   = 0;
    p->internal = internal;
    return p;
}

static unsigned is_action(const char *desired,
                          const char *actual)
{
    return actual != NULL && strcmp(desired, actual) == 0;
}

static unsigned is_action_add(const char *action)
{
    return is_action("add", action);
}

static unsigned is_action_remove(const char *action)
{
    return is_action("remove", action);
}

static device_t *next_device(device_t *d)
{
    device_t *n = d->next;

    /* This will return the input, 'd', if there is only one device on
     * the list; this is ok.  In other words, the dummy head node is
     * never returned.
     */
    if (n == devices.device_list) {
        n = next_device(n);
    }
    return n;
}

static device_t *find_named_device(const char *sysname)
{
    DEVICES_ITERATE(d, {
            if (strcmp(d->sysname, sysname) == 0) {
                return d;
            }
        });
    return NULL;
}

static device_t *find_internal_device(void)
{
    DEVICES_ITERATE(d, {
            if (d->internal == 1) {
                return d;
            }
        });
    return NULL;
}

static void add_device(device_t *d)
{
    d->next                 = devices.device_list->next;
    d->prev                 = devices.device_list->next->prev;
    devices.device_list->next->prev = d;
    devices.device_list->next       = d;
}

static void remove_device(device_t *d)
{
    d->prev->next = d->next;
    d->next->prev = d->prev;
    d->next       = NULL;
    d->prev       = NULL;
}

static void free_device(device_t *d)
{
    char *s    = d->sysname;
    d->sysname = NULL;
    free(s);
    free(d);
}

static void set_active_device(device_t *d)
{
    /* Sets the device, 'd', to be the active device.  All other known
     * devices are set to be inactive.
     */
    DEVICES_ITERATE(dl, {
            if (dl->active) {
                dl->active = 0;
                send_card_changed_message(dl);
                break;          /* invariant: Only one active device. */
            }
        });
    d->active = 1;
    send_card_changed_message(d);
}

static void set_next_device_active(device_t *d)
{
    device_t *n = next_device(d);

    /* All devices are put onto the list of devices in reverse order.
     * The most recently inserted device will be first in the list.
     * The next most recently inserted device will be second on the
     * list, etc.  We want to move the 'active' flag to the next
     * device when the active device is removed.
     *
     * If there is only one device available, its active flag will be
     * disabled, and then re-enabled.  This ok, because it's expected
     * that the device will be removed, the devices with nothing set
     * as active.
     */
    assert(d->active);
    d->active = 0;
    if (n != d) {
        n->active = 1;
        send_card_changed_message(n);
    }
}

static void set_next_device_primary(device_t *d)
{
    device_t *n = next_device(d);

    /* All devices are put onto the list of devices in reverse order.
     * The most recently inserted device will be first in the list.
     * The next most recently inserted device will be second on the
     * list, etc.  We want to move the 'primary' flag to the next
     * device when the primary device is removed.
     *
     * If there is only one device available, its primary flag will be
     * disabled, and then re-enabled.  This ok, because it's expected
     * that the device will be removed, the devices with nothing set
     * as primary.
     */
    assert(d->primary);
    d->primary = 0;
    if (n != d) {
        n->primary = 1;
        send_card_changed_message(n);
    }
}

static void set_primary_and_active_device(void)
{
    /* A device needs to be marked as the 'primary' device, and a
     * device needs to be marked as the 'active' device; this is
     * intended to be the same device, but it is not necessary for it
     * to be so.
     *
     * On startup, choose an internal device, if present.  If none
     * exists, use the a non-internal device.  If no device can be
     * found, then there is no primary, and no active device.
     *
     * If there is no primary device, there never will be a primary
     * device; there should be no sound output in the case where the
     * primary device is selected.
     */
    device_t *d;

    LOCK();
    d = find_internal_device();
    if (d == NULL) {
        d = devices.device_list->next;
    }
    assert(d != NULL);
    if (d != devices.device_list) {
        d->primary = 1;
        d->active  = 1;
        send_card_changed_message(d);
    }
    UNLOCK();
}

/* is_device_card: Determine if a device is a 'card' proper.
 *
 *  If there is a better way of identifying a 'card' than through a
 *  string comparison, please implement it here.
 *
 * dev.sysname: When a card, will be of the form 'card0'.
 *
 * Result: 0 -> dev.sysname[0..3] is not a 'card'
 * Result: 1 -> dev.sysname[0..3] is a 'card'
 */
static unsigned is_device_card(struct udev_device  *dev,
                               const char         **sysname)
{
    const char    prefix[]   = "card";
    const size_t  prefix_len = sizeof(prefix) / sizeof(prefix[0]) - 1;

    *sysname = udev_device_get_sysname(dev);
    return (*sysname != NULL &&
            strncmp(*sysname, prefix, prefix_len) == 0);
}

static unsigned is_device_internal(struct udev_device *dev)
{
    const char *sysname;
    if (is_device_card(dev, &sysname)) {
        const char external[]      = "usb";
        struct udev_device *parent = udev_device_get_parent(dev);
        const char         *s      = udev_device_get_subsystem(parent);

        /* The parent node of a 'card' will either have a subsystem of
         * 'usb' (external device), or 'pci' (x86, internal device) or
         * 'platform' (arm, internal device).
         *
         *  Since the only external bus supported is 'usb', we can use
         *  this to determine if a card is internal or not.
         *
         *  If there is no subsystem, we will never treat it as an
         *  internal device.
         */
        return s != NULL && strcmp(s, external) != 0;
    } else {
        return 0;
    }
}

static void add_device_if_card(struct udev_device *dev)
{
    /* If the device, 'dev' is a card, add it to the set of devices
     * available for I/O.  Mark it as the active device.
     */
    const char *sysname;
    if (is_device_card(dev, &sysname)) {
        device_t   *d;

        assert(sysname != NULL);
        LOCK();
        d = allocate_device(sysname, is_device_internal(dev));
        if (d) {
            add_device(d);
            send_card_added_message(d);
            set_active_device(d);
        } else {
            verbose_log(0, LOG_WARNING,
                        "%s: Unable to allocate node for card '%s';"
                        "this card will be ignored",
                        __FUNCTION__, sysname);
        }
        UNLOCK();
    }
}

static void remove_device_if_card(struct udev_device *dev)
{
    const char *sysname;
    if (is_device_card(dev, &sysname)) {
       device_t   *d;

       assert(sysname != NULL);
        LOCK();
        d = find_named_device(sysname);

        /* If the card can't be found, it either wasn't able to put
         * into the set of cards when it was inserted, or no 'add'
         * event from udev was seen.  In either case, there is nothing
         * to do, so just ignore the whole issue.
         */
        if (d != NULL) {
            if (d->active) {
                set_next_device_active(d);
            }

            if (d->primary) {
                /* The primary device is supposed to be set to an
                 * internal device, which cannot be removed, but if an
                 * internal device is not enumerated on startup, it is
                 * possible to have a removable device set to be the
                 * primary device.
                 */
                set_next_device_primary(d);
            }
            remove_device(d);
            send_card_removed_message(d);
            free_device(d);
        }
        UNLOCK();
    }
}

static void udev_sound_subsystem_monitor_work(void)
{
    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
    int fd;

    udev_monitor_filter_add_match_subsystem_devtype(mon, subsystem, NULL);
    udev_monitor_enable_receiving(mon);
    fd = udev_monitor_get_fd(mon);

    while (!thread_management.tm_exit) {
        fd_set              fds;
        struct timeval      timeout;
        int                 ret;
        struct udev_device *dev;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        timeout.tv_sec  = 0;
        timeout.tv_usec = 0;
        ret             = select(fd + 1, &fds, NULL, NULL, &timeout);

        if (ret > 0 && FD_ISSET(fd, &fds)) {
            dev = udev_monitor_receive_device(mon);
            if (dev) {
                const char *action = udev_device_get_action(dev);

                if (is_action_add(action)) {
                    add_device_if_card(dev);
                } else if (is_action_remove(action)) {
                    remove_device_if_card(dev);
                }
                udev_device_unref(dev);
            }
            else {
                verbose_log(0, LOG_ERR,
                            "%s (internal error): No device obtained",
                            __FUNCTION__);
            }
        }
        usleep(250000);         /* 0.25 second */
    }
}

static void *udev_sound_subsystem_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;
    desc = desc;

    /* Initialization Code. */
    pthread_barrier_wait(&thread_management.tm_create_barrier);

    /* Wait for all other threads to start. */
    pthread_barrier_wait(&thread_management.tm_start_barrier);
    udev_sound_subsystem_monitor_work();
    return NULL;
}

static void enumerate_devices(void)
{
    /* Locking not necessary.  No threads are running. */
    struct udev_enumerate  *enumerate = udev_enumerate_new(udev);
    struct udev_list_entry *dl;
    struct udev_list_entry *dev_list_entry;

    udev_enumerate_add_match_subsystem(enumerate, subsystem);
    udev_enumerate_scan_devices(enumerate);
    dl = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, dl) {
        const char         *path = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev  = udev_device_new_from_syspath(udev, path);

        add_device_if_card(dev);
    }
    udev_enumerate_unref(enumerate);

    set_primary_and_active_device();
}

static void initialize(void)
{
    device_t *head = allocate_device("card999999", 0);
    pthread_mutexattr_t attr;

    assert(head != NULL);

    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&devices.device_mutex, &attr);

    /* Create dummy head for list. */
    head->next          = head;
    head->prev          = head;
    devices.device_list = head;

    udev = udev_new();
    assert(udev != NULL);
    enumerate_devices();
}


static void finalize(void)
{
    LOCK();
    DEVICES_ITERATE(d, {
            remove_device(d);
            free_device(d);
        });
    free_device(devices.device_list); /* Release dummy head. */
    UNLOCK();
    pthread_mutex_destroy(&devices.device_mutex);

    udev_unref(udev);
}

INITIALIZER("udev listener", initialize, finalize);
THREAD_DESCRIPTOR("udev listener: input subsystem", TSP_NORMAL,
                  udev_sound_subsystem_monitor);
