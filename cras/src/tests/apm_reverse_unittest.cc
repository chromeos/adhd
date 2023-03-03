// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>

extern "C" {
#include "cras/src/server/cras_apm_reverse.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/float_buffer.h"
}

namespace {
static device_enabled_callback_t device_enabled_callback_val;
static device_removed_callback_t device_removed_callback_val;
static struct cras_iodev* iodev_list_get_first_enabled_iodev_ret;
static int cras_iodev_set_ext_dsp_module_called;
static struct ext_dsp_module* ext_dsp_module_value[8];
static bool cras_iodev_is_tuned_aec_use_case_ret;
static int process_reverse_mock_called;
static int output_devices_changed_mock_called;
static int default_process_reverse_needed_ret;
static struct cras_iodev* fake_requested_echo_refs[8];
static int num_fake_requested_echo_refs = 0;

static int process_reverse_mock(struct float_buffer* fbuf,
                                unsigned int frame_rate,
                                const struct cras_iodev* odev) {
  process_reverse_mock_called++;
  return 0;
}
static int process_reverse_needed_mock(bool default_reverse,
                                       const struct cras_iodev* iodev) {
  if (default_reverse && default_process_reverse_needed_ret) {
    return 1;
  }

  for (int i = 0; i < num_fake_requested_echo_refs; i++) {
    if (iodev == fake_requested_echo_refs[i]) {
      return 1;
    }
  }
  return 0;
}
static void output_devices_changed_mock() {
  output_devices_changed_mock_called++;
}

class EchoRefTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    // Set up |output1| to be chosen as the default echo ref.
    output1.echo_reference_dev = NULL;
    iodev_list_get_first_enabled_iodev_ret = &output1;
    output2.echo_reference_dev = NULL;
    echo_ref.echo_reference_dev = NULL;
    unused_odev.direction = CRAS_STREAM_OUTPUT;

    cras_iodev_set_ext_dsp_module_called = 0;
    process_reverse_mock_called = 0;

    output_devices_changed_mock_called = 0;
    num_fake_requested_echo_refs = 0;
    default_process_reverse_needed_ret = 0;
    cras_apm_reverse_init(process_reverse_mock, process_reverse_needed_mock,
                          output_devices_changed_mock);
    EXPECT_NE((void*)NULL, device_enabled_callback_val);
    EXPECT_EQ(1, cras_iodev_set_ext_dsp_module_called);
    EXPECT_NE((void*)NULL, ext_dsp_module_value[0]);
    EXPECT_EQ(1, output_devices_changed_mock_called);

    // Save the default_rmod as ext dsp module.
    default_ext_ = ext_dsp_module_value[0];

    // Restart counter for test cases.
    cras_iodev_set_ext_dsp_module_called = 0;
    output_devices_changed_mock_called = 0;
  }
  virtual void TearDown() {
    // Pretend stream APM no longer needs reverse processing.
    num_fake_requested_echo_refs = 0;
    cras_apm_reverse_state_update();

    cras_apm_reverse_deinit();
  }
  void configure_ext_dsp_module(struct ext_dsp_module* ext) {
    ext->configure(ext, 800, 2, 48000);
    for (int i = 0; i < 2; i++) {
      ext->ports[i] = fbuf;
    }
  }

  float fbuf[500];
  struct cras_iodev output1, output2, unused_odev, echo_ref;
  struct ext_dsp_module* default_ext_;
  struct cras_stream_apm* stream =
      reinterpret_cast<struct cras_stream_apm*>(0x123);
};

TEST_F(EchoRefTestSuite, ApmProcessReverseData) {
  configure_ext_dsp_module(default_ext_);
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(0, process_reverse_mock_called);

  default_process_reverse_needed_ret = 1;
  cras_apm_reverse_state_update();

  default_ext_->run(default_ext_, 250);
  EXPECT_EQ(0, process_reverse_mock_called);

  default_ext_->run(default_ext_, 250);
  EXPECT_EQ(1, process_reverse_mock_called);
}

/* - System default on A
 * - Set aec ref to B
 * - Set aec ref to A
 * - Select system default to B
 * - Set aec ref to default(NULL)
 */
