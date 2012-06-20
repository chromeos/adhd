/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <alsa/asoundlib.h>
#include <regex.h>
#include <syslog.h>
#include <libudev.h>
#include <linux/input.h>

#include "cras_alsa_jack.h"
#include "cras_alsa_mixer.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "utlist.h"

/* Keeps an fd that is registered with system settings.  A list of fds must be
 * kept so that they can be removed when the jack list is destroyed. */
struct jack_poll_fd {
	int fd;
	struct jack_poll_fd *prev, *next;
};

/* cras_gpio_jack:  Describes headphone & microphone jack connected to GPIO
 *
 *   On Arm-based systems, the headphone & microphone jacks are
 *   connected to GPIOs which are plumbed through the /dev/input/event
 *   system.  For these jacks, the software is written to open the
 *   corresponding /dev/input/event file and monitor it for 'insert' &
 *   'remove' activity.
 *
 *   fd           : File descriptor corresponding to the /dev/input/event file.
 *
 *   switch_event : Indicates the type of the /dev/input/event file.
 *                  Either SW_HEADPHONE_INSERT, or SW_MICROPHONE_INSERT.
 *
 *   current_state: 0 -> device not plugged in
 *                  1 -> device plugged in
 *   device_name  : Device name extracted from /dev/input/event[0..9]+.
 *                  Allocated on heap; must free.
 */
struct cras_gpio_jack {
	int fd;
	unsigned switch_event;
	unsigned current_state;
	char *device_name;
};

/* Represents a single alsa Jack, e.g. "Headphone Jack" or "Mic Jack".
 *    is_gpio: 1 -> gpio switch (union field: gpio)
 *             0 -> Alsa 'jack' (union field: elem)
 *    elem - alsa hcontrol element for this jack, when is_gpio == 0.
 *    gpio - description of gpio-based jack, when is_gpio != 0.
 *    jack_list - list of jacks this belongs to.
 *    mixer_output - mixer output control used to control audio to this jack.
 *        This will be null for input jacks.
 */
struct cras_alsa_jack {
	unsigned is_gpio;	/* !0 -> 'gpio' valid
				 *  0 -> 'elem' valid
				 */
	union {
		snd_hctl_elem_t *elem;
		struct cras_gpio_jack gpio;
	};

	struct cras_alsa_jack_list *jack_list;
	struct cras_alsa_mixer_output *mixer_output;
	struct cras_alsa_jack *prev, *next;
};

/* Contains all Jacks for a given device.
 *    hctl - alsa hcontrol for this device.
 *    mixer - cras mixer for the card providing this device.
 *    device_index - Index ALSA uses to refer to the device.  The Y in "hw:X,Y".
 *    registered_fds - list of fds registered with system, to be removed upon
 *        destruction.
 *    change_callback - function to call when the state of a jack changes.
 *    callback_data - data to pass back to the callback.
 *    jacks - list of jacks for this device.
 */
struct cras_alsa_jack_list {
	snd_hctl_t *hctl;
	struct cras_alsa_mixer *mixer;
	size_t device_index;
	struct jack_poll_fd *registered_fds;
	jack_state_change_callback *change_callback;
	void *callback_data;
	struct cras_alsa_jack *jacks;
};

/*
 * Local Helpers.
 */

#define BITS_PER_BYTE		(8)
#define BITS_PER_LONG		(sizeof(long) * BITS_PER_BYTE)
#define NBITS(x)		((((x) - 1) / BITS_PER_LONG) + 1)
#define OFF(x)			((x) % BITS_PER_LONG)
#define BIT(x)			(1UL << OFF(x))
#define LONG(x)			((x) / BITS_PER_LONG)
#define IS_BIT_SET(bit, array)	!!((array[LONG(bit)]) & (1UL << OFF(bit)))

