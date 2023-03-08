// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {

// To test static functions.
#include "cras/src/server/cras_bt_io.c"
#include "third_party/utlist/utlist.h"
}

static unsigned int cras_iodev_add_node_called;
static unsigned int cras_iodev_rm_node_called;
static unsigned int cras_iodev_free_format_called;
static unsigned int cras_iodev_free_resources_called;
static unsigned int cras_iodev_set_active_node_called;
static unsigned int cras_iodev_list_add_output_called;
static unsigned int cras_iodev_list_rm_output_called;
static unsigned int cras_iodev_list_add_input_called;
static unsigned int cras_iodev_list_rm_input_called;
static int cras_bt_policy_switch_profile_called;
static int is_utf8_string_ret_value;
static size_t cras_iodev_set_node_plugged_called;
static int cras_iodev_set_node_plugged_value;

void ResetStubData() {
  cras_iodev_add_node_called = 0;
  cras_iodev_rm_node_called = 0;
  cras_iodev_free_format_called = 0;
  cras_iodev_free_resources_called = 0;
  cras_iodev_set_active_node_called = 0;
  cras_iodev_set_node_plugged_called = 0;
  cras_iodev_list_add_output_called = 0;
  cras_iodev_list_rm_output_called = 0;
  cras_iodev_list_add_input_called = 0;
  cras_iodev_list_rm_input_called = 0;
  cras_bt_policy_switch_profile_called = 0;
  is_utf8_string_ret_value = 1;
}

namespace {

class BtIoBasicSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    SetUpIodev(&iodev_, CRAS_STREAM_OUTPUT);
    SetUpIodev(&iodev2_, CRAS_STREAM_OUTPUT);
    SetUpIodev(&iodev3_, CRAS_STREAM_OUTPUT);
    iodev_.active_node = &node_;
    iodev2_.active_node = &node2_;
    iodev3_.active_node = &node3_;

    update_supported_formats_called_ = 0;
    frames_queued_called_ = 0;
    delay_frames_called_ = 0;
    get_buffer_called_ = 0;
    put_buffer_called_ = 0;
    configure_dev_called_ = 0;
    close_dev_called_ = 0;

    supported_rates_[0] = 48000;
    supported_rates_[1] = 0;

    supported_channel_counts_[0] = 2;
    supported_channel_counts_[1] = 0;

    supported_formats_[0] = SND_PCM_FORMAT_S16_LE;
    supported_formats_[1] = (snd_pcm_format_t)0;

    bt_io_mgr = bt_io_manager_create();
  }

  virtual void TearDown() { bt_io_manager_destroy(bt_io_mgr); }

  static void SetUpIodev(struct cras_iodev* d, enum CRAS_STREAM_DIRECTION dir) {
    d->direction = dir;
    d->update_supported_formats = update_supported_formats;
    d->frames_queued = frames_queued;
    d->delay_frames = delay_frames;
    d->get_buffer = get_buffer;
    d->put_buffer = put_buffer;
    d->configure_dev = configure_dev;
    d->close_dev = close_dev;
    d->supported_rates = NULL;
    d->supported_channel_counts = NULL;
    d->supported_formats = NULL;
  }

  // Stub functions for the iodev structure.
  static int update_supported_formats(struct cras_iodev* iodev) {
    iodev->supported_rates = supported_rates_;
    iodev->supported_channel_counts = supported_channel_counts_;
    iodev->supported_formats = supported_formats_;
    update_supported_formats_called_++;
    return 0;
  }
  static int frames_queued(const cras_iodev* iodev, struct timespec* tstamp) {
    frames_queued_called_++;
    return 0;
  }
  static int delay_frames(const cras_iodev* iodev) {
    delay_frames_called_++;
    return 0;
  }
  static int get_buffer(cras_iodev* iodev,
                        struct cras_audio_area** area,
                        unsigned int* num) {
    get_buffer_called_++;
    return 0;
  }
  static int put_buffer(cras_iodev* iodev, unsigned int num) {
    put_buffer_called_++;
    return 0;
  }
  static int configure_dev(cras_iodev* iodev) {
    configure_dev_called_++;
    return 0;
  }
  static int close_dev(cras_iodev* iodev) {
    free(iodev->format);
    iodev->format = NULL;
    close_dev_called_++;
    return 0;
  }

  static size_t supported_rates_[2];
  static size_t supported_channel_counts_[2];
  static snd_pcm_format_t supported_formats_[2];
  static struct bt_io_manager* bt_io_mgr;
  static struct cras_iodev iodev_;
  static struct cras_iodev iodev2_;
  static struct cras_iodev iodev3_;
  static struct cras_ionode node_;
  static struct cras_ionode node2_;
  static struct cras_ionode node3_;
  static unsigned int update_supported_formats_called_;
  static unsigned int frames_queued_called_;
  static unsigned int delay_frames_called_;
  static unsigned int get_buffer_called_;
  static unsigned int put_buffer_called_;
  static unsigned int configure_dev_called_;
  static unsigned int close_dev_called_;
};