TEST_F(EchoRefTestSuite, SetAecRefThenToDefault) {
  /* Verify set aec ref call assigns a new ext_dsp_module to
   * iodev other than the default one. */
  cras_apm_reverse_link_echo_ref(stream, &echo_ref);
  fake_requested_echo_refs[num_fake_requested_echo_refs++] = &echo_ref;
  cras_apm_reverse_state_update();
  EXPECT_EQ(1, cras_iodev_set_ext_dsp_module_called);
  EXPECT_NE((void*)NULL, ext_dsp_module_value[0]);
  EXPECT_NE((void*)NULL, ext_dsp_module_value[0]->ports);
  EXPECT_NE(default_ext_, ext_dsp_module_value[0]);

  /* When audio data is written through echo_ref, verify the associated
   * rmod triggers APM process reverse call. */
  configure_ext_dsp_module(ext_dsp_module_value[0]);
  ext_dsp_module_value[0]->run(ext_dsp_module_value[0], 500);
  EXPECT_EQ(1, process_reverse_mock_called);

  /* In comparison, when default echo_ref runs, it does NOT trigger
   * APM process reverse call. */
  configure_ext_dsp_module(default_ext_);
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(1, process_reverse_mock_called);

  /* Specifically set aec ref to output1, which is the current default,
   * i.e what is returned by cras_iodev_list_get_first_enabled_iodev.
   */
  cras_apm_reverse_link_echo_ref(stream, &output1);
  fake_requested_echo_refs[num_fake_requested_echo_refs - 1] = &output1;
  cras_apm_reverse_state_update();
  // Unlink from echo_ref
  EXPECT_EQ(2, cras_iodev_set_ext_dsp_module_called);

  /* Verify that when default_ext_ runs, it triggers APM process
   * reverse call. */
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(2, process_reverse_mock_called);

  /* Pretend user select system default to the first used echo ref.
   * Note that the stream apm is on the default aec ref per ealier
   * logic. */
  iodev_list_get_first_enabled_iodev_ret = &echo_ref;
  device_enabled_callback_val(&unused_odev, NULL);
  // Two more calls. Unlink from output1 then link to echo_ref.
  EXPECT_EQ(4, cras_iodev_set_ext_dsp_module_called);
  EXPECT_EQ((void*)NULL, ext_dsp_module_value[1]);
  EXPECT_NE((void*)NULL, ext_dsp_module_value[2]);

  // Verify dev changed callback is triggered accordingly.
  EXPECT_EQ(1, output_devices_changed_mock_called);
  cras_apm_reverse_state_update();

  /* Since stream apm is on another echo ref set ealier. Running the
   * new iodev/rmod won't trigger apm process reverse call. */
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(2, process_reverse_mock_called);

  /* Unset the echo ref, pretend that stream apm goes back to track the
   * system default echo ref. */
  cras_apm_reverse_link_echo_ref(stream, NULL);
  default_process_reverse_needed_ret = 1;
  num_fake_requested_echo_refs = 0;
  cras_apm_reverse_state_update();
  // Unlink from output1 which was linked earlier.
  EXPECT_EQ(5, cras_iodev_set_ext_dsp_module_called);

  /* Now the stream apm is tracking default, run it should trigger apm
   * process reverse call. */
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(3, process_reverse_mock_called);
}

/*
 * - System default on A
 * - Select aec ref to B
 * - Select system default to B
 * - Select system default to A
 */
TEST_F(EchoRefTestSuite, SetAecRefThenDefaultChangesBackAndForth) {
  /* Verify set aec ref call assigns a new ext_dsp_module to
   * iodev other than the default one. */
  cras_apm_reverse_link_echo_ref(stream, &echo_ref);
  fake_requested_echo_refs[num_fake_requested_echo_refs++] = &echo_ref;
  cras_apm_reverse_state_update();

  EXPECT_EQ(1, cras_iodev_set_ext_dsp_module_called);
  EXPECT_NE((void*)NULL, ext_dsp_module_value[0]);
  EXPECT_NE((void*)NULL, ext_dsp_module_value[0]->ports);
  EXPECT_NE(default_ext_, ext_dsp_module_value[0]);

  /* When audio data is written through echo_ref, verify the associated
   * rmod triggers APM process reverse call. */
  configure_ext_dsp_module(ext_dsp_module_value[0]);
  ext_dsp_module_value[0]->run(ext_dsp_module_value[0], 500);
  EXPECT_EQ(1, process_reverse_mock_called);

  // Pretend user select system default to the echo ref just set.
  iodev_list_get_first_enabled_iodev_ret = &echo_ref;
  device_enabled_callback_val(&unused_odev, NULL);
  EXPECT_EQ(3, cras_iodev_set_ext_dsp_module_called);

  // Verify dev changed callback is triggered accordingly.
  EXPECT_EQ(1, output_devices_changed_mock_called);
  cras_apm_reverse_state_update();

  /* Expect this device change sets a ext_dsp_module that is exactly the
   * default one. */
  EXPECT_EQ(default_ext_, ext_dsp_module_value[1]);
  configure_ext_dsp_module(ext_dsp_module_value[1]);
  ext_dsp_module_value[1]->run(ext_dsp_module_value[1], 500);
  EXPECT_EQ(2, process_reverse_mock_called);

  // User selects system default back to the old value.
  iodev_list_get_first_enabled_iodev_ret = &output1;
  device_enabled_callback_val(&unused_odev, NULL);
  EXPECT_EQ(5, cras_iodev_set_ext_dsp_module_called);
  // Verify dev changed callback is triggered accordingly.
  EXPECT_EQ(2, output_devices_changed_mock_called);
  cras_apm_reverse_state_update();

  /* Expect two more calls to set ext dsp module: for new and old
   * respectively. Intercept the later of the two and verify running
   * it would still trigger APM process reverse call. */
  EXPECT_NE((void*)NULL, ext_dsp_module_value[4]);
  configure_ext_dsp_module(ext_dsp_module_value[4]);
  ext_dsp_module_value[4]->run(ext_dsp_module_value[4], 500);
  EXPECT_EQ(3, process_reverse_mock_called);
}

