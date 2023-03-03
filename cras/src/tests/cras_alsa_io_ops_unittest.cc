// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <map>
#include <stdio.h>
#include <syslog.h>
#include <vector>

#include "cras_util.h"

extern "C" {
#include "cras/src/server/cras_alsa_io_ops.h"
}

static cras_iodev* mock_iodev;
static int mock_create_called;
static int mock_legacy_complete_init_called;
static int mock_ucm_add_nodes_and_jacks_called;
static int mock_ucm_complete_init_called;
static int mock_destroy_called;
static int mock_index_called;
static int mock_has_hctl_jacks_called;

void ResetStubData() {
  mock_iodev = (cras_iodev*)1;
  mock_create_called = 0;
  mock_legacy_complete_init_called = 0;
  mock_ucm_add_nodes_and_jacks_called = 0;
  mock_ucm_complete_init_called = 0;
  mock_destroy_called = 0;
  mock_index_called = 0;
  mock_has_hctl_jacks_called = 0;
}

// Mock functions for testing.
static struct cras_iodev* mock_create(size_t card_index,
                                      const char* card_name,
                                      size_t device_index,
                                      const char* pcm_name,
                                      const char* dev_name,
                                      const char* dev_id,
                                      enum CRAS_ALSA_CARD_TYPE card_type,
                                      int is_first,
                                      struct cras_alsa_mixer* mixer,
                                      const struct cras_card_config* config,
                                      struct cras_use_case_mgr* ucm,
                                      snd_hctl_t* hctl,
                                      enum CRAS_STREAM_DIRECTION direction,
                                      size_t usb_vid,
                                      size_t usb_pid,
                                      char* usb_serial_number) {
  mock_create_called++;
  return mock_iodev;
}

static int mock_legacy_complete_init(struct cras_iodev* iodev) {
  mock_legacy_complete_init_called++;
  return 0;
}

static int mock_ucm_add_nodes_and_jacks(struct cras_iodev* iodev,
                                        struct ucm_section* section) {
  mock_ucm_add_nodes_and_jacks_called++;
  return 0;
}

static void mock_ucm_complete_init(struct cras_iodev* iodev) {
  mock_ucm_complete_init_called++;
}

static void mock_destroy(struct cras_iodev* iodev) {
  mock_destroy_called++;
}

static unsigned mock_index(struct cras_iodev* iodev) {
  mock_index_called++;
  return 0;
}

static int mock_has_hctl_jacks(struct cras_iodev* iodev) {
  mock_has_hctl_jacks_called++;
  return 0;
}

// Test case for cras_alsa_iodev_ops_create.
TEST(AlsaIodevOps, Create) {
  // Create a mock cras_alsa_iodev_ops struct with mock functions.
  struct cras_alsa_iodev_ops mock_ops = {
      .create = mock_create,
  };

  // Call the function under test with the mock struct.
  struct cras_iodev* result = cras_alsa_iodev_ops_create(
      &mock_ops, 0, "card_name", 0, "pcm_name", "dev_name", "dev_id",
      ALSA_CARD_TYPE_INTERNAL, 1, NULL, NULL, NULL, NULL, CRAS_STREAM_OUTPUT, 0,
      0, NULL);

  // Verify the result.
  EXPECT_EQ(result, mock_iodev);
  ASSERT_EQ(mock_create_called, 1);
}

// Test case for cras_alsa_iodev_ops_legacy_complete_init.
TEST(AlsaIodevOps, LegacyCompleteInit) {
  // Create a mock cras_alsa_iodev_ops struct with mock functions.
  struct cras_alsa_iodev_ops mock_ops = {
      .legacy_complete_init = mock_legacy_complete_init,
  };

  // Create a mock cras_iodev.
  struct cras_iodev mock_iodev;

  // Call the function under test with the mock struct and iodev.
  int result = cras_alsa_iodev_ops_legacy_complete_init(&mock_ops, &mock_iodev);

  // Verify the result.
  ASSERT_EQ(result, 0);
  ASSERT_EQ(mock_legacy_complete_init_called, 1);
}

// Test case for cras_alsa_iodev_ops_ucm_add_nodes_and_jacks.
TEST(AlsaIodevOps, UcmAddNodesAndJacks) {
  // Create a mock cras_alsa_iodev_ops struct with mock functions.
  struct cras_alsa_iodev_ops mock_ops = {
      .ucm_add_nodes_and_jacks = mock_ucm_add_nodes_and_jacks,
  };

  // Create a mock cras_iodev and ucm_section.
  struct cras_iodev mock_iodev;
  struct ucm_section mock_section;
  int result = cras_alsa_iodev_ops_ucm_add_nodes_and_jacks(
      &mock_ops, &mock_iodev, &mock_section);

  // Verify the result.
  ASSERT_EQ(result, 0);
  ASSERT_EQ(mock_ucm_add_nodes_and_jacks_called, 1);
}

// Test case for cras_alsa_iodev_ops_ucm_complete_init.
TEST(AlsaIodevOps, UcmCompleteInit) {
  // Initialize a mock alsa iodev ops and cras iodev.
  struct cras_alsa_iodev_ops mock_ops = {.ucm_complete_init =
                                             mock_ucm_complete_init};
  struct cras_iodev mock_iodev;

  // Call the function with the mock ops and iodev.
  cras_alsa_iodev_ops_ucm_complete_init(&mock_ops, &mock_iodev);

  // Verify that the mock function was called once.
  ASSERT_EQ(mock_ucm_complete_init_called, 1);
}

// Test case for cras_alsa_iodev_ops_destroy.
TEST(AlsaIodevOps, Destroy) {
  // Initialize a mock alsa iodev ops and cras iodev.
  struct cras_alsa_iodev_ops mock_ops = {.destroy = mock_destroy};
  struct cras_iodev mock_iodev;

  // Call the function with the mock ops and iodev.
  cras_alsa_iodev_ops_destroy(&mock_ops, &mock_iodev);

  // Verify that the mock function was called once.
  ASSERT_EQ(mock_destroy_called, 1);
}

// Test case for cras_alsa_iodev_ops_has_hctl_jacks.
TEST(AlsaIodevOps, HasHctlJacks) {
  // Initialize a mock alsa iodev ops and cras iodev.
  struct cras_alsa_iodev_ops mock_ops = {.has_hctl_jacks = mock_has_hctl_jacks};
  struct cras_iodev mock_iodev;

  // Call the function with the mock ops and iodev.
  int has_hctl_jacks =
      cras_alsa_iodev_ops_has_hctl_jacks(&mock_ops, &mock_iodev);

  // Verify that the mock function was called once and returned 0.
  ASSERT_EQ(mock_has_hctl_jacks_called, 1);
  ASSERT_EQ(has_hctl_jacks, 0);
}

// Test case for cras_alsa_iodev_ops_index.
TEST(AlsaIodevOps, Index) {
  // Initialize a mock alsa iodev ops and cras iodev.
  struct cras_alsa_iodev_ops mock_ops = {.index = mock_index};
  struct cras_iodev mock_iodev;

  // Call the function with the mock ops and iodev.
  unsigned index = cras_alsa_iodev_ops_index(&mock_ops, &mock_iodev);

  // Verify that the mock function was called once and returned 0.
  ASSERT_EQ(mock_index_called, 1);
  ASSERT_EQ(index, 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  openlog(NULL, LOG_PERROR, LOG_USER);
  return RUN_ALL_TESTS();
}
