// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>
#include <poll.h>
#include <stdio.h>
#include <gtest/gtest.h>
#include <string>

extern "C" {
#include "cras_alsa_jack.h"
#include "cras_types.h"
#include "cras_util.h"
}

namespace {

static size_t snd_hctl_open_called;
static int snd_hctl_open_return_value;
static snd_hctl_t *snd_hctl_open_pointer_val;
static size_t snd_hctl_load_called;
static int snd_hctl_load_return_value;
static int fake_jack_cb_plugged;
static int snd_hctl_close_called;
static void *fake_jack_cb_data;
static size_t fake_jack_cb_called;
static size_t snd_hctl_first_elem_called;
static snd_hctl_elem_t *snd_hctl_first_elem_return_val;
static size_t snd_hctl_elem_next_called;
std::deque<snd_hctl_elem_t *> snd_hctl_elem_next_ret_vals;
static size_t snd_hctl_elem_get_name_called;
static size_t snd_hctl_elem_set_callback_called;
static snd_hctl_elem_callback_t snd_hctl_elem_set_callback_value;
static struct pollfd *snd_hctl_poll_descriptors_fds;
static size_t snd_hctl_poll_descriptors_num_fds;
static size_t snd_hctl_poll_descriptors_called;
static size_t cras_system_add_select_fd_called;
static std::vector<int> cras_system_add_select_fd_values;
static size_t cras_system_rm_select_fd_called;
static std::vector<int> cras_system_rm_select_fd_values;
static size_t snd_hctl_handle_events_called;
static size_t snd_hctl_elem_set_callback_private_called;
static void *snd_hctl_elem_set_callback_private_value;
static size_t snd_hctl_elem_get_hctl_called;
static snd_hctl_t *snd_hctl_elem_get_hctl_return_value;
static size_t snd_ctl_elem_value_get_boolean_called;
static int snd_ctl_elem_value_get_boolean_return_value;
static void *fake_jack_cb_arg;
static size_t snd_hctl_nonblock_called;
static struct cras_alsa_mixer *fake_mixer;
static size_t cras_alsa_mixer_get_output_matching_name_called;
static struct cras_alsa_mixer_output *
    cras_alsa_mixer_get_output_matching_name_return_value;

static void ResetStubData() {
  snd_hctl_open_called = 0;
  snd_hctl_open_return_value = 0;
  snd_hctl_open_pointer_val = reinterpret_cast<snd_hctl_t *>(0x4323);
  snd_hctl_load_called = 0;
  snd_hctl_load_return_value = 0;
  snd_hctl_close_called = 0;
  snd_hctl_first_elem_called = 0;
  snd_hctl_first_elem_return_val = reinterpret_cast<snd_hctl_elem_t *>(0x87);
  snd_hctl_elem_next_called = 0;
  snd_hctl_elem_next_ret_vals.clear();
  snd_hctl_elem_get_name_called = 0;
  snd_hctl_elem_set_callback_called = 0;
  snd_hctl_poll_descriptors_num_fds = 0;
  snd_hctl_poll_descriptors_called = 0;
  cras_system_add_select_fd_called = 0;
  cras_system_add_select_fd_values.clear();
  cras_system_rm_select_fd_called = 0;
  cras_system_rm_select_fd_values.clear();
  snd_hctl_handle_events_called = 0;
  snd_hctl_elem_set_callback_private_called = 0;
  snd_hctl_elem_get_hctl_called = 0;
  snd_ctl_elem_value_get_boolean_called = 0;
  fake_jack_cb_called = 0;
  fake_jack_cb_arg = reinterpret_cast<void *>(0x987);
  snd_hctl_nonblock_called = 0;
  fake_mixer = reinterpret_cast<struct cras_alsa_mixer *>(0x789);
  cras_alsa_mixer_get_output_matching_name_called = 0;
  cras_alsa_mixer_get_output_matching_name_return_value =
      reinterpret_cast<struct cras_alsa_mixer_output *>(0x456);
}

static void fake_jack_cb(const struct cras_alsa_jack *jack,
                         int plugged,
                         void *data)
{
  fake_jack_cb_called++;
  fake_jack_cb_plugged = plugged;
  fake_jack_cb_data = data;
}

TEST(AlsaJacks, CreateFailOpen) {
  ResetStubData();
  snd_hctl_open_return_value = -1;
  snd_hctl_open_pointer_val = NULL;
  EXPECT_EQ(NULL, cras_alsa_jack_list_create(0, 0,
                                             fake_mixer,
                                             CRAS_STREAM_OUTPUT,
                                             fake_jack_cb,
                                             fake_jack_cb_arg));
  EXPECT_EQ(1, snd_hctl_open_called);
}

TEST(AlsaJacks, CreateFailLoad) {
  ResetStubData();
  snd_hctl_load_return_value = -1;
  EXPECT_EQ(NULL, cras_alsa_jack_list_create(0, 0,
                                             fake_mixer,
                                             CRAS_STREAM_OUTPUT,
                                             fake_jack_cb,
                                             fake_jack_cb_arg));
  EXPECT_EQ(1, snd_hctl_open_called);
  EXPECT_EQ(1, snd_hctl_load_called);
  EXPECT_EQ(1, snd_hctl_close_called);
}

TEST(AlsaJacks, CreateNoElements) {
  struct cras_alsa_jack_list *jack_list;

  ResetStubData();
  snd_hctl_first_elem_return_val = NULL;
  jack_list = cras_alsa_jack_list_create(0, 0,
                                         fake_mixer,
                                         CRAS_STREAM_OUTPUT,
                                         fake_jack_cb,
                                         fake_jack_cb_arg);
  ASSERT_NE(static_cast<struct cras_alsa_jack_list *>(NULL), jack_list);
  EXPECT_EQ(1, snd_hctl_open_called);
  EXPECT_EQ(1, snd_hctl_load_called);
  EXPECT_EQ(1, snd_hctl_first_elem_called);
  EXPECT_EQ(0, snd_hctl_elem_next_called);

  cras_alsa_jack_list_destroy(jack_list);
  EXPECT_EQ(1, snd_hctl_close_called);
}

static struct cras_alsa_jack_list *run_test_with_elem_list(
    CRAS_STREAM_DIRECTION direction,
    std::string *elems,
    size_t nelems,
    size_t njacks) {
  struct cras_alsa_jack_list *jack_list;

  snd_hctl_first_elem_return_val =
      reinterpret_cast<snd_hctl_elem_t *>(&elems[0]);
  for (unsigned int i = 1; i < nelems; i++)
    snd_hctl_elem_next_ret_vals.push_front(
        reinterpret_cast<snd_hctl_elem_t *>(&elems[i]));

  jack_list = cras_alsa_jack_list_create(0, 0,
                                         fake_mixer,
                                         direction,
                                         fake_jack_cb,
                                         fake_jack_cb_arg);
  if (jack_list == NULL)
    return jack_list;
  EXPECT_EQ(1, snd_hctl_open_called);
  EXPECT_EQ(1, snd_hctl_load_called);
  EXPECT_EQ(1, snd_hctl_first_elem_called);
  EXPECT_EQ(nelems, snd_hctl_elem_next_called);
  EXPECT_EQ(nelems, snd_hctl_elem_get_name_called);
  EXPECT_EQ(njacks, snd_hctl_elem_set_callback_called);
  if (direction == CRAS_STREAM_OUTPUT)
    EXPECT_EQ(njacks, cras_alsa_mixer_get_output_matching_name_called);

  return jack_list;
}

TEST(AlsaJacks, ReportNull) {
  cras_alsa_jack_list_report(NULL);
}

TEST(AlsaJacks, CreateNoJacks) {
  static std::string elem_names[] = {
    "Mic Jack",
    "foo",
    "bar",
  };
  struct cras_alsa_jack_list *jack_list;

  ResetStubData();
  jack_list = run_test_with_elem_list(CRAS_STREAM_OUTPUT,
                                      elem_names,
                                      ARRAY_SIZE(elem_names),
                                      0);
  ASSERT_NE(static_cast<struct cras_alsa_jack_list *>(NULL), jack_list);

  cras_alsa_jack_list_destroy(jack_list);
  EXPECT_EQ(1, snd_hctl_close_called);
}

TEST(AlsaJacks, CreateOneHpJack) {
  std::string elem_names[] = {
    "asdf",
    "Headphone Jack, klasdjf",
    "Mic Jack",
  };
  struct pollfd poll_fds[] = {
    {3, 0, 0},
  };
  struct cras_alsa_jack_list *jack_list;

  ResetStubData();
  snd_hctl_poll_descriptors_fds = poll_fds;
  snd_hctl_poll_descriptors_num_fds = ARRAY_SIZE(poll_fds);
  jack_list = run_test_with_elem_list(CRAS_STREAM_OUTPUT,
                                      elem_names,
                                      ARRAY_SIZE(elem_names),
                                      1);
  ASSERT_NE(static_cast<struct cras_alsa_jack_list *>(NULL), jack_list);
  EXPECT_EQ(ARRAY_SIZE(poll_fds), cras_system_add_select_fd_called);
  EXPECT_EQ(3, cras_system_add_select_fd_values[0]);

  snd_hctl_elem_get_hctl_return_value = reinterpret_cast<snd_hctl_t *>(0x33);
  snd_hctl_elem_get_name_called = 0;
  snd_ctl_elem_value_get_boolean_return_value = 1;
  snd_hctl_elem_set_callback_value(
      reinterpret_cast<snd_hctl_elem_t *>(&elem_names[1]), 0);
  EXPECT_EQ(1, snd_hctl_elem_get_name_called);
  EXPECT_EQ(1, fake_jack_cb_plugged);
  EXPECT_EQ(1, fake_jack_cb_called);
  EXPECT_EQ(fake_jack_cb_arg, fake_jack_cb_data);

  fake_jack_cb_called = 0;
  cras_alsa_jack_list_report(jack_list);
  EXPECT_EQ(1, fake_jack_cb_plugged);
  EXPECT_EQ(1, fake_jack_cb_called);

  cras_alsa_jack_list_destroy(jack_list);
  EXPECT_EQ(ARRAY_SIZE(poll_fds), cras_system_rm_select_fd_called);
  EXPECT_EQ(3, cras_system_rm_select_fd_values[0]);
  EXPECT_EQ(1, snd_hctl_close_called);
}

TEST(AlsaJacks, CreateOneMicJack) {
  static std::string elem_names[] = {
    "asdf",
    "Headphone Jack",
    "Mic Jack",
  };
  struct cras_alsa_jack_list *jack_list;

  ResetStubData();
  jack_list = run_test_with_elem_list(CRAS_STREAM_INPUT,
                                      elem_names,
                                      ARRAY_SIZE(elem_names),
                                      1);
  ASSERT_NE(static_cast<struct cras_alsa_jack_list *>(NULL), jack_list);

  cras_alsa_jack_list_destroy(jack_list);
  EXPECT_EQ(1, snd_hctl_close_called);
}

/* Stubs */

extern "C" {

// From cras_system_state
int cras_system_add_select_fd(int fd,
			      void (*callback)(void *data),
			      void *callback_data)
{
  cras_system_add_select_fd_called++;
  cras_system_add_select_fd_values.push_back(fd);
  return 0;
}
void cras_system_rm_select_fd(int fd)
{
  cras_system_rm_select_fd_called++;
  cras_system_rm_select_fd_values.push_back(fd);
}

// From alsa-lib hcontrol.c
int snd_hctl_open(snd_hctl_t **hctlp, const char *name, int mode) {
  *hctlp = snd_hctl_open_pointer_val;
  snd_hctl_open_called++;
  return snd_hctl_open_return_value;
}
int snd_hctl_load(snd_hctl_t *hctl) {
  snd_hctl_load_called++;
  return snd_hctl_load_return_value;
}
int snd_hctl_close(snd_hctl_t *hctl) {
  snd_hctl_close_called++;
  return 0;
}
snd_hctl_elem_t *snd_hctl_first_elem(snd_hctl_t *hctl) {
  snd_hctl_first_elem_called++;
  return snd_hctl_first_elem_return_val;
}
snd_hctl_elem_t *snd_hctl_elem_next(snd_hctl_elem_t *elem) {
  snd_hctl_elem_next_called++;
  if (snd_hctl_elem_next_ret_vals.empty())
    return NULL;
  snd_hctl_elem_t *ret_elem = snd_hctl_elem_next_ret_vals.back();
  snd_hctl_elem_next_ret_vals.pop_back();
  return ret_elem;
}
const char *snd_hctl_elem_get_name(const snd_hctl_elem_t *obj) {
  snd_hctl_elem_get_name_called++;
  const std::string *name = reinterpret_cast<const std::string *>(obj);
  return name->c_str();
}
void snd_hctl_elem_set_callback(snd_hctl_elem_t *obj,
                                snd_hctl_elem_callback_t val) {
  snd_hctl_elem_set_callback_called++;
  snd_hctl_elem_set_callback_value = val;
}
int snd_hctl_poll_descriptors_count(snd_hctl_t *hctl) {
  return snd_hctl_poll_descriptors_num_fds;
}
int snd_hctl_poll_descriptors(snd_hctl_t *hctl,
                              struct pollfd *pfds,
                              unsigned int space) {
  unsigned int num = min(space, snd_hctl_poll_descriptors_num_fds);
  memcpy(pfds, snd_hctl_poll_descriptors_fds, num * sizeof(*pfds));
  snd_hctl_poll_descriptors_called++;
  return num;
}
int snd_hctl_handle_events(snd_hctl_t *hctl) {
  snd_hctl_handle_events_called++;
  return 0;
}
void snd_hctl_elem_set_callback_private(snd_hctl_elem_t *obj, void * val) {
  snd_hctl_elem_set_callback_private_called++;
  snd_hctl_elem_set_callback_private_value = val;
}
void *snd_hctl_elem_get_callback_private(const snd_hctl_elem_t *obj) {
  return snd_hctl_elem_set_callback_private_value;
}
snd_hctl_t *snd_hctl_elem_get_hctl(snd_hctl_elem_t *elem) {
  snd_hctl_elem_get_hctl_called++;
  return snd_hctl_elem_get_hctl_return_value;
}
int snd_hctl_elem_read(snd_hctl_elem_t *elem, snd_ctl_elem_value_t * value) {
  return 0;
}
int snd_hctl_nonblock(snd_hctl_t *hctl, int nonblock) {
  snd_hctl_nonblock_called++;
  return 0;
}
// From alsa-lib control.c
int snd_ctl_elem_value_get_boolean(const snd_ctl_elem_value_t *obj,
                                   unsigned int idx) {
  snd_ctl_elem_value_get_boolean_called++;
  return snd_ctl_elem_value_get_boolean_return_value;
}

// From cras_alsa_mixer
struct cras_alsa_mixer_output *cras_alsa_mixer_get_output_matching_name(
    const struct cras_alsa_mixer *cras_mixer,
    size_t device_index,
    const char * const name)
{
  cras_alsa_mixer_get_output_matching_name_called++;
  return cras_alsa_mixer_get_output_matching_name_return_value;
}

} /* extern "C" */

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