/* - System default on A
 * - Request to add an echo ref B
 * - Set system default to C
 * - Select system default to B
 */
TEST_F(EchoRefTestSuite, SetAecRefBeforeStart) {
  /* APM hasn't started yet. Default ext dsp module won't trigger
   * process reverse stream by running. */
  configure_ext_dsp_module(default_ext_);
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(0, process_reverse_mock_called);

  //
  cras_apm_reverse_link_echo_ref(stream, &echo_ref);
  fake_requested_echo_refs[num_fake_requested_echo_refs++] = &echo_ref;
  cras_apm_reverse_state_update();

  EXPECT_EQ(1, cras_iodev_set_ext_dsp_module_called);
  EXPECT_NE((void*)NULL, ext_dsp_module_value[0]);
  EXPECT_NE(default_ext_, ext_dsp_module_value[0]);

  /* Expect default ext dsp module won't trigger APM process reverse stream
   * because the aec ref set ealier is different than default output. */
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(0, process_reverse_mock_called);

  /* Verify the ext dsp module on the echo ref we set earlier would
   * trigger APM process reverse stream call. */
  configure_ext_dsp_module(ext_dsp_module_value[0]);
  ext_dsp_module_value[0]->run(ext_dsp_module_value[0], 500);
  EXPECT_EQ(1, process_reverse_mock_called);

  // Pretend that user changes the default to output2.
  iodev_list_get_first_enabled_iodev_ret = &output2;
  device_enabled_callback_val(&unused_odev, NULL);
  EXPECT_EQ(3, cras_iodev_set_ext_dsp_module_called);
  EXPECT_EQ(default_ext_, ext_dsp_module_value[1]);
  // Verify dev changed callback is triggered accordingly.
  EXPECT_EQ(1, output_devices_changed_mock_called);
  cras_apm_reverse_state_update();

  /* The default still don't trigger more reverse processing, because the
   * current default |output2| is different from |echo_ref| */
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(1, process_reverse_mock_called);

  // Pretend that user changes the default to the same echo ref.
  iodev_list_get_first_enabled_iodev_ret = &echo_ref;
  device_enabled_callback_val(&unused_odev, NULL);
  EXPECT_EQ(5, cras_iodev_set_ext_dsp_module_called);
  EXPECT_EQ(default_ext_, ext_dsp_module_value[3]);
  // Verify dev changed callback is triggered accordingly.
  EXPECT_EQ(2, output_devices_changed_mock_called);
  cras_apm_reverse_state_update();

  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(2, process_reverse_mock_called);
}

/* - System default on A
 * - Set aec ref to A
 * - Select system default to B
 * - Set aec ref to default(NULL)
 */
