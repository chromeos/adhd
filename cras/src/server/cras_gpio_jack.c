/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libudev.h>
#include <regex.h>

#include "cras_util.h"
#include "cras_gpio_jack.h"

int gpio_switch_open(const char *pathname)
{
	return open(pathname, O_RDONLY);
}

int gpio_switch_read(int fd, void *buf, size_t n_bytes)
{
	return read(fd, buf, n_bytes);
}

int gpio_switch_eviocgname(int fd, char *name, size_t n_bytes)
{
	return ioctl(fd, EVIOCGNAME(n_bytes), name);
}

int gpio_switch_eviocgbit(int fd, unsigned long sw, void *buf)
{
	return ioctl(fd, EVIOCGBIT(EV_SW, sw + 1), buf);
}

int gpio_switch_eviocgsw(int fd, void *bits, size_t n_bytes)
{
	return ioctl(fd, EVIOCGSW(n_bytes), bits);
}

static void compile_regex(regex_t *regex, const char *str)
{
	int r;
	r = regcomp(regex, str, REG_EXTENDED);
	assert(r == 0);
}

static unsigned is_microphone_jack(const char *name)
{
	regmatch_t m[1];
	regex_t regex;
	unsigned success;
	const char *re = "^.*(Mic|Headphone) Jack$";

	compile_regex(&regex, re);
	success = regexec(&regex, name, ARRAY_SIZE(m), m, 0) == 0;
	regfree(&regex);
	return success;
}

static unsigned is_headphone_jack(const char *name)
{
	regmatch_t  m[1];
	regex_t	    regex;
	unsigned    success;
	const char *re = "^.*Headphone Jack$";

	compile_regex(&regex, re);
	success = regexec(&regex, name, ARRAY_SIZE(m), m, 0) == 0;
	regfree(&regex);
	return success;
}

/* sys_input_get_device_name:
 *
 *   Returns the heap-allocated device name of a /dev/input/event*
 *   pathname.  Caller is responsible for releasing.
 *
 */
char *sys_input_get_device_name(const char *path)
{
	char name[256];
	int  fd = open(path, O_RDONLY);

	if (fd >= 0) {
		gpio_switch_eviocgname(fd, name, sizeof(name));
		close(fd);
		return strdup(name);
	} else
		return NULL;
}

/* gpio_get_switch_names:
 *
 *    Fills 'names' with up to 'n_names' entries of
 *    '/dev/input/event*' pathnames which are associated with a GPIO
 *    jack of the specified 'direction'.
 *
 *  Returns the number of filenames found.
 *
 */
unsigned gpio_get_switch_names(enum CRAS_STREAM_DIRECTION direction,
			       char **names, size_t n_names)
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *dl;
	struct udev_list_entry *dev_list_entry;
	unsigned n = 0;

	udev = udev_new();
	assert(udev != NULL);
	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "input");
	udev_enumerate_scan_devices(enumerate);
	dl = udev_enumerate_get_list_entry(enumerate);

	udev_list_entry_foreach(dev_list_entry, dl) {
		const char *path = udev_list_entry_get_name(dev_list_entry);
		struct udev_device *dev = udev_device_new_from_syspath(udev,
								       path);
		const char *devnode = udev_device_get_devnode(dev);
		char *ioctl_name;
		int save;

		if (devnode == NULL)
			continue;

		ioctl_name = sys_input_get_device_name(devnode);
		if (ioctl_name == NULL)
			continue;

		save = ((direction == CRAS_STREAM_INPUT &&
			 is_microphone_jack(ioctl_name)) ||
			(direction == CRAS_STREAM_OUTPUT &&
			 is_headphone_jack(ioctl_name)));

		if (save && n < n_names)
			names[n++] = strdup(devnode);

		free(ioctl_name);
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
	return n;
}
