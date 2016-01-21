// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <iniparser.h>
#include <stdio.h>

extern "C" {
#include "cras_alsa_card.h"
#include "cras_alsa_io.h"
#include "cras_alsa_mixer.h"
#include "cras_alsa_ucm.h"
#include "cras_types.h"
#include "cras_util.h"
#include "utlist.h"
}

namespace {

static size_t cras_alsa_mixer_create_called;
static struct cras_alsa_mixer *cras_alsa_mixer_create_return;
static size_t cras_alsa_mixer_destroy_called;
static size_t cras_alsa_iodev_create_called;
static struct cras_iodev *cras_alsa_iodev_create_return;
static size_t cras_alsa_iodev_destroy_called;
static struct cras_iodev *cras_alsa_iodev_destroy_arg;
static size_t snd_ctl_open_called;
static size_t snd_ctl_open_return;
static size_t snd_ctl_close_called;
static size_t snd_ctl_close_return;
static size_t snd_ctl_pcm_next_device_called;
static bool snd_ctl_pcm_next_device_return_error;
static int *snd_ctl_pcm_next_device_set_devs;
static size_t snd_ctl_pcm_next_device_set_devs_size;
static size_t snd_ctl_pcm_next_device_set_devs_index;
static size_t snd_ctl_pcm_info_called;
static int *snd_ctl_pcm_info_rets;
static size_t snd_ctl_pcm_info_rets_size;
static size_t snd_ctl_pcm_info_rets_index;
static size_t snd_ctl_card_info_called;
static int snd_ctl_card_info_ret;
static size_t iniparser_freedict_called;
static size_t iniparser_load_called;
static struct cras_device_blacklist *fake_blacklist;
static int cras_device_blacklist_check_retval;
static unsigned ucm_create_called;
static unsigned ucm_destroy_called;
static size_t ucm_get_dev_for_mixer_called;
static size_t ucm_get_flag_called;
static char ucm_get_flag_name[64];
static char* device_config_dir;
static const char* cras_card_config_dir;
static struct mixer_name *ucm_get_coupled_mixer_names_return_value;
static char *coupled_output_names_value[4];
static size_t coupled_output_names_size_value;

static void ResetStubData() {
  cras_alsa_mixer_create_called = 0;
  cras_alsa_mixer_create_return = reinterpret_cast<struct cras_alsa_mixer *>(1);
  cras_alsa_mixer_destroy_called = 0;
  cras_alsa_iodev_create_called = 0;
  cras_alsa_iodev_create_return = reinterpret_cast<struct cras_iodev *>(2);
  cras_alsa_iodev_destroy_called = 0;
  snd_ctl_open_called = 0;
  snd_ctl_open_return = 0;
  snd_ctl_close_called = 0;
  snd_ctl_close_return = 0;
  snd_ctl_pcm_next_device_called = 0;
  snd_ctl_pcm_next_device_return_error = false;
  snd_ctl_pcm_next_device_set_devs_size = 0;
  snd_ctl_pcm_next_device_set_devs_index = 0;
  snd_ctl_pcm_info_called = 0;
  snd_ctl_pcm_info_rets_size = 0;
  snd_ctl_pcm_info_rets_index = 0;
  snd_ctl_card_info_called = 0;
  snd_ctl_card_info_ret = 0;
  iniparser_freedict_called = 0;
  iniparser_load_called = 0;
  fake_blacklist = reinterpret_cast<struct cras_device_blacklist *>(3);
  cras_device_blacklist_check_retval = 0;
  ucm_create_called = 0;
  ucm_destroy_called = 0;
  ucm_get_dev_for_mixer_called = 0;
  ucm_get_flag_called = 0;
  memset(ucm_get_flag_name, 0, sizeof(ucm_get_flag_name));
  device_config_dir = reinterpret_cast<char *>(3);
  cras_card_config_dir = NULL;
  ucm_get_coupled_mixer_names_return_value = NULL;
  for (size_t i = 0; i < 4; i++)
    coupled_output_names_value[i] = NULL;
  coupled_output_names_size_value = 0;
}

TEST(AlsaCard, CreateFailInvalidCard) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 55;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_EQ(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
}

TEST(AlsaCard, CreateFailMixerInit) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  cras_alsa_mixer_create_return = static_cast<struct cras_alsa_mixer *>(NULL);
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_EQ(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(1, cras_alsa_mixer_create_called);
  EXPECT_EQ(0, cras_alsa_mixer_destroy_called);
}

TEST(AlsaCard, CreateFailCtlOpen) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  snd_ctl_open_return = -1;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_EQ(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(1, snd_ctl_open_called);
  EXPECT_EQ(0, snd_ctl_close_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateFailCtlCardInfo) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  snd_ctl_card_info_ret = -1;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_EQ(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(1, snd_ctl_open_called);
  EXPECT_EQ(1, snd_ctl_close_called);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateNoDevices) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 1;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(1, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(0, cras_alsa_iodev_create_called);
  EXPECT_EQ(1, cras_alsa_card_get_index(c));

  cras_alsa_card_destroy(c);
  EXPECT_EQ(0, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneOutputNextDevError) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_return_error = true;
  card_info.card_type = ALSA_CARD_TYPE_USB;
  card_info.card_index = 0;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_EQ(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(snd_ctl_open_called, snd_ctl_close_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneOutput) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0};
  int info_rets[] = {0, -1};
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  card_info.card_type = ALSA_CARD_TYPE_USB;
  card_info.card_index = 0;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(2, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(1, cras_alsa_iodev_create_called);
  EXPECT_EQ(1, snd_ctl_card_info_called);
  EXPECT_EQ(1, ucm_create_called);
  EXPECT_EQ(1, ucm_get_dev_for_mixer_called);
  EXPECT_EQ(1, ucm_get_flag_called);
  EXPECT_EQ(0, strcmp(ucm_get_flag_name, "ExtraMainVolume"));
  EXPECT_EQ(cras_card_config_dir, device_config_dir);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(1, ucm_destroy_called);
  EXPECT_EQ(1, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneOutputBlacklisted) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0};
  int info_rets[] = {0, -1};
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  cras_device_blacklist_check_retval = 1;
  card_info.card_type = ALSA_CARD_TYPE_USB;
  card_info.card_index = 0;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(2, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(1, snd_ctl_card_info_called);
  EXPECT_EQ(0, cras_alsa_iodev_create_called);
  EXPECT_EQ(cras_card_config_dir, device_config_dir);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(0, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateTwoOutputs) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0, 3};
  int info_rets[] = {0, -1, 0};
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(3, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(2, cras_alsa_iodev_create_called);
  EXPECT_EQ(1, snd_ctl_card_info_called);
  EXPECT_EQ(cras_card_config_dir, device_config_dir);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(2, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneInput) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0};
  int info_rets[] = {-1, 0};
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(2, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(1, cras_alsa_iodev_create_called);
  EXPECT_EQ(cras_card_config_dir, device_config_dir);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(1, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneInputAndOneOutput) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0};
  int info_rets[] = {0, 0};
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(2, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(2, cras_alsa_iodev_create_called);
  EXPECT_EQ(cras_card_config_dir, device_config_dir);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(2, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneInputAndOneOutputTwoDevices) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0, 3};
  int info_rets[] = {0, -1, -1, 0};
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(3, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(2, cras_alsa_iodev_create_called);
  EXPECT_EQ(cras_card_config_dir, device_config_dir);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(2, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneOutputWithCoupledMixers) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0};
  int info_rets[] = {0, -1};
  struct mixer_name *mixer_name_1, *mixer_name_2;
  /* Use strdup because cras_alsa_card_create will delete it. */
  const char *name1 = strdup("MixerName1"), *name2 = strdup("MixerName2");
  size_t i;

  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;

  /* Creates a list of mixer names as return value of
   * ucm_get_coupled_mixer_names. */
  mixer_name_1 = (struct mixer_name*)malloc(sizeof(*mixer_name_1));
  mixer_name_2 = (struct mixer_name*)malloc(sizeof(*mixer_name_2));
  mixer_name_1->name = name1;
  mixer_name_2->name = name2;
  DL_APPEND(ucm_get_coupled_mixer_names_return_value, mixer_name_1);
  DL_APPEND(ucm_get_coupled_mixer_names_return_value, mixer_name_2);

  c = cras_alsa_card_create(&card_info, device_config_dir, fake_blacklist);

  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(2, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(1, cras_alsa_iodev_create_called);
  EXPECT_EQ(1, snd_ctl_card_info_called);
  EXPECT_EQ(1, ucm_create_called);
  EXPECT_EQ(1, ucm_get_dev_for_mixer_called);
  EXPECT_EQ(1, ucm_get_flag_called);
  EXPECT_EQ(0, strcmp(ucm_get_flag_name, "ExtraMainVolume"));
  EXPECT_EQ(cras_card_config_dir, device_config_dir);

  /* Checks cras_alsa_card_create can handle the list and pass the names to
   * cras_alsa_mixer_create. */
  EXPECT_EQ(0, strcmp(coupled_output_names_value[0], "MixerName1"));
  EXPECT_EQ(0, strcmp(coupled_output_names_value[1], "MixerName2"));
  EXPECT_EQ(2, coupled_output_names_size_value);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(1, ucm_destroy_called);
  EXPECT_EQ(1, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);

  for(i = 0; i < coupled_output_names_size_value; i++) {
    free(coupled_output_names_value[i]);
    coupled_output_names_value[i] = NULL;
  }
}


/* Stubs */

extern "C" {
struct cras_alsa_mixer *cras_alsa_mixer_create(
    const char *card_name, const struct cras_card_config *config) {
  cras_alsa_mixer_create_called++;
  return cras_alsa_mixer_create_return;
}

int cras_alsa_mixer_add_controls_by_name_matching(
    struct cras_alsa_mixer* cmix, const char *output_names_extra[],
    size_t output_names_extra_size, const char *extra_master_volume,
    const char *coupled_output_names[], size_t coupled_output_names_size) {
  /* Duplicate coupled_output_names to verify in the end of unittest
   * because names will get freed later in cras_alsa_card_create. */
  for(size_t i = 0; i < coupled_output_names_size; i++)
    coupled_output_names_value[i] = strdup(coupled_output_names[i]);
  coupled_output_names_size_value = coupled_output_names_size;
  return 0;
}

void cras_alsa_mixer_destroy(struct cras_alsa_mixer *cras_mixer) {
  cras_alsa_mixer_destroy_called++;
}

struct cras_iodev *alsa_iodev_create(size_t card_index,
				     const char *card_name,
				     size_t device_index,
				     const char *dev_name,
				     const char *dev_id,
				     enum CRAS_ALSA_CARD_TYPE card_type,
				     int is_first,
				     struct cras_alsa_mixer *mixer,
				     snd_use_case_mgr_t *ucm,
				     enum CRAS_STREAM_DIRECTION direction,
                                     size_t usb_vid,
                                     size_t usb_pid) {
  cras_alsa_iodev_create_called++;
  return cras_alsa_iodev_create_return;
}
void alsa_iodev_destroy(struct cras_iodev *iodev) {
  cras_alsa_iodev_destroy_called++;
  cras_alsa_iodev_destroy_arg = iodev;
}

size_t snd_pcm_info_sizeof() {
  return 10;
}
size_t snd_ctl_card_info_sizeof() {
  return 10;
}
int snd_ctl_open(snd_ctl_t **handle, const char *name, int card) {
  snd_ctl_open_called++;
  if (snd_ctl_open_return == 0)
    *handle = reinterpret_cast<snd_ctl_t*>(0xff);
  else
    *handle = NULL;
  return snd_ctl_open_return;
}
int snd_ctl_close(snd_ctl_t *handle) {
  snd_ctl_close_called++;
  return snd_ctl_close_return;
}
int snd_ctl_pcm_next_device(snd_ctl_t *ctl, int *device) {
  if (snd_ctl_pcm_next_device_return_error) {
    *device = 10;
    return -1;
  }
  snd_ctl_pcm_next_device_called++;
  if (snd_ctl_pcm_next_device_set_devs_index >=
      snd_ctl_pcm_next_device_set_devs_size) {
    *device = -1;
    return 0;
  }
  *device =
      snd_ctl_pcm_next_device_set_devs[snd_ctl_pcm_next_device_set_devs_index];
  snd_ctl_pcm_next_device_set_devs_index++;
  return 0;
}
void snd_pcm_info_set_device(snd_pcm_info_t *obj, unsigned int val) {
}
void snd_pcm_info_set_subdevice(snd_pcm_info_t *obj, unsigned int val) {
}
void snd_pcm_info_set_stream(snd_pcm_info_t *obj, snd_pcm_stream_t val) {
}
int snd_ctl_pcm_info(snd_ctl_t *ctl, snd_pcm_info_t *info) {
  int ret;
  snd_ctl_pcm_info_called++;
  if (snd_ctl_pcm_info_rets_index >=
      snd_ctl_pcm_info_rets_size) {
    return -1;
  }
  ret = snd_ctl_pcm_info_rets[snd_ctl_pcm_info_rets_index];
  snd_ctl_pcm_info_rets_index++;
  return ret;
}
int snd_ctl_card_info(snd_ctl_t *ctl, snd_ctl_card_info_t *info) {
  snd_ctl_card_info_called++;
  return snd_ctl_card_info_ret;
}
const char *snd_ctl_card_info_get_name(const snd_ctl_card_info_t *obj) {
  return "TestName";
}
const char *snd_ctl_card_info_get_id(const snd_ctl_card_info_t *obj) {
  return "TestId";
}

struct cras_card_config *cras_card_config_create(const char *config_path,
						 const char *card_name)
{
  cras_card_config_dir = config_path;
  return NULL;
}

void cras_card_config_destroy(struct cras_card_config *card_config)
{
}

struct cras_volume_curve *cras_card_config_get_volume_curve_for_control(
		const struct cras_card_config *card_config,
		const char *control_name)
{
  return NULL;
}

int cras_device_blacklist_check(
    struct cras_device_blacklist *blacklist,
    unsigned vendor_id,
    unsigned product_id,
    unsigned device_index) {
  EXPECT_EQ(fake_blacklist, blacklist);

  return cras_device_blacklist_check_retval;
}

snd_use_case_mgr_t* ucm_create(const char* name) {
  ucm_create_called++;
  return reinterpret_cast<snd_use_case_mgr_t*>(0x44);
}

void ucm_destroy(snd_use_case_mgr_t* mgr) {
  ucm_destroy_called++;
}

char *ucm_get_dev_for_mixer(snd_use_case_mgr_t *mgr, const char *mixer,
                            enum CRAS_STREAM_DIRECTION dir)
{
  ucm_get_dev_for_mixer_called++;
  return strdup("device");
}

char *ucm_get_flag(snd_use_case_mgr_t *mgr, const char *flag_name) {
  ucm_get_flag_called++;
  strncpy(ucm_get_flag_name, flag_name, sizeof(ucm_get_flag_name));
  return NULL;
}

struct mixer_name *ucm_get_coupled_mixer_names(
  snd_use_case_mgr_t *mgr, const char *dev)
{
  return ucm_get_coupled_mixer_names_return_value;
}

void ucm_free_mixer_names(struct mixer_name *names)
{
  struct mixer_name *m;
  DL_FOREACH(names, m) {
    DL_DELETE(names, m);
    free((void*)m->name);
    free(m);
  }
}

} /* extern "C" */

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