TEST_F(EchoRefTestSuite, SetAecRefToDefaultChangeDefaultThenUnsetAecRef) {
  /* There won't be any call to add another ext dsp module, because
   * caller sets aec ref to the current default. */
  cras_apm_reverse_link_echo_ref(stream, &output1);
  fake_requested_echo_refs[num_fake_requested_echo_refs++] = &output1;
  cras_apm_reverse_state_update();
  EXPECT_EQ(0, cras_iodev_set_ext_dsp_module_called);

  /* Default ext DSP module would trigger APM process reverse call
   * because it's been set as the echo ref. */
  configure_ext_dsp_module(default_ext_);
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(1, process_reverse_mock_called);

  // Pretend that user changes the default output to another device.
  iodev_list_get_first_enabled_iodev_ret = &output2;
  device_enabled_callback_val(&unused_odev, NULL);
  EXPECT_EQ(2, cras_iodev_set_ext_dsp_module_called);
  EXPECT_NE((void*)NULL, ext_dsp_module_value[0]);
  EXPECT_EQ(default_ext_, ext_dsp_module_value[0]);
  // Verify dev changed callback is triggered accordingly.
  EXPECT_EQ(1, output_devices_changed_mock_called);
  cras_apm_reverse_state_update();

  // should NOT trigger
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(1, process_reverse_mock_called);

  // Unset aec ref so it should go back to track system default.
  default_process_reverse_needed_ret = 1;
  cras_apm_reverse_link_echo_ref(stream, NULL);
  num_fake_requested_echo_refs = 0;
  cras_apm_reverse_state_update();
  EXPECT_EQ(3, cras_iodev_set_ext_dsp_module_called);
  default_ext_->run(default_ext_, 500);
  EXPECT_EQ(2, process_reverse_mock_called);
}

TEST_F(EchoRefTestSuite, SetAecRefForMultipleApms) {
  struct cras_stream_apm* stream2 =
      reinterpret_cast<struct cras_stream_apm*>(0x456);
  cras_apm_reverse_link_echo_ref(stream, &output2);
  EXPECT_EQ(1, cras_iodev_set_ext_dsp_module_called);
  cras_apm_reverse_link_echo_ref(stream, NULL);
  EXPECT_EQ(2, cras_iodev_set_ext_dsp_module_called);

  cras_apm_reverse_link_echo_ref(stream, &output2);
  EXPECT_EQ(3, cras_iodev_set_ext_dsp_module_called);
  cras_apm_reverse_link_echo_ref(stream2, &output2);
  EXPECT_EQ(3, cras_iodev_set_ext_dsp_module_called);
  cras_apm_reverse_link_echo_ref(stream, NULL);
  // list2 is still using output2 as echo ref, expect no more call to unset.
  EXPECT_EQ(3, cras_iodev_set_ext_dsp_module_called);
  cras_apm_reverse_link_echo_ref(stream2, NULL);
  EXPECT_EQ(4, cras_iodev_set_ext_dsp_module_called);
}

/*
 * - System default on A
 * - Set aec ref to B
 * - Notify B is removed
 */
TEST_F(EchoRefTestSuite, SetAecRefThenRemoveDev) {
  cras_apm_reverse_link_echo_ref(stream, &output2);
  fake_requested_echo_refs[num_fake_requested_echo_refs++] = &output2;
  cras_apm_reverse_state_update();
  EXPECT_EQ(1, cras_iodev_set_ext_dsp_module_called);
  EXPECT_NE(default_ext_, ext_dsp_module_value[0]);

  configure_ext_dsp_module(ext_dsp_module_value[0]);
  ext_dsp_module_value[0]->run(ext_dsp_module_value[0], 500);
  EXPECT_EQ(1, process_reverse_mock_called);

  device_removed_callback_val(&output2);
  EXPECT_EQ(2, cras_iodev_set_ext_dsp_module_called);
  EXPECT_EQ((void*)NULL, ext_dsp_module_value[1]);
}

TEST_F(EchoRefTestSuite, ApmProcessReverseDataWithChannelsExceedingLimit) {
  const int nframes = 500;
  const int claimed_channels = MAX_EXT_DSP_PORTS * 2;

  default_ext_->configure(default_ext_, nframes, claimed_channels, 48000);

  for (int c = 0; c < MAX_EXT_DSP_PORTS; ++c) {
    default_ext_->ports[c] = (float*)calloc(nframes, sizeof(float));
  }

  default_process_reverse_needed_ret = 1;
  cras_apm_reverse_state_update();

  default_ext_->run(default_ext_, nframes);

  for (int c = 0; c < MAX_EXT_DSP_PORTS; ++c) {
    free(default_ext_->ports[c]);
  }

  EXPECT_EQ(1, process_reverse_mock_called);
}

extern "C" {
int cras_iodev_list_set_device_enabled_callback(
    device_enabled_callback_t enabled_cb,
    device_disabled_callback_t disabled_cb,
    device_removed_callback_t removed_cb,
    void* cb_data) {
  device_enabled_callback_val = enabled_cb;
  device_removed_callback_val = removed_cb;
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
bool cras_iodev_is_tuned_aec_use_case(const struct cras_ionode* node) {
  return cras_iodev_is_tuned_aec_use_case_ret;
}

bool cras_system_get_hw_echo_ref_disabled() {
  return false;
}
}  // extern "C"
}  // namespace