struct bt_io_manager* BtIoBasicSuite::bt_io_mgr;
size_t BtIoBasicSuite::supported_rates_[2];
size_t BtIoBasicSuite::supported_channel_counts_[2];
snd_pcm_format_t BtIoBasicSuite::supported_formats_[2];
struct cras_iodev BtIoBasicSuite::iodev_;
struct cras_iodev BtIoBasicSuite::iodev2_;
struct cras_iodev BtIoBasicSuite::iodev3_;
struct cras_ionode BtIoBasicSuite::node_;
struct cras_ionode BtIoBasicSuite::node2_;
struct cras_ionode BtIoBasicSuite::node3_;
unsigned int BtIoBasicSuite::update_supported_formats_called_;
unsigned int BtIoBasicSuite::frames_queued_called_;
unsigned int BtIoBasicSuite::delay_frames_called_;
unsigned int BtIoBasicSuite::get_buffer_called_;
unsigned int BtIoBasicSuite::put_buffer_called_;
unsigned int BtIoBasicSuite::configure_dev_called_;
unsigned int BtIoBasicSuite::close_dev_called_;

TEST_F(BtIoBasicSuite, CreateBtIo) {
  struct cras_iodev* bt_iodev;
  struct cras_audio_area* fake_area;
  struct cras_audio_format fake_fmt;
  struct timespec tstamp;
  unsigned fr;

  iodev_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_A2DP);
  EXPECT_NE((void*)NULL, bt_io_mgr->bt_iodevs[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(1, cras_iodev_list_add_output_called);
  EXPECT_EQ(CRAS_BT_FLAG_A2DP, bt_io_mgr->active_btflag);

  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_OUTPUT];

  bt_iodev->open_dev(bt_iodev);
  bt_iodev->format = &fake_fmt;
  bt_iodev->update_supported_formats(bt_iodev);
  EXPECT_EQ(1, update_supported_formats_called_);

  bt_iodev->state = CRAS_IODEV_STATE_OPEN;
  bt_iodev->configure_dev(bt_iodev);
  EXPECT_EQ(1, configure_dev_called_);
  bt_iodev->frames_queued(bt_iodev, &tstamp);
  EXPECT_EQ(1, frames_queued_called_);
  bt_iodev->get_buffer(bt_iodev, &fake_area, &fr);
  EXPECT_EQ(1, get_buffer_called_);
  bt_iodev->put_buffer(bt_iodev, fr);
  EXPECT_EQ(1, put_buffer_called_);
  bt_iodev->close_dev(bt_iodev);
  EXPECT_EQ(1, close_dev_called_);
  EXPECT_EQ(1, cras_iodev_free_format_called);

  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);

  EXPECT_EQ(1, cras_iodev_free_resources_called);
  EXPECT_EQ(1, cras_iodev_list_rm_output_called);
}

TEST_F(BtIoBasicSuite, AppendRmIodev) {
  ResetStubData();

  iodev_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_A2DP);
  EXPECT_NE((void*)NULL, bt_io_mgr->bt_iodevs[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ((void*)NULL, bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT]);
  // EXPECT_EQ(CRAS_BT_FLAG_A2DP, bt_io_mgr.active_btflag);

  iodev2_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev2_, CRAS_BT_FLAG_HFP);
  EXPECT_NE((void*)NULL, bt_io_mgr->bt_iodevs[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ((void*)NULL, bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT]);

  iodev3_.direction = CRAS_STREAM_INPUT;
  iodev3_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev3_, CRAS_BT_FLAG_HFP);
  EXPECT_NE((void*)NULL, bt_io_mgr->bt_iodevs[CRAS_STREAM_OUTPUT]);
  EXPECT_NE((void*)NULL, bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT]);

  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
  EXPECT_EQ(2, cras_iodev_set_node_plugged_called);

  bt_io_manager_remove_iodev(bt_io_mgr, &iodev2_);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev3_);
}

TEST_F(BtIoBasicSuite, SwitchProfileOnOpenDevForInputDev) {
  struct cras_iodev* bt_iodev;

  ResetStubData();
  iodev_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_A2DP);
  iodev2_.direction = CRAS_STREAM_INPUT;
  iodev2_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev2_, CRAS_BT_FLAG_HFP);
  iodev3_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev3_, CRAS_BT_FLAG_HFP);

  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT];
  bt_iodev->open_dev(bt_iodev);

  EXPECT_EQ(CRAS_BT_FLAG_HFP, bt_io_mgr->active_btflag);
  EXPECT_EQ(1, cras_bt_policy_switch_profile_called);

  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev2_);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev3_);
}