static unsigned sys_input_get_switch_state(
	int	  fd,	 /* Open file descriptor. */
	unsigned  sw,	 /* SW_xxx identifier */
	unsigned *state) /* out: 0 -> off, 1 -> on */
{
	unsigned long	    bits[NBITS(SW_CNT)];
	const unsigned long switch_no = sw;

	memset(bits, '\0', sizeof(bits));
	/* If switch event present & supported, get current state. */
	if (ioctl(fd, EVIOCGBIT(EV_SW, switch_no + 1), bits) >= 0) {
		if (IS_BIT_SET(switch_no, bits)) {
			ioctl(fd, EVIOCGSW(sizeof(bits)), bits);
			*state = IS_BIT_SET(switch_no, bits);
			return 0;
		}
	}
	return -1;
}

static char *sys_input_get_device_name(const char *path)
{
	char name[256];
	int  fd = open(path, O_RDONLY);

	if (fd >= 0) {
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		close(fd);
		return strdup(name);
	} else
		return NULL;
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

static inline struct cras_alsa_jack *cras_alloc_jack(int is_gpio)
{
	struct cras_alsa_jack *jack = calloc(1, sizeof(*jack));
	if (jack == NULL)
		return NULL;
	jack->is_gpio = is_gpio;
	return jack;
}

static inline void gpio_change_callback(struct cras_alsa_jack *jack)
{
	jack->jack_list->change_callback(jack,
					 jack->gpio.current_state,
					 jack->jack_list->callback_data);
}

/* gpio_switch_initial_state
 *
 *   Determines the initial state of a gpio-based switch.
 */
static void gpio_switch_initial_state(struct cras_alsa_jack *jack)
{
	unsigned v;
	unsigned r = sys_input_get_switch_state(jack->gpio.fd,
						jack->gpio.switch_event, &v);
	jack->gpio.current_state = r == 0 ? v : 0;
	gpio_change_callback(jack);
}

/* gpio_switch_callback
 *
 *   This callback is invoked whenever the associated /dev/input/event
 *   file has data to read.  Perform autoswitching to / from the
 *   associated device when data is available.
 */
static void gpio_switch_callback(void *arg)
{
	struct cras_alsa_jack *jack = arg;
	int i;
	int r;
	struct input_event ev[64];

	r = read(jack->gpio.fd, ev,
		 ARRAY_SIZE(ev) * sizeof(struct input_event));

	for (i = 0; i < r / sizeof(struct input_event); ++i) {
		if (ev[i].type == EV_SW && ev[i].code == SW_HEADPHONE_INSERT) {
			jack->gpio.current_state = ev[i].value;
			gpio_change_callback(jack);
		}
	}
}

/* open_and_monitor_gpio:
 *
 *   Opens a /dev/input/event file associated with a headphone /
 *   microphone jack and watches it for activity.
 */
static void open_and_monitor_gpio(struct cras_alsa_jack_list *jack_list,
				  enum CRAS_STREAM_DIRECTION direction,
				  const char *pathname,
				  unsigned switch_event)
{
	struct cras_alsa_jack *jack;
	int r;

	jack = cras_alloc_jack(1);
	if (jack == NULL)
		return;

	jack->jack_list = jack_list;
	DL_APPEND(jack_list->jacks, jack);
	jack->gpio.fd = open(pathname, O_RDONLY);
	jack->gpio.switch_event = switch_event;
        jack->gpio.device_name = sys_input_get_device_name(pathname);

	if (jack->gpio.fd == -1 || jack->gpio.device_name == NULL) {
		free(jack->gpio.device_name);
		free(jack);
		return;
	}

	if (direction == CRAS_STREAM_OUTPUT)
		jack->mixer_output = cras_alsa_mixer_get_output_matching_name(
			jack_list->mixer,
			jack_list->device_index,
			"Headphone");

	sys_input_get_switch_state(jack->gpio.fd, switch_event,
				   &jack->gpio.current_state);
	r = cras_system_add_select_fd(jack->gpio.fd,
				      gpio_switch_callback, jack);
	assert(r == 0);
}

/* gpio_enumerate_dev_input:
 *
 *   Finds /dev/input/event files which correspond to headphones &
 *   microphone switches.  If found, they are opened, associated with
 *   an Alsa control and monitored for activity.
 */
static void gpio_enumerate_dev_input(struct cras_alsa_jack_list *jack_list,
				     unsigned int card_index,
				     enum CRAS_STREAM_DIRECTION direction)
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *dl;
	struct udev_list_entry *dev_list_entry;

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

		if (devnode == NULL)
			continue;

		ioctl_name = sys_input_get_device_name(devnode);
		if (ioctl_name == NULL)
			continue;

		if (direction == CRAS_STREAM_INPUT &&
		    is_microphone_jack(ioctl_name))
			open_and_monitor_gpio(jack_list, direction,
					      devnode, SW_MICROPHONE_INSERT);

		if (direction == CRAS_STREAM_OUTPUT &&
		    is_headphone_jack(ioctl_name))
			open_and_monitor_gpio(jack_list, direction,
					      devnode, SW_HEADPHONE_INSERT);

		free(ioctl_name);
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
}

