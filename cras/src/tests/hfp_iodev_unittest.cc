/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>

extern "C" {
#include "cras_audio_area.h"
#include "cras_hfp_iodev.h"
#include "cras_hfp_slc.h"
#include "cras_iodev.h"
#include "cras_sco.h"
}

static struct cras_iodev* iodev;
static struct cras_bt_device* fake_device;
static struct hfp_slc_handle* fake_slc;
static struct cras_sco* fake_sco;
struct cras_audio_format fake_format;
static size_t cras_bt_device_append_iodev_called;
static size_t cras_bt_device_rm_iodev_called;
static size_t cras_iodev_add_node_called;
static size_t cras_iodev_rm_node_called;
static size_t cras_iodev_set_active_node_called;
static size_t cras_iodev_free_format_called;
static size_t cras_iodev_free_resources_called;
static size_t cras_bt_device_sco_connect_called;
static int cras_bt_transport_sco_connect_return_val;
static size_t cras_sco_add_iodev_called;
static size_t cras_sco_rm_iodev_called;
static size_t cras_sco_running_called;
static int cras_sco_running_return_val;
static size_t cras_sco_has_iodev_called;
static int cras_sco_has_iodev_return_val;
static size_t cras_sco_start_called;
static size_t cras_sco_stop_called;
static size_t cras_sco_acquire_called;
static unsigned cras_sco_acquire_return_val;
static size_t cras_sco_buf_release_called;
static unsigned cras_sco_buf_release_nwritten_val;
static size_t cras_sco_fill_output_with_zeros_called;
static size_t cras_sco_forceoutput_level_called;
static size_t cras_sco_forceoutput_level_target;
static size_t fake_buffer_size = 500;
static cras_audio_area* mock_audio_area;

void ResetStubData() {
  cras_bt_device_append_iodev_called = 0;
  cras_bt_device_rm_iodev_called = 0;
  cras_iodev_add_node_called = 0;
  cras_iodev_rm_node_called = 0;
  cras_iodev_set_active_node_called = 0;
  cras_iodev_free_format_called = 0;
  cras_iodev_free_resources_called = 0;
  cras_bt_device_sco_connect_called = 0;
  cras_bt_transport_sco_connect_return_val = 0;
  cras_sco_add_iodev_called = 0;
  cras_sco_rm_iodev_called = 0;
  cras_sco_running_called = 0;
  cras_sco_running_return_val = 1;
  cras_sco_has_iodev_called = 0;
  cras_sco_has_iodev_return_val = 0;
  cras_sco_start_called = 0;
  cras_sco_stop_called = 0;
  cras_sco_acquire_called = 0;
  cras_sco_acquire_return_val = 0;
  cras_sco_buf_release_called = 0;
  cras_sco_buf_release_nwritten_val = 0;
  cras_sco_fill_output_with_zeros_called = 0;
  cras_sco_forceoutput_level_called = 0;
  cras_sco_forceoutput_level_target = 0;

  fake_sco = reinterpret_cast<struct cras_sco*>(0x123);

  if (!mock_audio_area) {
    mock_audio_area = (cras_audio_area*)calloc(
        1, sizeof(*mock_audio_area) + sizeof(cras_channel_area) * 2);
  }
}

namespace {

class HfpIodev : public testing::Test {
 protected:
  virtual void SetUp() { ResetStubData(); }