TEST_F(BtIoBasicSuite, NoSwitchProfileOnOpenDevForInputDevAlreadyOnHfp) {
  struct cras_iodev* bt_iodev;
  ResetStubData();
  iodev_.direction = CRAS_STREAM_INPUT;
  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);

  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT];
  // No need to switch profile if already on HFP.
  bt_io_mgr->active_btflag = CRAS_BT_FLAG_HFP;
  bt_iodev->open_dev(bt_iodev);

  EXPECT_EQ(0, cras_bt_policy_switch_profile_called);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
}

TEST_F(BtIoBasicSuite, HfpOpenDevWhileProfileSwitchEventQueued) {
  struct cras_iodev* bt_iodev;
  ResetStubData();
  iodev_.direction = CRAS_STREAM_INPUT;
  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);

  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT];
  bt_io_mgr->active_btflag = CRAS_BT_FLAG_HFP;

  bt_io_mgr->is_profile_switching = true;
  EXPECT_EQ(-EAGAIN, bt_iodev->open_dev(bt_iodev));

  EXPECT_EQ(0, cras_bt_policy_switch_profile_called);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
}

TEST_F(BtIoBasicSuite, HfpCloseDevWhileProfileSwitchEventQueued) {
  struct cras_iodev* bt_iodev;
  ResetStubData();
  iodev_.direction = CRAS_STREAM_INPUT;
  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);

  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT];
  bt_iodev->state = CRAS_IODEV_STATE_OPEN;
  bt_io_mgr->active_btflag = CRAS_BT_FLAG_HFP;

  bt_io_mgr->is_profile_switching = true;
  bt_iodev->close_dev(bt_iodev);

  EXPECT_EQ(CRAS_BT_FLAG_HFP, bt_io_mgr->active_btflag);
  EXPECT_EQ(0, cras_bt_policy_switch_profile_called);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
}

TEST_F(BtIoBasicSuite, SwitchProfileOnCloseInputDev) {
  struct cras_iodev* bt_iodev;
  ResetStubData();
  iodev_.direction = CRAS_STREAM_INPUT;
  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);

  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT];
  bt_iodev->state = CRAS_IODEV_STATE_OPEN;

  iodev2_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev2_, CRAS_BT_FLAG_A2DP);

  bt_io_mgr->active_btflag = CRAS_BT_FLAG_HFP;
  bt_iodev->close_dev(bt_iodev);

  EXPECT_EQ(CRAS_BT_FLAG_A2DP, bt_io_mgr->active_btflag);
  EXPECT_EQ(1, cras_bt_policy_switch_profile_called);

  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev2_);
}

TEST_F(BtIoBasicSuite, NoSwitchProfileOnCloseInputDevNoSupportA2dp) {
  struct cras_iodev* bt_iodev;
  ResetStubData();
  iodev_.direction = CRAS_STREAM_INPUT;
  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);
  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT];
  bt_iodev->state = CRAS_IODEV_STATE_OPEN;

  bt_io_mgr->active_btflag = CRAS_BT_FLAG_HFP;
  bt_iodev->close_dev(bt_iodev);

  EXPECT_EQ(0, cras_bt_policy_switch_profile_called);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
}

TEST_F(BtIoBasicSuite, NoSwitchProfileOnCloseInputDevInCloseState) {
  struct cras_iodev* bt_iodev;
  ResetStubData();
  iodev_.direction = CRAS_STREAM_INPUT;
  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);
  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT];
  bt_iodev->state = CRAS_IODEV_STATE_CLOSE;
  iodev2_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev2_, CRAS_BT_FLAG_A2DP);

  bt_io_mgr->active_btflag = CRAS_BT_FLAG_HFP;
  bt_iodev->close_dev(bt_iodev);

  EXPECT_EQ(0, cras_bt_policy_switch_profile_called);

  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev2_);
}

TEST_F(BtIoBasicSuite, SwitchProfileOnAppendA2dpDev) {
  ResetStubData();
  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);

  iodev2_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev2_, CRAS_BT_FLAG_A2DP);

  EXPECT_EQ(CRAS_BT_FLAG_A2DP, bt_io_mgr->active_btflag);
  EXPECT_EQ(1, cras_bt_policy_switch_profile_called);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev2_);
}

TEST_F(BtIoBasicSuite, NoSwitchProfileOnAppendHfpDev) {
  ResetStubData();
  iodev2_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev2_, CRAS_BT_FLAG_A2DP);

  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);

  EXPECT_EQ(0, cras_bt_policy_switch_profile_called);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev2_);
}

