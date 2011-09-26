
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <malloc.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "verbose.h"
#include "sys_input.h"

#define BITS_PER_BYTE           (8)
#define BITS_PER_LONG           (sizeof(long) * BITS_PER_BYTE)
#define NBITS(x)                ((((x) - 1) / BITS_PER_LONG) + 1)
#define OFF(x)                  ((x) % BITS_PER_LONG)
#define BIT(x)                  (1UL << OFF(x))
#define LONG(x)                 ((x) / BITS_PER_LONG)
#define IS_BIT_SET(bit, array)  !!((array[LONG(bit)]) & (1UL << OFF(bit)))


static int is_event_device(const struct dirent *dir)
{
    /* If 'prefix' is not static, it will be initialized on each
     * invocation.  This causes the following error to be produced:
     *
     *   error: not protecting function: no buffer at least 8 bytes long
     *
     * It's probably a warning which is turned into an error, but it's
     * not useful.  Declaring 'prefix' static removes the
     * initialization and removes the error.
     */
    static const char prefix[]   = "event";
    const size_t      prefix_len = sizeof(prefix) / sizeof(prefix[0]) - 1;

    return strncmp(dir->d_name, prefix, prefix_len) == 0;
}

char *sys_input_find_device_by_name(const char *name)
{
    const char     *dir    = "/dev/input";
    struct dirent **namelist;
    char           *result = NULL;
    int             ndev;
    int             i;
    int             bytes;

    ndev = scandir(dir, &namelist, is_event_device, alphasort);
    for (i = 0; i < ndev; ++i) {
        char path[128];
        char device_name[256];
        int  fd;

        /* 'path' becomes "/dev/input/event[0..32)" */
        bytes = snprintf(path, sizeof(path), "%s/%s", dir, namelist[i]->d_name);
        assert((size_t)bytes <= sizeof(path));

        fd = open(path, O_RDONLY);
        if (fd >= 0) {
            ioctl(fd, EVIOCGNAME(sizeof(device_name)), device_name);
            close(fd);
            if (strncmp(name, device_name, strlen(name) + 1) == 0) {
                result = strdup(path);
                break;
            }
        }
    }
    for (i = 0; i < ndev; ++i) {
        free(namelist[i]);
    }
    VERBOSE_FUNCTION_EXIT();
    return result;
}

unsigned sys_input_get_switch_state(int       fd,    /* Open file descriptor. */
                                    unsigned  sw,    /* SW_xxx identifier */
                                    unsigned *state) /* out: 0 -> off, 1 -> on */
{
    unsigned long       bits[NBITS(SW_CNT)];
    const unsigned long switch_no = sw;

    memset(bits, '\0', sizeof(bits));
    /* If switch event present & supported, get current state. */
    if (ioctl(fd, EVIOCGBIT(EV_SW, switch_no + 1), bits) >= 0) {
        if (IS_BIT_SET(switch_no, bits)) {
            ioctl(fd, EVIOCGSW(sizeof(bits)), bits);
            *state = IS_BIT_SET(switch_no, bits);
            return 1;
        }
    }
    return 0;
}