  virtual void TearDown() {
    free(mock_audio_area);
    mock_audio_area = NULL;
  }
};

TEST_F(HfpIodev, CreateHfpOutputIodev) {
  iodev = hfp_iodev_create(CRAS_STREAM_OUTPUT, fake_device, fake_slc, fake_sco);

  ASSERT_EQ(CRAS_STREAM_OUTPUT, iodev->direction);
  ASSERT_EQ(1, cras_bt_device_append_iodev_called);
  ASSERT_EQ(1, cras_iodev_add_node_called);
  ASSERT_EQ(1, cras_iodev_set_active_node_called);

  hfp_iodev_destroy(iodev);

  ASSERT_EQ(1, cras_bt_device_rm_iodev_called);
  ASSERT_EQ(1, cras_iodev_rm_node_called);
  ASSERT_EQ(1, cras_iodev_free_resources_called);
}

TEST_F(HfpIodev, CreateHfpInputIodev) {
  iodev = hfp_iodev_create(CRAS_STREAM_INPUT, fake_device, fake_slc, fake_sco);

  ASSERT_EQ(CRAS_STREAM_INPUT, iodev->direction);
  ASSERT_EQ(1, cras_bt_device_append_iodev_called);
  ASSERT_EQ(1, cras_iodev_add_node_called);
  ASSERT_EQ(1, cras_iodev_set_active_node_called);
  /* Input device does not use software gain. */
  ASSERT_EQ(0, iodev->software_volume_needed);

  hfp_iodev_destroy(iodev);

  ASSERT_EQ(1, cras_bt_device_rm_iodev_called);
  ASSERT_EQ(1, cras_iodev_rm_node_called);
  ASSERT_EQ(1, cras_iodev_free_resources_called);
}

TEST_F(HfpIodev, OpenHfpIodev) {
  iodev = hfp_iodev_create(CRAS_STREAM_OUTPUT, fake_device, fake_slc, fake_sco);
  iodev->format = &fake_format;

  /* cras_sco* not start yet */
  cras_sco_running_return_val = 0;
  iodev->configure_dev(iodev);

  ASSERT_EQ(1, cras_bt_device_sco_connect_called);
  ASSERT_EQ(1, cras_sco_start_called);
  ASSERT_EQ(1, cras_sco_add_iodev_called);

  /* cras_sco* is running now */
  cras_sco_running_return_val = 1;

  iodev->close_dev(iodev);
  hfp_iodev_destroy(iodev);
  ASSERT_EQ(1, cras_sco_rm_iodev_called);
  ASSERT_EQ(1, cras_sco_stop_called);
  ASSERT_EQ(1, cras_iodev_free_format_called);
  ASSERT_EQ(1, cras_iodev_free_resources_called);
}

TEST_F(HfpIodev, OpenIodevWithHfpInfoAlreadyRunning) {
  iodev = hfp_iodev_create(CRAS_STREAM_INPUT, fake_device, fake_slc, fake_sco);

  iodev->format = &fake_format;

  /* cras_sco* already started by another device */
  cras_sco_running_return_val = 1;
  iodev->configure_dev(iodev);

  ASSERT_EQ(0, cras_bt_device_sco_connect_called);
  ASSERT_EQ(0, cras_sco_start_called);
  ASSERT_EQ(1, cras_sco_add_iodev_called);

  cras_sco_has_iodev_return_val = 1;
  iodev->close_dev(iodev);
  hfp_iodev_destroy(iodev);
  ASSERT_EQ(1, cras_sco_rm_iodev_called);
  ASSERT_EQ(0, cras_sco_stop_called);
  ASSERT_EQ(1, cras_iodev_free_format_called);
  ASSERT_EQ(1, cras_iodev_free_resources_called);
}

TEST_F(HfpIodev, PutGetBuffer) {
  cras_audio_area* area;
  unsigned frames;

  ResetStubData();
  iodev = hfp_iodev_create(CRAS_STREAM_OUTPUT, fake_device, fake_slc, fake_sco);
  iodev->format = &fake_format;
  iodev->configure_dev(iodev);

  cras_sco_acquire_return_val = 100;
  iodev->get_buffer(iodev, &area, &frames);

  ASSERT_EQ(1, cras_sco_acquire_called);
  ASSERT_EQ(100, frames);

  iodev->put_buffer(iodev, 40);
  ASSERT_EQ(1, cras_sco_buf_release_called);
  ASSERT_EQ(40, cras_sco_buf_release_nwritten_val);
  hfp_iodev_destroy(iodev);
  ASSERT_EQ(1, cras_iodev_free_resources_called);
}

TEST_F(HfpIodev, NoStreamState) {
  cras_audio_area* area;
  unsigned frames;

  ResetStubData();
  iodev = hfp_iodev_create(CRAS_STREAM_OUTPUT, fake_device, fake_slc, fake_sco);
  iodev->format = &fake_format;
  iodev->configure_dev(iodev);
  iodev->min_cb_level = iodev->buffer_size / 2;

  cras_sco_acquire_return_val = 100;
  iodev->get_buffer(iodev, &area, &frames);
  iodev->put_buffer(iodev, 100);

  iodev->no_stream(iodev, 1);
  ASSERT_EQ(1, cras_sco_fill_output_with_zeros_called);

  iodev->no_stream(iodev, 0);
  ASSERT_EQ(1, cras_sco_forceoutput_level_called);
  ASSERT_EQ(fake_buffer_size / 2, cras_sco_forceoutput_level_target);

  hfp_iodev_destroy(iodev);
}

}  // namespace