static void wait_for_dev_input_access(void)
{
	/* Wait for /dev/input/event* files to become accessible by
	 * having group 'input'.  Setting these files to have 'rw'
	 * access to group 'input' is done through a udev rule
	 * installed by adhd into /lib/udev/rules.d.
	 *
	 * Wait for up to 2 seconds for the /dev/input/event* files to be
	 * readable by gavd.
	 *
	 * TODO(thutt): This could also be done with a udev enumerate
	 *              and then a udev monitor.
	 */
	const unsigned max_iterations = 4;
	unsigned i = 0;

	while (i < max_iterations) {
		int		   readable;
		struct timeval	   timeout;
		const char * const pathname = "/dev/input/event0";

		timeout.tv_sec	= 0;
		timeout.tv_usec = 500000;   /* 1/2 second. */
		readable = access(pathname, O_RDONLY);

		/* If the file could be opened, then the udev rule has been
		 * applied and gavd can read the event files.  If there are no
		 * event files, then we don't need to wait.
		 *
		 * If access does not become available, then headphone &
		 * microphone jack autoswitching will not function properly.
		 */
		if (readable == 0 || (readable == -1 && errno == ENOENT)) {
			/* Access allowed, or file does not exist. */
			break;
		}
		assert(readable == -1 && errno == EACCES);
		select(1, NULL, NULL, NULL, &timeout);
		++i;
	}
}

static void find_gpio_jacks(struct cras_alsa_jack_list *jack_list,
			    unsigned int card_index,
			    enum CRAS_STREAM_DIRECTION direction)
{
	/* GPIO switches are on Arm-based machines, and are
	 * only associated with on-board devices.
	 */
	wait_for_dev_input_access();
	gpio_enumerate_dev_input(jack_list, card_index, direction);
}

/* Callback from alsa when a jack control changes.  This is registered with
 * snd_hctl_elem_set_callback in find_jack_controls and run by calling
 * snd_hctl_handle_events in alsa_control_event_pending below.
 * Args:
 *    elem - The ALSA control element that has changed.
 *    mask - unused.
 */
static int hctl_jack_cb(snd_hctl_elem_t *elem, unsigned int mask)
{
	const char *name;
	snd_ctl_elem_value_t *elem_value;
	struct cras_alsa_jack *jack;

	jack = snd_hctl_elem_get_callback_private(elem);
	if (jack == NULL) {
		syslog(LOG_ERR, "Invalid jack from control event.");
		return -EINVAL;
	}

	snd_ctl_elem_value_alloca(&elem_value);
	snd_hctl_elem_read(elem, elem_value);
	name = snd_hctl_elem_get_name(elem);

	syslog(LOG_DEBUG,
	       "Jack %s %s",
	       name,
	       snd_ctl_elem_value_get_boolean(elem_value, 0) ? "plugged"
							     : "unplugged");

	jack->jack_list->change_callback(
			jack,
			snd_ctl_elem_value_get_boolean(elem_value, 0),
			jack->jack_list->callback_data);
	return 0;
}

/* Handles notifications from alsa controls.  Called by main thread when a poll
 * fd provided by alsa signals there is an event available. */
