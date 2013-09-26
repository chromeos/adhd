// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>
#include <vector>

extern "C" {
#include "cras_alsa_mixer.h"
#include "cras_types.h"
#include "cras_util.h"
#include "cras_volume_curve.h"
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
static int snd_mixer_selem_set_playback_switch_all_value;
static int snd_mixer_selem_has_playback_volume_called;
static int *snd_mixer_selem_has_playback_volume_return_values;
static int snd_mixer_selem_has_playback_volume_return_values_index;
static int snd_mixer_selem_has_playback_volume_return_values_length;
static int snd_mixer_selem_has_playback_switch_called;
static int *snd_mixer_selem_has_playback_switch_return_values;
static int snd_mixer_selem_has_playback_switch_return_values_index;
static int snd_mixer_selem_has_playback_switch_return_values_length;
static int snd_mixer_selem_set_capture_dB_all_called;
static long *snd_mixer_selem_set_capture_dB_all_values;
static int snd_mixer_selem_set_capture_dB_all_values_index;
static int snd_mixer_selem_set_capture_dB_all_values_length;
static int snd_mixer_selem_set_capture_switch_all_called;
static int snd_mixer_selem_set_capture_switch_all_value;
static int snd_mixer_selem_has_capture_volume_called;
static int *snd_mixer_selem_has_capture_volume_return_values;
static int snd_mixer_selem_has_capture_volume_return_values_index;
static int snd_mixer_selem_has_capture_volume_return_values_length;
static int snd_mixer_selem_has_capture_switch_called;
static int *snd_mixer_selem_has_capture_switch_return_values;
static int snd_mixer_selem_has_capture_switch_return_values_index;
static int snd_mixer_selem_has_capture_switch_return_values_length;
static int snd_mixer_selem_get_name_called;
static const char **snd_mixer_selem_get_name_return_values;
static int snd_mixer_selem_get_name_return_values_index;
static int snd_mixer_selem_get_name_return_values_length;
static int snd_mixer_selem_get_playback_dB_called;
static long *snd_mixer_selem_get_playback_dB_return_values;
static int snd_mixer_selem_get_playback_dB_return_values_index;
static int snd_mixer_selem_get_playback_dB_return_values_length;
static int snd_mixer_selem_get_capture_dB_called;
static long *snd_mixer_selem_get_capture_dB_return_values;
static int snd_mixer_selem_get_capture_dB_return_values_index;
static int snd_mixer_selem_get_capture_dB_return_values_length;
static size_t cras_card_config_get_volume_curve_for_control_called;
static size_t cras_volume_curve_destroy_called;
static size_t snd_mixer_selem_get_playback_dB_range_called;
static size_t snd_mixer_selem_get_playback_dB_range_values_index;
static size_t snd_mixer_selem_get_playback_dB_range_values_length;
static const long *snd_mixer_selem_get_playback_dB_range_min_values;
static const long *snd_mixer_selem_get_playback_dB_range_max_values;
static size_t snd_mixer_selem_get_capture_dB_range_called;
static size_t snd_mixer_selem_get_capture_dB_range_values_index;
static size_t snd_mixer_selem_get_capture_dB_range_values_length;
static const long *snd_mixer_selem_get_capture_dB_range_min_values;
static const long *snd_mixer_selem_get_capture_dB_range_max_values;
static size_t iniparser_getstring_return_index;
static size_t iniparser_getstring_return_length;
static char **iniparser_getstring_returns;

static void ResetStubData() {
  iniparser_getstring_return_index = 0;
  iniparser_getstring_return_length = 0;
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
  snd_mixer_selem_set_capture_dB_all_called = 0;
  snd_mixer_selem_set_capture_dB_all_values = static_cast<long *>(NULL);
  snd_mixer_selem_set_capture_dB_all_values_index = 0;
  snd_mixer_selem_set_capture_dB_all_values_length = 0;
  snd_mixer_selem_set_capture_switch_all_called = 0;
  snd_mixer_selem_has_capture_volume_called = 0;
  snd_mixer_selem_has_capture_volume_return_values = static_cast<int *>(NULL);
  snd_mixer_selem_has_capture_volume_return_values_index = 0;
  snd_mixer_selem_has_capture_volume_return_values_length = 0;
  snd_mixer_selem_has_capture_switch_called = 0;
  snd_mixer_selem_has_capture_switch_return_values = static_cast<int *>(NULL);
  snd_mixer_selem_has_capture_switch_return_values_index = 0;
  snd_mixer_selem_has_capture_switch_return_values_length = 0;
  snd_mixer_selem_get_name_called = 0;
  snd_mixer_selem_get_name_return_values = static_cast<const char **>(NULL);
  snd_mixer_selem_get_name_return_values_index = 0;
  snd_mixer_selem_get_name_return_values_length = 0;
  snd_mixer_selem_get_playback_dB_called = 0;
  snd_mixer_selem_get_playback_dB_return_values = static_cast<long *>(NULL);
  snd_mixer_selem_get_playback_dB_return_values_index = 0;
  snd_mixer_selem_get_playback_dB_return_values_length = 0;
  snd_mixer_selem_get_capture_dB_called = 0;
  snd_mixer_selem_get_capture_dB_return_values = static_cast<long *>(NULL);
  snd_mixer_selem_get_capture_dB_return_values_index = 0;
  snd_mixer_selem_get_capture_dB_return_values_length = 0;
  cras_card_config_get_volume_curve_for_control_called = 0;
  cras_volume_curve_destroy_called = 0;
  snd_mixer_selem_get_playback_dB_range_called = 0;
  snd_mixer_selem_get_playback_dB_range_values_index = 0;
  snd_mixer_selem_get_playback_dB_range_values_length = 0;
  snd_mixer_selem_get_playback_dB_range_min_values = static_cast<long *>(NULL);
  snd_mixer_selem_get_playback_dB_range_max_values = static_cast<long *>(NULL);
  snd_mixer_selem_get_capture_dB_range_called = 0;
  snd_mixer_selem_get_capture_dB_range_values_index = 0;
  snd_mixer_selem_get_capture_dB_range_values_length = 0;
  snd_mixer_selem_get_capture_dB_range_min_values = static_cast<long *>(NULL);
  snd_mixer_selem_get_capture_dB_range_max_values = static_cast<long *>(NULL);
}

TEST(AlsaMixer, CreateFailOpen) {
  struct cras_alsa_mixer *c;

  ResetStubData();
  snd_mixer_open_return_value = -1;
  c = cras_alsa_mixer_create("hw:0", NULL, NULL, 0);
  EXPECT_EQ(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(0, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateFailAttach) {
  struct cras_alsa_mixer *c;

  ResetStubData();
  snd_mixer_attach_return_value = -1;
  c = cras_alsa_mixer_create("hw:0", NULL, NULL, 0);
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
  c = cras_alsa_mixer_create("hw:0", NULL, NULL, 0);
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
  c = cras_alsa_mixer_create("hw:0", NULL, NULL, 0);
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
  c = cras_alsa_mixer_create("hw:0", NULL, NULL, 0);
  ASSERT_NE(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_selem_register_called);
  EXPECT_EQ(1, snd_mixer_load_called);
  EXPECT_EQ(0, snd_mixer_close_called);

  /* set mute shouldn't call anything. */
  cras_alsa_mixer_set_mute(c, 0, NULL);
  EXPECT_EQ(0, snd_mixer_selem_set_playback_switch_all_called);
  /* set volume shouldn't call anything. */
  cras_alsa_mixer_set_dBFS(c, 0, NULL);
  EXPECT_EQ(0, snd_mixer_selem_set_playback_dB_all_called);

  cras_alsa_mixer_destroy(c);
  EXPECT_EQ(1, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateOneUnknownElement) {
  struct cras_alsa_mixer *c;
  const char *element_names[] = {
    "Unknown",
  };
  struct cras_alsa_mixer_output mixer_output;

  ResetStubData();
  snd_mixer_first_elem_return_value = reinterpret_cast<snd_mixer_elem_t *>(1);
  snd_mixer_selem_get_name_return_values = element_names;
  snd_mixer_selem_get_name_return_values_length = ARRAY_SIZE(element_names);
  c = cras_alsa_mixer_create("hw:0", NULL, NULL, 0);
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
  cras_alsa_mixer_set_mute(c, 0, NULL);
  EXPECT_EQ(0, snd_mixer_selem_set_playback_switch_all_called);
  /* if passed a mixer output then it should mute that. */
  mixer_output.elem = reinterpret_cast<snd_mixer_elem_t *>(0x454);
  mixer_output.has_mute = 1;
  cras_alsa_mixer_set_mute(c, 0, &mixer_output);
  EXPECT_EQ(1, snd_mixer_selem_set_playback_switch_all_called);
  /* set volume shouldn't call anything. */
  cras_alsa_mixer_set_dBFS(c, 0, NULL);
  EXPECT_EQ(0, snd_mixer_selem_set_playback_dB_all_called);

  cras_alsa_mixer_destroy(c);
  EXPECT_EQ(1, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateOneMasterElement) {
  struct cras_alsa_mixer *c;
  struct cras_alsa_mixer_output mixer_output;
  int element_playback_volume[] = {
    1,
  };
  int element_playback_switches[] = {
    1,
  };
  const char *element_names[] = {
    "Master",
  };
  long set_dB_values[3];

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
  c = cras_alsa_mixer_create("hw:0", NULL, NULL, 0);
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
  cras_alsa_mixer_set_mute(c, 0, NULL);
  EXPECT_EQ(1, snd_mixer_selem_set_playback_switch_all_called);
  /* set volume should be called for Master. */
  cras_alsa_mixer_set_dBFS(c, 0, NULL);
  EXPECT_EQ(1, snd_mixer_selem_set_playback_dB_all_called);

  /* if passed a mixer output then it should set the volume for that too. */
  mixer_output.elem = reinterpret_cast<snd_mixer_elem_t *>(0x454);
  mixer_output.has_mute = 1;
  mixer_output.has_volume = 1;
  mixer_output.max_volume_dB = 950;
  snd_mixer_selem_set_playback_dB_all_values = set_dB_values;
  snd_mixer_selem_set_playback_dB_all_values_length =
      ARRAY_SIZE(set_dB_values);
  snd_mixer_selem_set_playback_dB_all_values_index = 0;
  snd_mixer_selem_set_playback_dB_all_called = 0;
  snd_mixer_selem_get_playback_dB_called = 0;
  cras_alsa_mixer_set_dBFS(c, 0, &mixer_output);
  EXPECT_EQ(2, snd_mixer_selem_set_playback_dB_all_called);
  EXPECT_EQ(950, set_dB_values[0]);
  EXPECT_EQ(950, set_dB_values[1]);

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
  struct cras_alsa_mixer_output mixer_output;
  long set_dB_values[3];

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
  static const long min_volumes[] = {-500, -1250};
  static const long max_volumes[] = {40, 40};
  snd_mixer_selem_get_playback_dB_range_called = 0;
  snd_mixer_selem_get_playback_dB_range_values_index = 0;
  snd_mixer_selem_get_playback_dB_range_min_values = min_volumes;
  snd_mixer_selem_get_playback_dB_range_max_values = max_volumes;
  snd_mixer_selem_get_playback_dB_range_values_length = ARRAY_SIZE(min_volumes);
  c = cras_alsa_mixer_create("hw:0", NULL, NULL, 0);
  ASSERT_NE(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(2, snd_mixer_selem_get_playback_dB_range_called);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_selem_register_called);
  EXPECT_EQ(1, snd_mixer_load_called);
  EXPECT_EQ(0, snd_mixer_close_called);
  EXPECT_EQ(2, snd_mixer_elem_next_called);
  EXPECT_EQ(2, snd_mixer_selem_get_name_called);
  EXPECT_EQ(1, snd_mixer_selem_has_playback_switch_called);

  /* Set mute should be called for Master only. */
  cras_alsa_mixer_set_mute(c, 0, NULL);
  EXPECT_EQ(1, snd_mixer_selem_set_playback_switch_all_called);
  /* Set volume should be called for Master and PCM. If Master doesn't set to
   * anything but zero then the entire volume should be passed to the PCM
   * control.*/
  long get_dB_returns[] = {
    0,
    0,
  };
  snd_mixer_selem_get_playback_dB_return_values = get_dB_returns;
  snd_mixer_selem_get_playback_dB_return_values_length =
      ARRAY_SIZE(get_dB_returns);
  snd_mixer_selem_set_playback_dB_all_values = set_dB_values;
  snd_mixer_selem_set_playback_dB_all_values_length =
      ARRAY_SIZE(set_dB_values);
  cras_alsa_mixer_set_dBFS(c, -50, NULL);
  EXPECT_EQ(2, snd_mixer_selem_set_playback_dB_all_called);
  EXPECT_EQ(2, snd_mixer_selem_get_playback_dB_called);
  // volume set should be relative to max volume (40 + 40).
  EXPECT_EQ(30, set_dB_values[0]);
  EXPECT_EQ(30, set_dB_values[1]);
  /* Set volume should be called for Master, PCM, and the mixer_output passed
   * in. If Master doesn't set to anything but zero then the entire volume
   * should be passed to the PCM control.*/
  long get_dB_returns_output[] = {
    0,
    0,
  };
  snd_mixer_selem_get_playback_dB_return_values_index = 0;
  snd_mixer_selem_get_playback_dB_return_values = get_dB_returns_output;
  snd_mixer_selem_get_playback_dB_return_values_length =
      ARRAY_SIZE(get_dB_returns_output);
  snd_mixer_selem_set_playback_dB_all_values = set_dB_values;
  snd_mixer_selem_set_playback_dB_all_values_length =
      ARRAY_SIZE(set_dB_values);
  snd_mixer_selem_set_playback_dB_all_values_index = 0;
  snd_mixer_selem_set_playback_dB_all_called = 0;
  snd_mixer_selem_get_playback_dB_called = 0;
  mixer_output.elem = reinterpret_cast<snd_mixer_elem_t *>(0x454);
  mixer_output.has_volume = 1;
  mixer_output.max_volume_dB = 0;
  cras_alsa_mixer_set_dBFS(c, -50, &mixer_output);
  EXPECT_EQ(3, snd_mixer_selem_set_playback_dB_all_called);
  EXPECT_EQ(2, snd_mixer_selem_get_playback_dB_called);
  EXPECT_EQ(30, set_dB_values[0]);
  EXPECT_EQ(30, set_dB_values[1]);
  EXPECT_EQ(30, set_dB_values[2]);
  /* Set volume should be called for Master and PCM. PCM should get the volume
   * remaining after Master is set, in this case -50 - -25 = -25. */
  long get_dB_returns2[] = {
    -25,
    -24,
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
  mixer_output.has_volume = 0;
  cras_alsa_mixer_set_dBFS(c, -50, &mixer_output);
  EXPECT_EQ(2, snd_mixer_selem_set_playback_dB_all_called);
  EXPECT_EQ(2, snd_mixer_selem_get_playback_dB_called);
  EXPECT_EQ(30, set_dB_values[0]);
  EXPECT_EQ(55, set_dB_values[1]);

  cras_alsa_mixer_destroy(c);
  EXPECT_EQ(1, snd_mixer_close_called);
}

TEST(AlsaMixer, CreateTwoMainCaptureElements) {
  struct cras_alsa_mixer *c;
  snd_mixer_elem_t *elements[] = {
    reinterpret_cast<snd_mixer_elem_t *>(1),
  };
  int element_capture_volume[] = {
    1,
    1,
  };
  int element_capture_switches[] = {
    1,
    1,
  };
  const char *element_names[] = {
    "Capture", /* Called twice pere element (one for logging). */
    "Capture",
    "Digital Capture",
    "Digital Capture",
  };

  ResetStubData();
  snd_mixer_first_elem_return_value = reinterpret_cast<snd_mixer_elem_t *>(1);
  snd_mixer_elem_next_return_values = elements;
  snd_mixer_elem_next_return_values_length = ARRAY_SIZE(elements);
  snd_mixer_selem_has_capture_volume_return_values = element_capture_volume;
  snd_mixer_selem_has_capture_volume_return_values_length =
      ARRAY_SIZE(element_capture_volume);
  snd_mixer_selem_has_capture_switch_return_values = element_capture_switches;
  snd_mixer_selem_has_capture_switch_return_values_length =
      ARRAY_SIZE(element_capture_switches);
  snd_mixer_selem_get_name_return_values = element_names;
  snd_mixer_selem_get_name_return_values_length = ARRAY_SIZE(element_names);
  c = cras_alsa_mixer_create("hw:0", NULL, NULL, 0);
  ASSERT_NE(static_cast<struct cras_alsa_mixer *>(NULL), c);
  EXPECT_EQ(1, snd_mixer_open_called);
  EXPECT_EQ(1, snd_mixer_attach_called);
  EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
  EXPECT_EQ(1, snd_mixer_selem_register_called);
  EXPECT_EQ(1, snd_mixer_load_called);
  EXPECT_EQ(0, snd_mixer_close_called);
  EXPECT_EQ(2, snd_mixer_elem_next_called);
  EXPECT_EQ(4, snd_mixer_selem_get_name_called);
  EXPECT_EQ(1, snd_mixer_selem_has_capture_switch_called);

  /* Set mute should be called for Master only. */
  cras_alsa_mixer_set_capture_mute(c, 0);
  EXPECT_EQ(1, snd_mixer_selem_set_capture_switch_all_called);
  /* Set volume should be called for Capture and Digital Capture. If Capture
   * doesn't set to anything but zero then the entire volume should be passed to
   * the Digital Capture control. */
  long get_dB_returns[] = {
    0,
    0,
  };
  long set_dB_values[2];
  snd_mixer_selem_get_capture_dB_return_values = get_dB_returns;
  snd_mixer_selem_get_capture_dB_return_values_length =
      ARRAY_SIZE(get_dB_returns);
  snd_mixer_selem_set_capture_dB_all_values = set_dB_values;
  snd_mixer_selem_set_capture_dB_all_values_length =
      ARRAY_SIZE(set_dB_values);
  cras_alsa_mixer_set_capture_dBFS(c, -10, NULL);
  EXPECT_EQ(2, snd_mixer_selem_set_capture_dB_all_called);
  EXPECT_EQ(2, snd_mixer_selem_get_capture_dB_called);
  EXPECT_EQ(-10, set_dB_values[0]);
  EXPECT_EQ(-10, set_dB_values[1]);
  /* Set volume should be called for Capture and Digital Capture. Capture should
   * get the gain remaining after Mic Boos is set, in this case 20 - 25 = -5. */
  long get_dB_returns2[] = {
    25,
    -5,
  };
  snd_mixer_selem_get_capture_dB_return_values = get_dB_returns2;
  snd_mixer_selem_get_capture_dB_return_values_length =
      ARRAY_SIZE(get_dB_returns2);
  snd_mixer_selem_get_capture_dB_return_values_index = 0;
  snd_mixer_selem_set_capture_dB_all_values = set_dB_values;
  snd_mixer_selem_set_capture_dB_all_values_length =
      ARRAY_SIZE(set_dB_values);
  snd_mixer_selem_set_capture_dB_all_values_index = 0;
  snd_mixer_selem_set_capture_dB_all_called = 0;
  snd_mixer_selem_get_capture_dB_called = 0;
  cras_alsa_mixer_set_capture_dBFS(c, 20, NULL);
  EXPECT_EQ(2, snd_mixer_selem_set_capture_dB_all_called);
  EXPECT_EQ(2, snd_mixer_selem_get_capture_dB_called);
  EXPECT_EQ(20, set_dB_values[0]);
  EXPECT_EQ(-5, set_dB_values[1]);

  /* Set volume to the two main controls plus additional specific input
   * volume control */
  struct mixer_volume_control *mixer_input;
  mixer_input = (struct mixer_volume_control *)calloc(1, sizeof(*mixer_input));
  mixer_input->elem = reinterpret_cast<snd_mixer_elem_t *>(1);

  long get_dB_returns3[] = {
    0,
    0,
  };
  long set_dB_values3[3];
  snd_mixer_selem_get_capture_dB_return_values = get_dB_returns3;
  snd_mixer_selem_get_capture_dB_return_values_length =
      ARRAY_SIZE(get_dB_returns3);
  snd_mixer_selem_get_capture_dB_return_values_index = 0;
  snd_mixer_selem_set_capture_dB_all_values = set_dB_values3;
  snd_mixer_selem_set_capture_dB_all_values_length =
      ARRAY_SIZE(set_dB_values3);
  snd_mixer_selem_set_capture_dB_all_values_index = 0;
  snd_mixer_selem_set_capture_dB_all_called = 0;
  snd_mixer_selem_get_capture_dB_called = 0;

  cras_alsa_mixer_set_capture_dBFS(c, 20, mixer_input);

  EXPECT_EQ(3, snd_mixer_selem_set_capture_dB_all_called);
  EXPECT_EQ(2, snd_mixer_selem_get_capture_dB_called);
  EXPECT_EQ(20, set_dB_values3[0]);
  EXPECT_EQ(20, set_dB_values3[1]);
  EXPECT_EQ(20, set_dB_values3[2]);

  cras_alsa_mixer_destroy(c);
  EXPECT_EQ(1, snd_mixer_close_called);
  free(mixer_input);
}

class AlsaMixerOutputs : public testing::Test {
  protected:
    virtual void SetUp() {
      output_called_values_.clear();
      output_callback_called_ = 0;
      snd_mixer_elem_t *elements[] = {
        reinterpret_cast<snd_mixer_elem_t *>(2),  // PCM
        reinterpret_cast<snd_mixer_elem_t *>(3),  // Headphone
        reinterpret_cast<snd_mixer_elem_t *>(4),  // Speaker
        reinterpret_cast<snd_mixer_elem_t *>(5),  // HDMI
        reinterpret_cast<snd_mixer_elem_t *>(6),  // IEC958
        reinterpret_cast<snd_mixer_elem_t *>(7),  // Mic Boost
        reinterpret_cast<snd_mixer_elem_t *>(8),  // Capture
      };
      int element_playback_volume[] = {
        1,
        1,
        1,
        0,
        0,
        1,
        1,
      };
      int element_playback_switches[] = {
        1,
        1,
        1,
        0,
        1,
        1,
        1,
      };
      int element_capture_volume[] = {
        1,
        1,
      };
      int element_capture_switches[] = {
        1,
        1,
      };
      const char *element_names[] = {
        "Master",
        "PCM",
        "Headphone",
        "Headphone",
        "Headphone", /* Called three times because of log. */
        "Speaker",
        "Speaker",
        "Speaker",
        "HDMI",
        "HDMI",
        "HDMI",
        "IEC958",
        "IEC958",
        "IEC958",
	"Capture",
	"Capture",
	"Digital Capture",
	"Digital Capture",
      };
      const char *output_names_extra[] = {
        "IEC958"
      };
      char *iniparser_returns[] = {
	      NULL,
      };

      ResetStubData();
      snd_mixer_first_elem_return_value =
          reinterpret_cast<snd_mixer_elem_t *>(1);  // Master
      snd_mixer_elem_next_return_values = elements;
      snd_mixer_elem_next_return_values_length = ARRAY_SIZE(elements);
      snd_mixer_selem_has_playback_volume_return_values =
          element_playback_volume;
      snd_mixer_selem_has_playback_volume_return_values_length =
        ARRAY_SIZE(element_playback_volume);
      snd_mixer_selem_has_playback_switch_return_values =
          element_playback_switches;
      snd_mixer_selem_has_playback_switch_return_values_length =
        ARRAY_SIZE(element_playback_switches);
      snd_mixer_selem_has_capture_volume_return_values =
          element_capture_volume;
      snd_mixer_selem_has_capture_volume_return_values_length =
        ARRAY_SIZE(element_capture_volume);
      snd_mixer_selem_has_capture_switch_return_values =
          element_capture_switches;
      snd_mixer_selem_has_capture_switch_return_values_length =
        ARRAY_SIZE(element_capture_switches);
      snd_mixer_selem_get_name_return_values = element_names;
      snd_mixer_selem_get_name_return_values_length = ARRAY_SIZE(element_names);
      iniparser_getstring_returns = iniparser_returns;
      iniparser_getstring_return_length = ARRAY_SIZE(iniparser_returns);
      cras_mixer_ = cras_alsa_mixer_create("hw:0",
          reinterpret_cast<struct cras_card_config*>(5),
          output_names_extra, ARRAY_SIZE(output_names_extra));
      ASSERT_NE(static_cast<struct cras_alsa_mixer *>(NULL), cras_mixer_);
      EXPECT_EQ(1, snd_mixer_open_called);
      EXPECT_EQ(1, snd_mixer_attach_called);
      EXPECT_EQ(0, strcmp(snd_mixer_attach_mixdev, "hw:0"));
      EXPECT_EQ(1, snd_mixer_selem_register_called);
      EXPECT_EQ(1, snd_mixer_load_called);
      EXPECT_EQ(0, snd_mixer_close_called);
      EXPECT_EQ(ARRAY_SIZE(elements) + 1, snd_mixer_elem_next_called);
      EXPECT_EQ(6, snd_mixer_selem_has_playback_volume_called);
      EXPECT_EQ(5, snd_mixer_selem_has_playback_switch_called);
      EXPECT_EQ(2, snd_mixer_selem_has_capture_volume_called);
      EXPECT_EQ(1, snd_mixer_selem_has_capture_switch_called);
      EXPECT_EQ(5, cras_card_config_get_volume_curve_for_control_called);
    }

    virtual void TearDown() {
      cras_alsa_mixer_destroy(cras_mixer_);
      EXPECT_EQ(1, snd_mixer_close_called);
    }

    static void OutputCallback(struct cras_alsa_mixer_output *out, void *arg) {
      output_callback_called_++;
      output_called_values_.push_back(out);
    }

  struct cras_alsa_mixer *cras_mixer_;
  static size_t output_callback_called_;
  static std::vector<struct cras_alsa_mixer_output *> output_called_values_;
};

size_t AlsaMixerOutputs::output_callback_called_;
std::vector<struct cras_alsa_mixer_output *>
    AlsaMixerOutputs::output_called_values_;

TEST_F(AlsaMixerOutputs, CheckNoOutputsForDeviceOne) {
  cras_alsa_mixer_list_outputs(cras_mixer_,
                               1,
                               AlsaMixerOutputs::OutputCallback,
                               reinterpret_cast<void*>(555));
  EXPECT_EQ(0, output_callback_called_);
}

TEST_F(AlsaMixerOutputs, CheckFourOutputsForDeviceZero) {
  cras_alsa_mixer_list_outputs(cras_mixer_,
                               0,
                               AlsaMixerOutputs::OutputCallback,
                               reinterpret_cast<void*>(555));
  EXPECT_EQ(4, output_callback_called_);
}

TEST_F(AlsaMixerOutputs, CheckFindOutputByNameNoMatch) {
  struct cras_alsa_mixer_output *out;

  snd_mixer_selem_get_name_called = 0;
  out = cras_alsa_mixer_get_output_matching_name(cras_mixer_,
                                                 0,  // device_index
                                                 "Headphone Jack");
  EXPECT_EQ(static_cast<struct cras_alsa_mixer_output *>(NULL), out);
  EXPECT_EQ(4, snd_mixer_selem_get_name_called);
}

TEST_F(AlsaMixerOutputs, CheckFindOutputByName) {
  struct cras_alsa_mixer_output *out;
  const char *element_names[] = {
    "Speaker",
    "Headphone",
  };

  snd_mixer_selem_get_name_called = 0;
  snd_mixer_selem_get_name_return_values = element_names;
  snd_mixer_selem_get_name_return_values_index = 0;
  snd_mixer_selem_get_name_return_values_length = ARRAY_SIZE(element_names);
  out = cras_alsa_mixer_get_output_matching_name(cras_mixer_,
                                                 0,  // device_index
                                                 "Headphone Jack");
  EXPECT_NE(static_cast<struct cras_alsa_mixer_output *>(NULL), out);
  EXPECT_EQ(2, snd_mixer_selem_get_name_called);
}

TEST_F(AlsaMixerOutputs, CheckFindOutputHDMIByName) {
  struct cras_alsa_mixer_output *out;
  const char *element_names[] = {
    "Speaker",
    "Headphone",
    "HDMI",
  };

  snd_mixer_selem_get_name_called = 0;
  snd_mixer_selem_get_name_return_values = element_names;
  snd_mixer_selem_get_name_return_values_index = 0;
  snd_mixer_selem_get_name_return_values_length = ARRAY_SIZE(element_names);
  out = cras_alsa_mixer_get_output_matching_name(cras_mixer_,
                                                 0,  // device_index
                                                 "HDMI Jack");
  EXPECT_NE(static_cast<struct cras_alsa_mixer_output *>(NULL), out);
  EXPECT_EQ(3, snd_mixer_selem_get_name_called);
}

TEST_F(AlsaMixerOutputs, CheckFindInputName) {
  struct mixer_volume_control *control;
  snd_mixer_elem_t *elements[] = {
    reinterpret_cast<snd_mixer_elem_t *>(2),  // Headphone
    reinterpret_cast<snd_mixer_elem_t *>(3),  // MIC
  };
  const char *element_names[] = {
    "Speaker",
    "Headphone",
    "MIC",
  };

  snd_mixer_first_elem_return_value = reinterpret_cast<snd_mixer_elem_t *>(1);
  snd_mixer_elem_next_return_values = elements;
  snd_mixer_elem_next_return_values_index = 0;
  snd_mixer_elem_next_return_values_length = ARRAY_SIZE(elements);

  snd_mixer_selem_get_name_called = 0;
  snd_mixer_selem_get_name_return_values = element_names;
  snd_mixer_selem_get_name_return_values_index = 0;
  snd_mixer_selem_get_name_return_values_length = ARRAY_SIZE(element_names);
  control = cras_alsa_mixer_get_input_matching_name(cras_mixer_,
                                                    "MIC");
  EXPECT_NE(static_cast<struct mixer_volume_control *>(NULL), control);
  free(control);
  EXPECT_EQ(3, snd_mixer_selem_get_name_called);
}

TEST_F(AlsaMixerOutputs, ActivateDeactivate) {
  int rc;

  cras_alsa_mixer_list_outputs(cras_mixer_,
                               0,
                               AlsaMixerOutputs::OutputCallback,
                               reinterpret_cast<void*>(555));
  EXPECT_EQ(4, output_callback_called_);
  EXPECT_EQ(4, output_called_values_.size());

  rc = cras_alsa_mixer_set_output_active_state(output_called_values_[0], 0);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(1, snd_mixer_selem_set_playback_switch_all_called);
  cras_alsa_mixer_set_output_active_state(output_called_values_[0], 1);
  EXPECT_EQ(2, snd_mixer_selem_set_playback_switch_all_called);
}

TEST_F(AlsaMixerOutputs, MinMaxCaptureGain) {
  long min, max;
  static const long min_volumes[] = {500, -1250, -40, -40};
  static const long max_volumes[] = {-40, -40, 3000, 400};

  snd_mixer_selem_get_capture_dB_range_called = 0;
  snd_mixer_selem_get_capture_dB_range_values_index = 0;
  snd_mixer_selem_get_capture_dB_range_min_values = min_volumes;
  snd_mixer_selem_get_capture_dB_range_max_values = max_volumes;
  snd_mixer_selem_get_capture_dB_range_values_length = ARRAY_SIZE(min_volumes);
  min = cras_alsa_mixer_get_minimum_capture_gain(cras_mixer_,
		  NULL);
  EXPECT_EQ(-750, min);
  max = cras_alsa_mixer_get_maximum_capture_gain(cras_mixer_,
		  NULL);
  EXPECT_EQ(3400, max);
}

TEST_F(AlsaMixerOutputs, MinMaxCaptureGainWithActiveInput) {
  struct mixer_volume_control *mixer_input;
  long min, max;

  static const long min_volumes[] = {500, -1250, 50, -40, -40, -40};
  static const long max_volumes[] = {-40, -40, -40, 3000, 400, 60};

  snd_mixer_selem_get_capture_dB_range_called = 0;
  snd_mixer_selem_get_capture_dB_range_values_index = 0;
  snd_mixer_selem_get_capture_dB_range_min_values = min_volumes;
  snd_mixer_selem_get_capture_dB_range_max_values = max_volumes;
  snd_mixer_selem_get_capture_dB_range_values_length = ARRAY_SIZE(min_volumes);

  mixer_input = (struct mixer_volume_control *)calloc(1, sizeof(*mixer_input));
  mixer_input->elem = reinterpret_cast<snd_mixer_elem_t *>(2);
  min = cras_alsa_mixer_get_minimum_capture_gain(cras_mixer_, mixer_input);
  max = cras_alsa_mixer_get_maximum_capture_gain(cras_mixer_, mixer_input);
  EXPECT_EQ(-700, min);
  EXPECT_EQ(3460, max);

  free((void *)mixer_input);
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
unsigned int snd_mixer_selem_get_index(snd_mixer_elem_t *elem) {
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
int snd_mixer_selem_has_capture_volume(snd_mixer_elem_t *elem) {
  snd_mixer_selem_has_capture_volume_called++;
  if (snd_mixer_selem_has_capture_volume_return_values_index >=
      snd_mixer_selem_has_capture_volume_return_values_length)
    return -1;

  return snd_mixer_selem_has_capture_volume_return_values[
      snd_mixer_selem_has_capture_volume_return_values_index++];
}
int snd_mixer_selem_has_capture_switch(snd_mixer_elem_t *elem) {
  snd_mixer_selem_has_capture_switch_called++;
  if (snd_mixer_selem_has_capture_switch_return_values_index >=
      snd_mixer_selem_has_capture_switch_return_values_length)
    return -1;

  return snd_mixer_selem_has_capture_switch_return_values[
      snd_mixer_selem_has_capture_switch_return_values_index++];
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
  snd_mixer_selem_set_playback_switch_all_value = value;
  return 0;
}
int snd_mixer_selem_set_capture_dB_all(snd_mixer_elem_t *elem,
                                       long value,
                                       int dir) {
  snd_mixer_selem_set_capture_dB_all_called++;
  if (snd_mixer_selem_set_capture_dB_all_values_index <
      snd_mixer_selem_set_capture_dB_all_values_length)
    snd_mixer_selem_set_capture_dB_all_values[
        snd_mixer_selem_set_capture_dB_all_values_index++] = value;
  return 0;
}
int snd_mixer_selem_get_capture_dB(snd_mixer_elem_t *elem,
                                   snd_mixer_selem_channel_id_t channel,
                                   long *value) {
  snd_mixer_selem_get_capture_dB_called++;
  if (snd_mixer_selem_get_capture_dB_return_values_index >=
      snd_mixer_selem_get_capture_dB_return_values_length)
    *value = 0;
  else
    *value = snd_mixer_selem_get_capture_dB_return_values[
        snd_mixer_selem_get_capture_dB_return_values_index++];
  return 0;
}
int snd_mixer_selem_set_capture_switch_all(snd_mixer_elem_t *elem, int value) {
  snd_mixer_selem_set_capture_switch_all_called++;
  snd_mixer_selem_set_capture_switch_all_value = value;
  return 0;
}
int snd_mixer_selem_get_capture_dB_range(snd_mixer_elem_t *elem, long *min,
                                         long *max) {
  snd_mixer_selem_get_capture_dB_range_called++;
  if (snd_mixer_selem_get_capture_dB_range_values_index >=
      snd_mixer_selem_get_capture_dB_range_values_length) {
    *min = 0;
    *max = 0;
  } else {
    *min = snd_mixer_selem_get_capture_dB_range_min_values[
        snd_mixer_selem_get_capture_dB_range_values_index];
    *max = snd_mixer_selem_get_capture_dB_range_max_values[
        snd_mixer_selem_get_capture_dB_range_values_index++];
  }
  return 0;
}
int snd_mixer_selem_get_playback_dB_range(snd_mixer_elem_t *elem,
                                          long *min,
                                          long *max) {
  snd_mixer_selem_get_playback_dB_range_called++;
  if (snd_mixer_selem_get_playback_dB_range_values_index >=
      snd_mixer_selem_get_playback_dB_range_values_length) {
    *min = 0;
    *max = 0;
  } else {
    *min = snd_mixer_selem_get_playback_dB_range_min_values[
        snd_mixer_selem_get_playback_dB_range_values_index];
    *max = snd_mixer_selem_get_playback_dB_range_max_values[
        snd_mixer_selem_get_playback_dB_range_values_index++];
  }
  return 0;
}

//  From cras_volume_curve.
static long get_dBFS_default(const struct cras_volume_curve *curve,
			     size_t volume)
{
  return 100 * (volume - 100);
}

void cras_volume_curve_destroy(struct cras_volume_curve *curve)
{
  cras_volume_curve_destroy_called++;
  free(curve);
}

// From libiniparser.
struct cras_volume_curve *cras_card_config_get_volume_curve_for_control(
		const struct cras_card_config *card_config,
		const char *control_name)
{
  struct cras_volume_curve *curve;
  curve = (struct cras_volume_curve *)calloc(1, sizeof(*curve));
  cras_card_config_get_volume_curve_for_control_called++;
  if (curve != NULL)
    curve->get_dBFS = get_dBFS_default;
  return curve;
}

} /* extern "C" */

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
