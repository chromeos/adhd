// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_alsa_mixer.h"
#include "cras_types.h"
#include "cras_util.h"
}

namespace {

static size_t snd_mixer_open_called;
static int snd_mixer_open_return_value;
static size_t snd_mixer_close_called;
static size_t snd_mixer_attach_called;
static int snd_mixer_attach_return_value;
const char *snd_mixer_attach_mixdev;
static size_t snd_mixer_selem_register_called;
static int snd_mixer_selem_register_return_value;
static size_t snd_mixer_load_called;
static int snd_mixer_load_return_value;
static size_t snd_mixer_first_elem_called;
static snd_mixer_elem_t *snd_mixer_first_elem_return_value;
static int snd_mixer_elem_next_called;
static snd_mixer_elem_t **snd_mixer_elem_next_return_values;
static int snd_mixer_elem_next_return_values_index;
static int snd_mixer_elem_next_return_values_length;
static int snd_mixer_selem_set_playback_dB_all_called;
static long *snd_mixer_selem_set_playback_dB_all_values;
static int snd_mixer_selem_set_playback_dB_all_values_index;
static int snd_mixer_selem_set_playback_dB_all_values_length;
static int snd_mixer_selem_set_playback_switch_all_called;
static int snd_mixer_selem_has_playback_volume_called;
static int *snd_mixer_selem_has_playback_volume_return_values;
static int snd_mixer_selem_has_playback_volume_return_values_index;
static int snd_mixer_selem_has_playback_volume_return_values_length;
static int snd_mixer_selem_has_playback_switch_called;
static int *snd_mixer_selem_has_playback_switch_return_values;
static int snd_mixer_selem_has_playback_switch_return_values_index;
static int snd_mixer_selem_has_playback_switch_return_values_length;
static int snd_mixer_selem_get_name_called;
static const char **snd_mixer_selem_get_name_return_values;
static int snd_mixer_selem_get_name_return_values_index;
static int snd_mixer_selem_get_name_return_values_length;
static int snd_mixer_selem_get_playback_dB_called;
static long *snd_mixer_selem_get_playback_dB_return_values;
static int snd_mixer_selem_get_playback_dB_return_values_index;
static int snd_mixer_selem_get_playback_dB_return_values_length;

static void ResetStubData() {
  snd_mixer_open_called = 0;
  snd_mixer_open_return_value = 0;
  snd_mixer_close_called = 0;
  snd_mixer_attach_called = 0;
  snd_mixer_attach_return_value = 0;
  snd_mixer_selem_register_called = 0;
  snd_mixer_selem_register_return_value = 0;
  snd_mixer_load_called = 0;
  snd_mixer_load_return_value = 0;
  snd_mixer_first_elem_called = 0;
  snd_mixer_first_elem_return_value = static_cast<snd_mixer_elem_t *>(NULL);
  snd_mixer_elem_next_called = 0;
  snd_mixer_elem_next_return_values = static_cast<snd_mixer_elem_t **>(NULL);
  snd_mixer_elem_next_return_values_index = 0;
  snd_mixer_elem_next_return_values_length = 0;
  snd_mixer_selem_set_playback_dB_all_called = 0;
  snd_mixer_selem_set_playback_dB_all_values = static_cast<long *>(NULL);
  snd_mixer_selem_set_playback_dB_all_values_index = 0;
  snd_mixer_selem_set_playback_dB_all_values_length = 0;
  snd_mixer_selem_set_playback_switch_all_called = 0;
  snd_mixer_selem_has_playback_volume_called = 0;
  snd_mixer_selem_has_playback_volume_return_values = static_cast<int *>(NULL);
  snd_mixer_selem_has_playback_volume_return_values_index = 0;
  snd_mixer_selem_has_playback_volume_return_values_length = 0;
  snd_mixer_selem_has_playback_switch_called = 0;
  snd_mixer_selem_has_playback_switch_return_values = static_cast<int *>(NULL);
  snd_mixer_selem_has_playback_switch_return_values_index = 0;
  snd_mixer_selem_has_playback_switch_return_values_length = 0;
  snd_mixer_selem_get_name_called = 0;
  snd_mixer_selem_get_name_return_values = static_cast<const char **>(NULL);
  snd_mixer_selem_get_name_return_values_index = 0;
  snd_mixer_selem_get_name_return_values_length = 0;
  snd_mixer_selem_get_playback_dB_called = 0;
  snd_mixer_selem_get_playback_dB_return_values = static_cast<long *>(NULL);
  snd_mixer_selem_get_playback_dB_return_values_index = 0;
  snd_mixer_selem_get_playback_dB_return_values_length = 0;
}

TEST(AlsaMixer, CreateFailOpen) {
  struct cras_alsa_mixer *c;

  ResetStubData();
  snd_mixer_open_return_value = -1;
  c = cras_alsa_mixer_create("hw:0");
  EXPECT_EQ(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(0, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateFailAttach) {
  struct cras_alsa_mixer *c;

  ResetStubData();
  snd_mixer_attach_return_value = -1;
  c = cras_alsa_mixer_create("hw:0");
  EXPECT_EQ(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateFailSelemRegister) {
  struct cras_alsa_mixer *c;

  ResetStubData();
  snd_mixer_selem_register_return_value = -1;
  c = cras_alsa_mixer_create("hw:0");
  EXPECT_EQ(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_selem_register_called);
  EXPECT_EQ(1, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateFailLoad) {
  struct cras_alsa_mixer *c;

  ResetStubData();
  snd_mixer_load_return_value = -1;
  c = cras_alsa_mixer_create("hw:0");
  EXPECT_EQ(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_selem_register_called);
  EXPECT_EQ(1, snd_mixer_load_called);
  EXPECT_EQ(1, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateNoElements) {
  struct cras_alsa_mixer *c;

  ResetStubData();
  c = cras_alsa_mixer_create("hw:0");
  ASSERT_NE(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_selem_register_called);
  EXPECT_EQ(1, snd_mixer_load_called);
  EXPECT_EQ(0, snd_mixer_close_called);

  /* set mute shouldn't call anything. */
  cras_alsa_mixer_set_mute(c, 0);
  EXPECT_EQ(0, snd_mixer_selem_set_playback_switch_all_called);
  /* set volume shouldn't call anything. */
  cras_alsa_mixer_set_dBFS(c, 0);
  EXPECT_EQ(0, snd_mixer_selem_set_playback_dB_all_called);

  cras_alsa_mixer_destroy(c);
  EXPECT_EQ(1, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateOneUnknownElement) {
  struct cras_alsa_mixer *c;
  const char *element_names[] = {
    "Unknown",
  };

  ResetStubData();
  snd_mixer_first_elem_return_value = reinterpret_cast<snd_mixer_elem_t *>(1);
  snd_mixer_selem_get_name_return_values = element_names;
  snd_mixer_selem_get_name_return_values_length = ARRAY_SIZE(element_names);
  c = cras_alsa_mixer_create("hw:0");
  ASSERT_NE(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_selem_register_called);
  EXPECT_EQ(1, snd_mixer_load_called);
  EXPECT_EQ(0, snd_mixer_close_called);
  EXPECT_EQ(0, snd_mixer_selem_has_playback_volume_called);
  EXPECT_EQ(1, snd_mixer_selem_get_name_called);

  /* set mute shouldn't call anything. */
  cras_alsa_mixer_set_mute(c, 0);
  EXPECT_EQ(0, snd_mixer_selem_set_playback_switch_all_called);
  /* set volume shouldn't call anything. */
  cras_alsa_mixer_set_dBFS(c, 0);
  EXPECT_EQ(0, snd_mixer_selem_set_playback_dB_all_called);

  cras_alsa_mixer_destroy(c);
  EXPECT_EQ(1, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateOneMasterElement) {
  struct cras_alsa_mixer *c;
  int element_playback_volume[] = {
    1,
  };
  int element_playback_switches[] = {
    1,
  };
  const char *element_names[] = {
    "Master",
  };

  ResetStubData();
  snd_mixer_first_elem_return_value = reinterpret_cast<snd_mixer_elem_t *>(1);
  snd_mixer_selem_has_playback_volume_return_values = element_playback_volume;
  snd_mixer_selem_has_playback_volume_return_values_length =
      ARRAY_SIZE(element_playback_volume);
  snd_mixer_selem_has_playback_switch_return_values = element_playback_switches;
  snd_mixer_selem_has_playback_switch_return_values_length =
      ARRAY_SIZE(element_playback_switches);
  snd_mixer_selem_get_name_return_values = element_names;
  snd_mixer_selem_get_name_return_values_length = ARRAY_SIZE(element_names);
  c = cras_alsa_mixer_create("hw:0");
  ASSERT_NE(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_selem_register_called);
  EXPECT_EQ(1, snd_mixer_load_called);
  EXPECT_EQ(0, snd_mixer_close_called);
  EXPECT_EQ(1, snd_mixer_selem_get_name_called);
  EXPECT_EQ(1, snd_mixer_elem_next_called);

  /* set mute should be called for Master. */
  cras_alsa_mixer_set_mute(c, 0);
  EXPECT_EQ(1, snd_mixer_selem_set_playback_switch_all_called);
  /* set volume should be called for Master. */
  cras_alsa_mixer_set_dBFS(c, 0);
  EXPECT_EQ(1, snd_mixer_selem_set_playback_dB_all_called);

  cras_alsa_mixer_destroy(c);
  EXPECT_EQ(1, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateTwoMainVolumeElements) {
  struct cras_alsa_mixer *c;
  snd_mixer_elem_t *elements[] = {
    reinterpret_cast<snd_mixer_elem_t *>(1),
  };
  int element_playback_volume[] = {
    1,
    1,
  };
  int element_playback_switches[] = {
    1,
    1,
  };
  const char *element_names[] = {
    "Master",
    "PCM",
  };

  ResetStubData();
  snd_mixer_first_elem_return_value = reinterpret_cast<snd_mixer_elem_t *>(1);
  snd_mixer_elem_next_return_values = elements;
  snd_mixer_elem_next_return_values_length = ARRAY_SIZE(elements);
  snd_mixer_selem_has_playback_volume_return_values = element_playback_volume;
  snd_mixer_selem_has_playback_volume_return_values_length =
      ARRAY_SIZE(element_playback_volume);
  snd_mixer_selem_has_playback_switch_return_values = element_playback_switches;
  snd_mixer_selem_has_playback_switch_return_values_length =
      ARRAY_SIZE(element_playback_switches);
  snd_mixer_selem_get_name_return_values = element_names;
  snd_mixer_selem_get_name_return_values_length = ARRAY_SIZE(element_names);
  c = cras_alsa_mixer_create("hw:0");
  ASSERT_NE(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_selem_register_called);
  EXPECT_EQ(1, snd_mixer_load_called);
  EXPECT_EQ(0, snd_mixer_close_called);
  EXPECT_EQ(2, snd_mixer_elem_next_called);
  EXPECT_EQ(2, snd_mixer_selem_get_name_called);

  /* Set mute should be called for Master only. */
  cras_alsa_mixer_set_mute(c, 0);
  EXPECT_EQ(1, snd_mixer_selem_set_playback_switch_all_called);
  /* Set volume should be called for Master and PCM. If Master doesn't set to
   * anything but zero then the entire volume should be passed to the PCM
   * control.*/
  long get_dB_returns[] = {
    0,
    0,
  };
  long set_dB_values[2];
  snd_mixer_selem_get_playback_dB_return_values = get_dB_returns;
  snd_mixer_selem_get_playback_dB_return_values_length =
      ARRAY_SIZE(get_dB_returns);
  snd_mixer_selem_set_playback_dB_all_values = set_dB_values;
  snd_mixer_selem_set_playback_dB_all_values_length =
      ARRAY_SIZE(set_dB_values);
  cras_alsa_mixer_set_dBFS(c, -50);
  EXPECT_EQ(2, snd_mixer_selem_set_playback_dB_all_called);
  EXPECT_EQ(2, snd_mixer_selem_get_playback_dB_called);
  EXPECT_EQ(-50, set_dB_values[0]);
  EXPECT_EQ(-50, set_dB_values[1]);
  /* Set volume should be called for Master and PCM. PCM should get the volume
   * remaining after Master is set, in this cast -50 - -25 = -25. */
  long get_dB_returns2[] = {
    -25,
    -25,
  };
  snd_mixer_selem_get_playback_dB_return_values = get_dB_returns2;
  snd_mixer_selem_get_playback_dB_return_values_length =
      ARRAY_SIZE(get_dB_returns2);
  snd_mixer_selem_get_playback_dB_return_values_index = 0;
  snd_mixer_selem_set_playback_dB_all_values = set_dB_values;
  snd_mixer_selem_set_playback_dB_all_values_length =
      ARRAY_SIZE(set_dB_values);
  snd_mixer_selem_set_playback_dB_all_values_index = 0;
  snd_mixer_selem_set_playback_dB_all_called = 0;
  snd_mixer_selem_get_playback_dB_called = 0;
  cras_alsa_mixer_set_dBFS(c, -50);
  EXPECT_EQ(2, snd_mixer_selem_set_playback_dB_all_called);
  EXPECT_EQ(2, snd_mixer_selem_get_playback_dB_called);
  EXPECT_EQ(-50, set_dB_values[0]);
  EXPECT_EQ(-25, set_dB_values[1]);

  cras_alsa_mixer_destroy(c);
  EXPECT_EQ(1, snd_mixer_close_called);
}

/* Stubs */

extern "C" {
int snd_mixer_open(snd_mixer_t **mixer, int mode) {
  snd_mixer_open_called++;
  *mixer = reinterpret_cast<snd_mixer_t *>(2);
  return snd_mixer_open_return_value;
}
int snd_mixer_attach(snd_mixer_t *mixer, const char *name) {
  snd_mixer_attach_called++;
  snd_mixer_attach_mixdev = name;
  return snd_mixer_attach_return_value;
}
int snd_mixer_selem_register(snd_mixer_t *mixer,
                             struct snd_mixer_selem_regopt *options,
                             snd_mixer_class_t **classp) {
  snd_mixer_selem_register_called++;
  return snd_mixer_selem_register_return_value;
}
int snd_mixer_load(snd_mixer_t *mixer) {
  snd_mixer_load_called++;
  return snd_mixer_load_return_value;
}
const char *snd_mixer_selem_get_name(snd_mixer_elem_t *elem) {
  snd_mixer_selem_get_name_called++;
  if (snd_mixer_selem_get_name_return_values_index >=
      snd_mixer_selem_get_name_return_values_length)
    return static_cast<char *>(NULL);

  return snd_mixer_selem_get_name_return_values[
      snd_mixer_selem_get_name_return_values_index++];
}
int snd_mixer_selem_get_index(snd_mixer_elem_t *elem) {
  return 0;
}
int snd_mixer_selem_has_playback_volume(snd_mixer_elem_t *elem) {
  snd_mixer_selem_has_playback_volume_called++;
  if (snd_mixer_selem_has_playback_volume_return_values_index >=
      snd_mixer_selem_has_playback_volume_return_values_length)
    return -1;

  return snd_mixer_selem_has_playback_volume_return_values[
      snd_mixer_selem_has_playback_volume_return_values_index++];
}
int snd_mixer_selem_has_playback_switch(snd_mixer_elem_t *elem) {
  snd_mixer_selem_has_playback_switch_called++;
  if (snd_mixer_selem_has_playback_switch_return_values_index >=
      snd_mixer_selem_has_playback_switch_return_values_length)
    return -1;

  return snd_mixer_selem_has_playback_switch_return_values[
      snd_mixer_selem_has_playback_switch_return_values_index++];
}
snd_mixer_elem_t *snd_mixer_first_elem(snd_mixer_t *mixer) {
  snd_mixer_first_elem_called++;
  return snd_mixer_first_elem_return_value;
}
snd_mixer_elem_t *snd_mixer_elem_next(snd_mixer_elem_t *elem) {
  snd_mixer_elem_next_called++;
  if (snd_mixer_elem_next_return_values_index >=
      snd_mixer_elem_next_return_values_length)
    return static_cast<snd_mixer_elem_t *>(NULL);

  return snd_mixer_elem_next_return_values[
      snd_mixer_elem_next_return_values_index++];
}
int snd_mixer_close(snd_mixer_t *mixer) {
  snd_mixer_close_called++;
  return 0;
}
int snd_mixer_selem_set_playback_dB_all(snd_mixer_elem_t *elem,
                                        long value,
                                        int dir) {
  snd_mixer_selem_set_playback_dB_all_called++;
  if (snd_mixer_selem_set_playback_dB_all_values_index <
      snd_mixer_selem_set_playback_dB_all_values_length)
    snd_mixer_selem_set_playback_dB_all_values[
        snd_mixer_selem_set_playback_dB_all_values_index++] = value;
  return 0;
}
int snd_mixer_selem_get_playback_dB(snd_mixer_elem_t *elem,
                                    snd_mixer_selem_channel_id_t channel,
                                    long *value) {
  snd_mixer_selem_get_playback_dB_called++;
  if (snd_mixer_selem_get_playback_dB_return_values_index >=
      snd_mixer_selem_get_playback_dB_return_values_length)
    *value = 0;
  else
    *value = snd_mixer_selem_get_playback_dB_return_values[
        snd_mixer_selem_get_playback_dB_return_values_index++];
  return 0;
}
int snd_mixer_selem_set_playback_switch_all(snd_mixer_elem_t *elem, int value) {
  snd_mixer_selem_set_playback_switch_all_called++;
  return 0;
}
} /* extern "C" */

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
