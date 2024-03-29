/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_gpio_jack.h"

#include <fcntl.h>
#include <libudev.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include "cras/common/check.h"
#include "cras/src/common/cras_string.h"

#define NAME_SIZE 256

int gpio_switch_open(const char* pathname) {
  return open(pathname, O_RDONLY);
}

int gpio_switch_read(int fd, void* buf, size_t n_bytes) {
  return read(fd, buf, n_bytes);
}

int gpio_switch_eviocgname(int fd, char* name, size_t n_bytes) {
  return ioctl(fd, EVIOCGNAME(n_bytes), name);
}

int gpio_switch_eviocgbit(int fd, void* buf, size_t n_bytes) {
  return ioctl(fd, EVIOCGBIT(EV_SW, n_bytes), buf);
}

int gpio_switch_eviocgsw(int fd, void* bits, size_t n_bytes) {
  return ioctl(fd, EVIOCGSW(n_bytes), bits);
}

char* sys_input_get_device_name(const char* path) {
  char name[NAME_SIZE];
  int fd = open(path, O_RDONLY);

  if (fd >= 0) {
    gpio_switch_eviocgname(fd, name, sizeof(name));
    close(fd);
    return strndup(name, NAME_SIZE);
  } else {
    syslog(LOG_WARNING, "Could not open '%s': %s", path, cras_strerror(errno));
    return NULL;
  }
}

void gpio_switch_list_for_each(gpio_switch_list_callback callback, void* arg) {
  struct udev* udev;
  struct udev_enumerate* enumerate;
  struct udev_list_entry* dl;
  struct udev_list_entry* dev_list_entry;

  if (!callback) {
    return;
  }

  udev = udev_new();
  CRAS_CHECK(udev != NULL);
  enumerate = udev_enumerate_new(udev);
  udev_enumerate_add_match_subsystem(enumerate, "input");
  udev_enumerate_scan_devices(enumerate);
  dl = udev_enumerate_get_list_entry(enumerate);

  udev_list_entry_foreach(dev_list_entry, dl) {
    const char* path = udev_list_entry_get_name(dev_list_entry);
    struct udev_device* dev = udev_device_new_from_syspath(udev, path);
    const char* devnode = udev_device_get_devnode(dev);
    char* ioctl_name;

    if (devnode == NULL) {
      continue;
    }

    ioctl_name = sys_input_get_device_name(devnode);
    if (ioctl_name == NULL) {
      continue;
    }

    if (callback(devnode, ioctl_name, arg)) {
      free(ioctl_name);
      break;
    }
    free(ioctl_name);
  }
  udev_enumerate_unref(enumerate);
  udev_unref(udev);
}
