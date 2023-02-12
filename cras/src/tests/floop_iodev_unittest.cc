// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "cras/src/server/cras_floop_iodev.c"
}

#include <gtest/gtest.h>

TEST(FlexibleLoopback, PointerArithmetic) {
  struct flexible_loopback floop = {};
  EXPECT_EQ(&floop, const_pair_to_floop(&floop.pair));
  EXPECT_EQ(&floop, input_to_floop(&floop.pair.input));
  EXPECT_EQ(&floop, output_to_floop(&floop.pair.output));
}

TEST(FlexibleLoopback, CrasFloopPairMatchOutputStream) {
  struct flexible_loopback floop = {
      .params = {.client_types_mask = 1 << CRAS_CLIENT_TYPE_CHROME}};

  struct cras_rstream test_stream = {.client_type = CRAS_CLIENT_TYPE_TEST};
  struct cras_rstream chrome_stream = {.client_type = CRAS_CLIENT_TYPE_CHROME};

  EXPECT_FALSE(cras_floop_pair_match_output_stream(&floop.pair, &test_stream))
      << "should not match: different mask, floop is not active";
  EXPECT_FALSE(cras_floop_pair_match_output_stream(&floop.pair, &chrome_stream))
      << "should not match: floop is not active";

  floop.input_active = true;

  EXPECT_FALSE(cras_floop_pair_match_output_stream(&floop.pair, &test_stream))
      << "should not match: different mask";
  EXPECT_TRUE(cras_floop_pair_match_output_stream(&floop.pair, &chrome_stream))
      << "should match: floop active and matching mask";
}

TEST(FlexibleLoopback, CreateDestroy) {
  struct cras_floop_params params = {.client_types_mask = 0};
  struct cras_floop_pair* floop = cras_floop_pair_create(&params);
  cras_floop_pair_destroy(floop);
}

// Stubs
extern "C" {
void cras_iodev_set_active_node(struct cras_iodev* iodev,
                                struct cras_ionode* node) {
  iodev->active_node = node;
}

void cras_iodev_add_node(struct cras_iodev* iodev, struct cras_ionode* node) {
  iodev->nodes = node;
}

void cras_iodev_init_audio_area(struct cras_iodev* iodev, int num_channels) {}

void cras_iodev_free_audio_area(struct cras_iodev* iodev) {}

int cras_iodev_default_no_stream_playback(struct cras_iodev* odev, int enable) {
  return 0;
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area* area,
                                         const struct cras_audio_format* fmt,
                                         uint8_t* base_buffer) {}

int cras_iodev_list_add_input(struct cras_iodev* input) {
  return 0;
}
int cras_iodev_list_rm_input(struct cras_iodev* input) {
  return 0;
}

int cras_iodev_list_add_output(struct cras_iodev* output) {
  return 0;
}
int cras_iodev_list_rm_output(struct cras_iodev* output) {
  return 0;
}

void cras_iodev_list_enable_floop_pair(struct cras_floop_pair* pair) {}

void cras_iodev_list_disable_floop_pair(struct cras_floop_pair* pair) {}
}
