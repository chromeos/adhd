// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>

extern "C" {
#include "cras_apm_reverse.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "float_buffer.h"
}

namespace {
static device_enabled_callback_t device_enabled_callback_val;
static struct cras_iodev* iodev_list_get_first_enabled_iodev_ret;
static int cras_iodev_set_ext_dsp_module_called;
static struct ext_dsp_module* ext_dsp_module_value[8];
static bool cras_iodev_is_aec_use_case_ret;
static int process_reverse_mock_called;
static int process_reverse_needed_ret;
static int output_devices_changed_mock_called;

static int process_reverse_mock(struct float_buffer* fbuf,
                                unsigned int frame_rate) {
  process_reverse_mock_called++;
  return 0;
}
static int process_reverse_needed_mock() {
  return process_reverse_needed_ret;
}
static void output_devices_changed_mock() {
  output_devices_changed_mock_called++;
}

class EchoRefTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    /* Set up |output1| to be chosen as the default echo ref. */
    output1.echo_reference_dev = NULL;
    iodev_list_get_first_enabled_iodev_ret = &output1;

    cras_iodev_set_ext_dsp_module_called = 0;
    process_reverse_mock_called = 0;

    process_reverse_needed_ret = 0;
    output_devices_changed_mock_called = 0;
    cras_apm_reverse_init(process_reverse_mock, process_reverse_needed_mock,
                          output_devices_changed_mock);
    EXPECT_NE((void*)NULL, device_enabled_callback_val);
    EXPECT_EQ(1, cras_iodev_set_ext_dsp_module_called);
    EXPECT_NE((void*)NULL, ext_dsp_module_value[0]);
    EXPECT_EQ(1, output_devices_changed_mock_called);

    /* Save the default_rmod as ext dsp module. */
    default_ext_ = ext_dsp_module_value[0];

    /* Restart counter for test cases. */
    cras_iodev_set_ext_dsp_module_called = 0;
    output_devices_changed_mock_called = 0;
  }
  virtual void TearDown() {
    /* Pretend APM list no longer needs reverse processing. */
    process_reverse_needed_ret = 0;
    cras_apm_reverse_state_update();

    cras_apm_reverse_deinit();
  }
  void configure_ext_dsp_module(struct ext_dsp_module* ext) {
    ext->configure(ext, 800, 2, 48000);
    for (int i = 0; i < 2; i++)
      ext->ports[i] = fbuf;
  }

  float fbuf[500];
  struct cras_iodev output1;
  struct ext_dsp_module* default_ext_;
};

TEST_F(EchoRefTestSuite, ApmProcessReverseData) {
  configure_ext_dsp_module(default_ext_);
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(0, process_reverse_mock_called);

  process_reverse_needed_ret = 1;
  cras_apm_reverse_state_update();

  default_ext_->run(default_ext_, 250);
  EXPECT_EQ(0, process_reverse_mock_called);

  default_ext_->run(default_ext_, 250);
  EXPECT_EQ(1, process_reverse_mock_called);
}

extern "C" {
int cras_iodev_list_set_device_enabled_callback(
    device_enabled_callback_t enabled_cb,
    device_disabled_callback_t disabled_cb,
    void* cb_data) {
  device_enabled_callback_val = enabled_cb;
  return 0;
}
struct cras_iodev* cras_iodev_list_get_first_enabled_iodev(
    enum CRAS_STREAM_DIRECTION direction) {
  return iodev_list_get_first_enabled_iodev_ret;
}
void cras_iodev_set_ext_dsp_module(struct cras_iodev* iodev,
                                   struct ext_dsp_module* ext) {
  ext_dsp_module_value[cras_iodev_set_ext_dsp_module_called++] = ext;
}
bool cras_iodev_is_aec_use_case(const struct cras_ionode* node) {
  return cras_iodev_is_aec_use_case_ret;
}

int cras_system_get_hw_echo_ref_disabled() {
  return 0;
}
}  // extern "C"
}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
