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
static cras_audio_format format;
static size_t cras_iodev_list_add_output_called;
static size_t cras_iodev_list_rm_output_called;
static size_t cras_bt_transport_acquire_called;
static size_t cras_bt_transport_configuration_called;
static size_t cras_bt_transport_release_called;
static size_t init_a2dp_called;
static int init_a2dp_return_val;
static size_t destroy_a2dp_called;
static size_t drain_a2dp_called;
static size_t cras_iodev_free_format_called;
static int pcm_buf_size_val;
static unsigned int a2dp_write_processed_bytes_val;

void ResetStubData() {
  cras_iodev_list_add_output_called = 0;
  cras_iodev_list_rm_output_called = 0;
  cras_bt_transport_acquire_called = 0;
  cras_bt_transport_configuration_called = 0;
  cras_bt_transport_release_called = 0;
  init_a2dp_called = 0;
  init_a2dp_return_val = 0;
  destroy_a2dp_called = 0;
  drain_a2dp_called = 0;
  cras_iodev_free_format_called = 0;
  a2dp_write_processed_bytes_val = 0;

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

TEST(A2dpIoInit, InitializeFail) {
  struct cras_iodev *iodev;

  ResetStubData();

  init_a2dp_return_val = -1;
  iodev = a2dp_iodev_create(fake_transport);

  ASSERT_EQ(iodev, (void *)NULL);
  ASSERT_EQ(1, cras_bt_transport_configuration_called);
  ASSERT_EQ(1, init_a2dp_called);
  ASSERT_EQ(0, cras_iodev_list_add_output_called);
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
  ASSERT_EQ(1, cras_bt_transport_release_called);
  ASSERT_EQ(1, drain_a2dp_called);
  ASSERT_EQ(1, cras_iodev_free_format_called);

  a2dp_iodev_destroy(iodev);
}

TEST(A2dpIoInit, GetPutBuffer) {
  struct cras_iodev *iodev;
  struct cras_audio_format *format = NULL;
  uint8_t *buf1, *buf2, *buf3;
  unsigned frames;

  ResetStubData();
  iodev = a2dp_iodev_create(fake_transport);

  cras_iodev_set_format(iodev, format);
  iodev->open_dev(iodev);

  iodev->get_buffer(iodev, &buf1, &frames);
  ASSERT_EQ(256, frames);

  /* Test 100 frames(400 bytes) put and all processed. */
  a2dp_write_processed_bytes_val = 400;
  iodev->put_buffer(iodev, 100);
  ASSERT_EQ(400, pcm_buf_size_val);

  iodev->get_buffer(iodev, &buf2, &frames);
  ASSERT_EQ(256, frames);

  /* Assert buf2 points to the same position as buf1 */
  ASSERT_EQ(0, buf2 - buf1);

  /* Test 100 frames(400 bytes) put, only 360 bytes processed,
   * 40 bytes left in pcm buffer.
   */
  a2dp_write_processed_bytes_val = 360;
  iodev->put_buffer(iodev, 100);
  ASSERT_EQ(400, pcm_buf_size_val);

  iodev->get_buffer(iodev, &buf3, &frames);

  /* Existing buffer not completed processed, assert new buffer starts from
   * current write pointer.
   */
  ASSERT_EQ(156, frames);
  ASSERT_EQ(400, buf3 - buf1);

  a2dp_iodev_destroy(iodev);
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
  cras_bt_transport_release_called++;
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

uint16_t cras_bt_transport_write_mtu(const struct cras_bt_transport *transport)
{
  return 1;
}

void cras_iodev_free_format(struct cras_iodev *iodev)
{
  cras_iodev_free_format_called++;
}

// Cras iodev
int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt)
{
  format.format = SND_PCM_FORMAT_S16_LE;
  format.num_channels = 2;
  iodev->format = &format;
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

int init_a2dp(struct a2dp_info *a2dp, a2dp_sbc_t *sbc)
{
  init_a2dp_called++;
  return init_a2dp_return_val;
}

void destroy_a2dp(struct a2dp_info *a2dp)
{
  destroy_a2dp_called++;
}

void a2dp_drain(struct a2dp_info *a2dp)
{
  drain_a2dp_called++;
}

unsigned int a2dp_write(void *pcm_buf, int pcm_buf_size, struct a2dp_info *a2dp,
			int format_bytes, int stream_fd, size_t link_mtu,
			int *written_bytes)
{
  pcm_buf_size_val = pcm_buf_size;
  return a2dp_write_processed_bytes_val;
}

}