TEST_F(BtIoBasicSuite, CreateSetDeviceActiveProfileToA2DP) {
  ResetStubData();
  iodev2_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev2_, CRAS_BT_FLAG_A2DP);
  EXPECT_EQ(CRAS_BT_FLAG_A2DP, bt_io_mgr->active_btflag);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev2_);
}

TEST_F(BtIoBasicSuite, CreateNoSetDeviceActiveProfileToA2DP) {
  struct cras_iodev* bt_iodev;
  ResetStubData();

  iodev_.direction = CRAS_STREAM_INPUT;
  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);
  iodev2_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev2_, CRAS_BT_FLAG_HFP);
  EXPECT_EQ(CRAS_BT_FLAG_HFP, bt_io_mgr->active_btflag);

  // If the BT input is being used, no profile change to A2DP will happen.
  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT];
  bt_iodev->state = CRAS_IODEV_STATE_OPEN;

  iodev3_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev3_, CRAS_BT_FLAG_A2DP);

  EXPECT_EQ(CRAS_BT_FLAG_HFP, bt_io_mgr->active_btflag);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev2_);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev3_);
}

TEST_F(BtIoBasicSuite, CreateSetDeviceActiveProfileToHFP) {
  ResetStubData();
  iodev_.active_node->btflags = CRAS_BT_FLAG_HFP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_HFP);

  EXPECT_EQ(CRAS_BT_FLAG_HFP, bt_io_mgr->active_btflag);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
}

TEST_F(BtIoBasicSuite, CreateDeviceWithInvalidUTF8Name) {
  struct cras_iodev* bt_iodev;
  ResetStubData();
  strcpy(iodev_.info.name, "Something BT");
  iodev_.info.name[0] = 0xfe;
  is_utf8_string_ret_value = 0;
  iodev_.active_node->btflags = CRAS_BT_FLAG_A2DP;
  bt_io_manager_append_iodev(bt_io_mgr, &iodev_, CRAS_BT_FLAG_A2DP);
  bt_iodev = bt_io_mgr->bt_iodevs[CRAS_STREAM_OUTPUT];

  ASSERT_STREQ("BLUETOOTH", bt_iodev->active_node->name);
  bt_io_manager_remove_iodev(bt_io_mgr, &iodev_);
}

}  // namespace

extern "C" {

// Cras iodev
void cras_iodev_add_node(struct cras_iodev* iodev, struct cras_ionode* node) {
  cras_iodev_add_node_called++;
  DL_APPEND(iodev->nodes, node);
}

void cras_iodev_rm_node(struct cras_iodev* iodev, struct cras_ionode* node) {
  cras_iodev_rm_node_called++;
  DL_DELETE(iodev->nodes, node);
}

void cras_iodev_free_format(struct cras_iodev* iodev) {
  cras_iodev_free_format_called++;
}

void cras_iodev_set_active_node(struct cras_iodev* iodev,
                                struct cras_ionode* node) {
  cras_iodev_set_active_node_called++;
  iodev->active_node = node;
}

int cras_iodev_set_node_attr(struct cras_ionode* ionode,
                             enum ionode_attr attr,
                             int value) {
  return 0;
}

void cras_iodev_free_resources(struct cras_iodev* iodev) {
  cras_iodev_free_resources_called++;
}

//  From iodev list.
int cras_iodev_list_add_output(struct cras_iodev* output) {
  cras_iodev_list_add_output_called++;
  return 0;
}

int cras_iodev_list_rm_output(struct cras_iodev* dev) {
  cras_iodev_list_rm_output_called++;
  return 0;
}

int cras_iodev_list_add_input(struct cras_iodev* output) {
  cras_iodev_list_add_input_called++;
  return 0;
}

int cras_iodev_list_rm_input(struct cras_iodev* dev) {
  cras_iodev_list_rm_input_called++;
  return 0;
}

int cras_bt_policy_switch_profile(struct bt_io_manager* mgr) {
  cras_bt_policy_switch_profile_called++;
  return 0;
}

int is_utf8_string(const char* string) {
  return is_utf8_string_ret_value;
}

int cras_iodev_default_no_stream_playback(struct cras_iodev* odev, int enable) {
  return 0;
}

int cras_iodev_frames_queued(struct cras_iodev* iodev,
                             struct timespec* hw_tstamp) {
  return 0;
}

unsigned int cras_iodev_default_frames_to_play_in_sleep(
    struct cras_iodev* odev,
    unsigned int* hw_level,
    struct timespec* hw_tstamp) {
  return 0;
}

void cras_iodev_set_node_plugged(struct cras_ionode* ionode, int plugged) {
  cras_iodev_set_node_plugged_called++;
  cras_iodev_set_node_plugged_value = plugged;
}

}  // extern "C"