static void alsa_control_event_pending(void *arg)
{
	struct cras_alsa_jack_list *jack_list;

	jack_list = (struct cras_alsa_jack_list *)arg;
	if (jack_list == NULL) {
		syslog(LOG_ERR, "Invalid jack_list from control event.");
		return;
	}

	/* handle_events will trigger the callback registered with each control
	 * that has changed. */
	snd_hctl_handle_events(jack_list->hctl);
}

/* Checks if the given control name is in the supplied list of possible jack
 * control names. */
static int is_jack_control_in_list(const char * const *list,
				   unsigned int list_length,
				   const char *control_name)
{
	unsigned int i;

	for (i = 0; i < list_length; i++)
		if (strcmp(control_name, list[i]) == 0)
			return 1;
	return 0;
}

/* Registers each poll fd (one per jack) with the system so that they are passed
 * to select in the main loop. */
static int add_jack_poll_fds(struct cras_alsa_jack_list *jack_list)
{
	struct pollfd *pollfds;
	nfds_t n;
	unsigned int i;
	int rc = 0;

	n = snd_hctl_poll_descriptors_count(jack_list->hctl);
	if (n == 0)
		return 0;

	pollfds = malloc(n * sizeof(*pollfds));
	if (pollfds == NULL)
		return -ENOMEM;

	n = snd_hctl_poll_descriptors(jack_list->hctl, pollfds, n);
	for (i = 0; i < n; i++) {
		struct jack_poll_fd *registered_fd;

		registered_fd = calloc(1, sizeof(*registered_fd));
		if (registered_fd == NULL) {
			rc = -ENOMEM;
			break;
		}
		registered_fd->fd = pollfds[i].fd;
		DL_APPEND(jack_list->registered_fds, registered_fd);
		rc = cras_system_add_select_fd(registered_fd->fd,
					       alsa_control_event_pending,
					       jack_list);
		if (rc < 0)
			break;
	}
	free(pollfds);
	return rc;
}

/* Cancels registration of each poll fd (one per jack) with the system. */
static void remove_jack_poll_fds(struct cras_alsa_jack_list *jack_list)
{
	struct jack_poll_fd *registered_fd, *tmp;

	DL_FOREACH_SAFE(jack_list->registered_fds, registered_fd, tmp) {
		cras_system_rm_select_fd(registered_fd->fd);
		DL_DELETE(jack_list->registered_fds, registered_fd);
		free(registered_fd);
	}
}

/* Looks for any JACK controls.  Monitors any found controls for changes and
 * decides to route based on plug/unlpug events. */
static int find_jack_controls(struct cras_alsa_jack_list *jack_list,
			      const char *device_name,
			      enum CRAS_STREAM_DIRECTION direction)
{
	int rc;
	snd_hctl_elem_t *elem;
	static const char * const output_jack_names[] = {
		"Headphone Jack",
		"Front Headphone Jack",
	};
	static const char * const input_jack_names[] = {
		"Mic Jack",
	};
	const char * const *jack_names;
	unsigned int num_jack_names;

	if (direction == CRAS_STREAM_OUTPUT) {
		jack_names = output_jack_names;
		num_jack_names = ARRAY_SIZE(output_jack_names);
	} else {
		assert(direction == CRAS_STREAM_INPUT);
		jack_names = input_jack_names;
		num_jack_names = ARRAY_SIZE(input_jack_names);
	}

