// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_apm_list.h"
#include "cras_audio_area.h"
#include "cras_types.h"
#include "webrtc_apm.h"
}

namespace {

static void *stream_ptr = reinterpret_cast<void *>(0x123);
static void *dev_ptr = reinterpret_cast<void *>(0x345);
static void *dev_ptr2 = reinterpret_cast<void *>(0x678);
static struct cras_apm_list *list;

TEST(ApmList, ApmListCreate) {
  list = cras_apm_list_create(stream_ptr, 0);
  EXPECT_EQ((void *)NULL, list);

  list = cras_apm_list_create(stream_ptr, APM_ECHO_CANCELLATION);
  EXPECT_NE((void *)NULL, list);
  EXPECT_EQ(APM_ECHO_CANCELLATION, cras_apm_list_get_effects(list));

  cras_apm_list_destroy(list);
}

TEST(ApmList, AddRemoveApm) {
  struct cras_audio_format fmt;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;

  list = cras_apm_list_create(stream_ptr, APM_ECHO_CANCELLATION);
  EXPECT_NE((void *)NULL, list);

  EXPECT_NE((void *)NULL, cras_apm_list_add(list, dev_ptr, &fmt));
  EXPECT_EQ((void *)NULL, cras_apm_list_get(list, dev_ptr2));

  EXPECT_NE((void *)NULL, cras_apm_list_add(list, dev_ptr2, &fmt));
  EXPECT_NE((void *)NULL, cras_apm_list_get(list, dev_ptr));

  cras_apm_list_remove(list, dev_ptr);
  EXPECT_EQ((void *)NULL, cras_apm_list_get(list, dev_ptr));
  EXPECT_NE((void *)NULL, cras_apm_list_get(list, dev_ptr2));

  cras_apm_list_remove(list, dev_ptr2);
  EXPECT_EQ((void *)NULL, cras_apm_list_get(list, dev_ptr2));

  cras_apm_list_destroy(list);
}

extern "C" {

webrtc_apm webrtc_apm_create(unsigned int num_channels,
			     unsigned int frame_rate,
			     unsigned int enable_echo_cancellation)
{
  return reinterpret_cast<webrtc_apm>(0x11);
}
void webrtc_apm_destroy(webrtc_apm apm)
{
  return;
}

} // extern "C"
} // namespace


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
