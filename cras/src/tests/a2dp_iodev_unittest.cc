// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdint.h>
#include <gtest/gtest.h>

extern "C" {

#include "a2dp-codecs.h"
#include "cras_bt_transport.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"

#include "cras_a2dp_iodev.h"
}

static struct cras_bt_transport *fake_transport;
static cras_audio_format *fake_format;
static size_t cras_iodev_list_add_output_called;
static size_t cras_iodev_list_rm_output_called;
static size_t cras_bt_transport_acquire_called;
static size_t cras_bt_transport_configuration_called;
static size_t init_a2dp_called;
static size_t destroy_a2dp_called;
static size_t cras_iodev_free_format_called;

void ResetStubData() {
  cras_iodev_list_add_output_called = 0;
  cras_iodev_list_rm_output_called = 0;
  cras_bt_transport_acquire_called = 0;
  cras_bt_transport_configuration_called = 0;
  init_a2dp_called = 0;
  destroy_a2dp_called = 0;
  cras_iodev_free_format_called = 0;

  fake_transport = reinterpret_cast<struct cras_bt_transport *>(0x123);
}

namespace {

TEST(A2dpIoInit, InitializeA2dpIodev) {
  struct cras_iodev *iodev;

  ResetStubData();

  iodev = a2dp_iodev_create(fake_transport);

  ASSERT_NE(iodev, (void *)NULL);
  ASSERT_EQ(iodev->direction, CRAS_STREAM_OUTPUT);
  ASSERT_EQ(1, cras_bt_transport_configuration_called);
  ASSERT_EQ(1, init_a2dp_called);
  ASSERT_EQ(1, cras_iodev_list_add_output_called);

  a2dp_iodev_destroy(iodev);

  ASSERT_EQ(1, cras_iodev_list_rm_output_called);
  ASSERT_EQ(1, destroy_a2dp_called);
}

TEST(A2dpIoInit, OpenIodev) {
  struct cras_iodev *iodev;
  struct cras_audio_format *format = NULL;

  ResetStubData();
  iodev = a2dp_iodev_create(fake_transport);

  cras_iodev_set_format(iodev, format);
  iodev->open_dev(iodev);

  ASSERT_EQ(1, cras_bt_transport_acquire_called);

  iodev->close_dev(iodev);
  ASSERT_EQ(1, cras_iodev_free_format_called);

  a2dp_iodev_destroy(iodev);
  free(fake_format);
}

} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

extern "C" {

int cras_bt_transport_configuration(const struct cras_bt_transport *transport,
                                    void *configuration, int len)
{
  cras_bt_transport_configuration_called++;
  return 0;
}

int cras_bt_transport_acquire(struct cras_bt_transport *transport)
{
  cras_bt_transport_acquire_called++;
  return 0;
}

int cras_bt_transport_release(struct cras_bt_transport *transport)
{
  return 0;
}

int cras_bt_transport_fd(const struct cras_bt_transport *transport)
{
  return 0;
}

const char *cras_bt_transport_object_path(
		const struct cras_bt_transport *transport)
{
  return NULL;
}

void cras_iodev_free_format(struct cras_iodev *iodev)
{
  cras_iodev_free_format_called++;
}

// Cras iodev
int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt)
{
  fake_format = (struct cras_audio_format *)malloc(sizeof(*fake_format));
  iodev->format = fake_format;
  return 0;
}

//  From iodev list.
int cras_iodev_list_add_output(struct cras_iodev *output)
{
  cras_iodev_list_add_output_called++;
  return 0;
}

int cras_iodev_list_rm_output(struct cras_iodev *dev)
{
  cras_iodev_list_rm_output_called++;
  return 0;
}

void init_a2dp(struct cras_audio_codec *codec, a2dp_sbc_t *sbc)
{
  init_a2dp_called++;
}

void destroy_a2dp(struct cras_audio_codec *codec)
{
  destroy_a2dp_called++;
}

}