extern "C" {
void cras_iodev_free_format(struct cras_iodev* iodev) {
  cras_iodev_free_format_called++;
}

void cras_iodev_add_node(struct cras_iodev* iodev, struct cras_ionode* node) {
  cras_iodev_add_node_called++;
  iodev->nodes = node;
}

void cras_iodev_rm_node(struct cras_iodev* iodev, struct cras_ionode* node) {
  cras_iodev_rm_node_called++;
  iodev->nodes = NULL;
}

void cras_iodev_set_active_node(struct cras_iodev* iodev,
                                struct cras_ionode* node) {
  cras_iodev_set_active_node_called++;
  iodev->active_node = node;
}

// From ewma_power
void ewma_power_disable(struct ewma_power* ewma) {}

//  From system_state.
size_t cras_system_get_volume() {
  return 0;
}

// From bt device
int cras_bt_device_sco_connect(struct cras_bt_device* device, int codec) {
  cras_bt_device_sco_connect_called++;
  return cras_bt_transport_sco_connect_return_val;
}

const char* cras_bt_device_name(const struct cras_bt_device* device) {
  return "fake-device-name";
}

const char* cras_bt_device_address(const struct cras_bt_device* device) {
  return "1A:2B:3C:4D:5E:6F";
}

void cras_bt_device_append_iodev(struct cras_bt_device* device,
                                 struct cras_iodev* iodev,
                                 enum cras_bt_device_profile profile) {
  cras_bt_device_append_iodev_called++;
}

void cras_bt_device_rm_iodev(struct cras_bt_device* device,
                             struct cras_iodev* iodev) {
  cras_bt_device_rm_iodev_called++;
}

int cras_bt_device_sco_packet_size(struct cras_bt_device* device,
                                   int sco_socket,
                                   int codec) {
  return 48;
}
const char* cras_bt_device_object_path(const struct cras_bt_device* device) {
  return "/fake/object/path";
}

int cras_bt_device_get_stable_id(const struct cras_bt_device* device) {
  return 123;
}

// From cras_cras_sco*
int cras_sco_add_iodev(struct cras_sco* sco,
                       enum CRAS_STREAM_DIRECTION direction,
                       struct cras_audio_format* format) {
  cras_sco_add_iodev_called++;
  return 0;
}

int cras_sco_rm_iodev(struct cras_sco* sco,
                      enum CRAS_STREAM_DIRECTION direction) {
  cras_sco_rm_iodev_called++;
  return 0;
}

int cras_sco_has_iodev(struct cras_sco* sco) {
  cras_sco_has_iodev_called++;
  return cras_sco_has_iodev_return_val;
}

int cras_sco_running(struct cras_sco* sco) {
  cras_sco_running_called++;
  return cras_sco_running_return_val;
}

int cras_sco_start(unsigned int mtu, int codec, struct cras_sco* sco) {
  cras_sco_start_called++;
  return 0;
}

int cras_sco_stop(struct cras_sco* sco) {
  cras_sco_stop_called++;
  return 0;
}

int cras_sco_set_fd(struct cras_sco* sco, int fd) {
  return 0;
}
void cras_sco_close_fd(struct cras_sco* sco) {
  return;
}

int cras_sco_buf_queued(struct cras_sco* sco,
                        const enum CRAS_STREAM_DIRECTION direction) {
  return 0;
}

int cras_sco_buf_size(struct cras_sco* sco,
                      enum CRAS_STREAM_DIRECTION direction) {
  return fake_buffer_size;
}

void cras_sco_buf_acquire(struct cras_sco* sco,
                          enum CRAS_STREAM_DIRECTION direction,
                          uint8_t** buf,
                          unsigned* count) {
  cras_sco_acquire_called++;
  *count = cras_sco_acquire_return_val;
}

void cras_sco_buf_release(struct cras_sco* sco,
                          enum CRAS_STREAM_DIRECTION direction,
                          unsigned written_bytes) {
  cras_sco_buf_release_called++;
  cras_sco_buf_release_nwritten_val = written_bytes;
}

int cras_sco_fill_output_with_zeros(struct cras_sco* sco,
                                    unsigned int nframes) {
  cras_sco_fill_output_with_zeros_called++;
  return 0;
}

void cras_sco_force_output_level(struct cras_sco* sco, unsigned int level) {
  cras_sco_forceoutput_level_called++;
  cras_sco_forceoutput_level_target = level;
}

void cras_iodev_init_audio_area(struct cras_iodev* iodev, int num_channels) {
  iodev->area = mock_audio_area;
}

void cras_iodev_free_audio_area(struct cras_iodev* iodev) {}

void cras_iodev_free_resources(struct cras_iodev* iodev) {
  cras_iodev_free_resources_called++;
}

int cras_iodev_fill_odev_zeros(struct cras_iodev* odev, unsigned int frames) {
  return 0;
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area* area,
                                         const struct cras_audio_format* fmt,
                                         uint8_t* base_buffer) {
  mock_audio_area->channels[0].buf = base_buffer;
}

int hfp_set_call_status(struct hfp_slc_handle* handle, int call) {
  return 0;
}

int hfp_event_speaker_gain(struct hfp_slc_handle* handle, int gain) {
  return 0;
}

int hfp_slc_get_selected_codec(struct hfp_slc_handle* handle) {
  return HFP_CODEC_ID_CVSD;
}

bool hfp_slc_get_wideband_speech_supported(struct hfp_slc_handle* handle) {
  return false;
}

int hfp_slc_codec_connection_setup(struct hfp_slc_handle* handle) {
  return 0;
}

}  // extern "C"

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
