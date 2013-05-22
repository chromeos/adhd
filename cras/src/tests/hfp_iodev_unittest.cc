/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>

extern "C" {
#include "cras_hfp_iodev.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
}

static struct cras_iodev *iodev;
static struct cras_bt_transport *fake_transport;
static size_t cras_iodev_list_add_output_called;
static size_t cras_iodev_list_rm_output_called;
static size_t cras_iodev_list_add_input_called;
static size_t cras_iodev_list_rm_input_called;
static size_t cras_iodev_add_node_called;
static size_t cras_iodev_rm_node_called;
static size_t cras_iodev_set_active_node_called;
static size_t cras_iodev_free_format_called;

void ResetStubData() {
  cras_iodev_list_add_output_called = 0;
  cras_iodev_list_rm_output_called = 0;
  cras_iodev_list_add_input_called = 0;
  cras_iodev_list_rm_input_called = 0;
  cras_iodev_add_node_called = 0;
  cras_iodev_rm_node_called = 0;
  cras_iodev_set_active_node_called = 0;
  cras_iodev_free_format_called = 0;
}

namespace {

TEST(HfpIodev, CreateHfpIodev) {
  iodev = hfp_iodev_create(CRAS_STREAM_OUTPUT, fake_transport);

  ASSERT_EQ(CRAS_STREAM_OUTPUT, iodev->direction);
  ASSERT_EQ(1, cras_iodev_list_add_output_called);
  ASSERT_EQ(1, cras_iodev_add_node_called);
  ASSERT_EQ(1, cras_iodev_set_active_node_called);

  hfp_iodev_destroy(iodev);

  ASSERT_EQ(1, cras_iodev_list_rm_output_called);
  ASSERT_EQ(1, cras_iodev_rm_node_called);
}

} // namespace

extern "C" {
void cras_iodev_free_format(struct cras_iodev *iodev)
{
  cras_iodev_free_format_called++;
}

void cras_iodev_add_node(struct cras_iodev *iodev, struct cras_ionode *node)
{
  cras_iodev_add_node_called++;
  iodev->nodes = node;
}

void cras_iodev_rm_node(struct cras_iodev *iodev, struct cras_ionode *node)
{
  cras_iodev_rm_node_called++;
  iodev->nodes = NULL;
}

void cras_iodev_set_active_node(struct cras_iodev *iodev,
				struct cras_ionode *node)
{
  cras_iodev_set_active_node_called++;
  iodev->active_node = node;
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

int cras_iodev_list_add_input(struct cras_iodev *output)
{
  cras_iodev_list_add_input_called++;
  return 0;
}

int cras_iodev_list_rm_input(struct cras_iodev *dev)
{
  cras_iodev_list_rm_input_called++;
  return 0;
}

// From bt transport
const char *cras_bt_transport_object_path(
		const struct cras_bt_transport *transport)
{
  return NULL;
}

} // extern "C"

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
