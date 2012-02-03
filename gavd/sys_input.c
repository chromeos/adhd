
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

char *sys_input_get_device_name(const char *path)
{
    char name[256];
    int  fd = open(path, O_RDONLY);

    if (fd >= 0) {
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        close(fd);
        return strdup(name);
    } else {
        return NULL;
    }
}