	rc = snd_hctl_open(&jack_list->hctl, device_name, SND_CTL_NONBLOCK);
	if (rc < 0) {
		syslog(LOG_ERR, "failed to get hctl for %s", device_name);
		return rc;
	}
	rc = snd_hctl_nonblock(jack_list->hctl, 1);
	if (rc < 0) {
		syslog(LOG_ERR, "failed to nonblock hctl for %s", device_name);
		return rc;
	}
	rc = snd_hctl_load(jack_list->hctl);
	if (rc < 0) {
		syslog(LOG_ERR, "failed to load hctl for %s", device_name);
		return rc;
	}
	for (elem = snd_hctl_first_elem(jack_list->hctl); elem != NULL;
			elem = snd_hctl_elem_next(elem)) {
		snd_ctl_elem_iface_t iface;
		const char *name;
		struct cras_alsa_jack *jack;

		iface = snd_hctl_elem_get_interface(elem);
		if (iface != SND_CTL_ELEM_IFACE_CARD)
			continue;
		name = snd_hctl_elem_get_name(elem);
		if (!is_jack_control_in_list(jack_names, num_jack_names, name))
			continue;

		jack = cras_alloc_jack(0);
		if (jack == NULL)
			return -ENOMEM;
		jack->elem = elem;
		jack->jack_list = jack_list;
		DL_APPEND(jack_list->jacks, jack);

		syslog(LOG_DEBUG, "Found Jack: %s for %s", name, device_name);
		snd_hctl_elem_set_callback(elem, hctl_jack_cb);
		snd_hctl_elem_set_callback_private(elem, jack);

		if (direction == CRAS_STREAM_OUTPUT)
			jack->mixer_output =
				cras_alsa_mixer_get_output_matching_name(
					jack_list->mixer,
					jack_list->device_index,
					name);
	}

	/* If we have found jacks, have the poll fds passed to select in the
	 * main loop. */
	if (jack_list->jacks != NULL) {
		rc = add_jack_poll_fds(jack_list);
		if (rc < 0)
			return rc;
	}

	return 0;
}

/*
 * Exported Interface.
 */

struct cras_alsa_jack_list *cras_alsa_jack_list_create(
		unsigned int card_index,
		unsigned int device_index,
		struct cras_alsa_mixer *mixer,
		enum CRAS_STREAM_DIRECTION direction,
		jack_state_change_callback *cb,
		void *cb_data)
{
	struct cras_alsa_jack_list *jack_list;
	char device_name[6];

	/* Enforce alsa limits. */
	assert(card_index < 32);
	assert(device_index < 32);

	jack_list = (struct cras_alsa_jack_list *)calloc(1, sizeof(*jack_list));
	if (jack_list == NULL)
		return NULL;

	jack_list->change_callback = cb;
	jack_list->callback_data = cb_data;
	jack_list->mixer = mixer;
	jack_list->device_index = device_index;

	snprintf(device_name, sizeof(device_name), "hw:%d", card_index);

	if (find_jack_controls(jack_list, device_name, direction) != 0) {
		cras_alsa_jack_list_destroy(jack_list);
		return NULL;
	}

	if (jack_list->jacks == NULL)
		find_gpio_jacks(jack_list, card_index, direction);
	return jack_list;
}

void cras_alsa_jack_list_destroy(struct cras_alsa_jack_list *jack_list)
{
	struct cras_alsa_jack *jack, *tmp;

	if (jack_list == NULL)
		return;
	remove_jack_poll_fds(jack_list);
	DL_FOREACH_SAFE(jack_list->jacks, jack, tmp) {
		DL_DELETE(jack_list->jacks, jack);
		if (jack->is_gpio) {
		    free(jack->gpio.device_name);
		}
		free(jack);
	}
	if (jack_list->hctl)
		snd_hctl_close(jack_list->hctl);
	free(jack_list);
}

struct cras_alsa_mixer_output *cras_alsa_jack_get_mixer_output(
		const struct cras_alsa_jack *jack)
{
	if (jack == NULL)
		return NULL;
	return jack->mixer_output;
}

void cras_alsa_jack_list_report(const struct cras_alsa_jack_list *jack_list)
{
	struct cras_alsa_jack *jack;

	if (jack_list == NULL)
		return;

	DL_FOREACH(jack_list->jacks, jack)
		if (jack->is_gpio)
			gpio_switch_initial_state(jack);
		else
			hctl_jack_cb(jack->elem, 0);
}

const char *cras_alsa_jack_get_name(const struct cras_alsa_jack *jack)
{
	if (jack == NULL)
		return NULL;
	if (jack->is_gpio)
		return jack->gpio.device_name;
	return snd_hctl_elem_get_name(jack->elem);
}
