/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>
#include <string.h>

#include "cras/src/server/cras_hfp_alsa_iodev.h"
#include "cras/src/server/cras_hfp_slc.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/tests/sr_bt_util_stub.h"
#include "cras_audio_format.h"

struct hfp_alsa_io {
  struct cras_iodev base;
  struct cras_iodev* aio;
  struct cras_bt_device* device;
  struct hfp_slc_handle* slc;
  struct cras_hfp* hfp;
};

static struct cras_sco* fake_sco;
static struct cras_iodev fake_sco_out, fake_sco_in;
static struct cras_bt_device* fake_device;
static struct cras_hfp* fake_hfp;
static struct hfp_slc_handle* fake_slc;
static struct cras_audio_format fake_format;

static size_t cras_bt_device_append_iodev_called;
static size_t cras_bt_device_rm_iodev_called;
static size_t cras_iodev_add_node_called;
static size_t cras_iodev_rm_node_called;
static size_t cras_iodev_set_active_node_called;
static size_t cras_iodev_free_format_called;
static size_t cras_iodev_free_resources_called;
static size_t cras_iodev_set_format_called;
static size_t hfp_set_call_status_called;
static size_t hfp_event_speaker_gain_called;
static int hfp_slc_get_selected_codec_return_val;
static bool cras_floss_hfp_is_codec_format_supported_ret;
static enum HFP_CODEC_FORMAT cras_floss_hfp_get_active_codec_format_ret;
static int cras_iodev_sr_bt_adapter_create_called;
static int cras_iodev_sr_bt_adapter_destroy_called;
static int cras_iodev_sr_bt_adapter_frames_queued_called;
static int cras_iodev_sr_bt_adapter_delay_frames_called;
static int cras_iodev_sr_bt_adapter_get_buffer_called;
static int cras_iodev_sr_bt_adapter_put_buffer_called;
static int cras_iodev_sr_bt_adapter_flush_buffer_called;

static size_t fake_open_dev_called;
static int fake_open_dev(struct cras_iodev* iodev) {
  fake_open_dev_called++;
  return 0;
}
static size_t fake_update_supported_formats_called;
static int fake_update_supported_formats(struct cras_iodev* iodev) {
  fake_update_supported_formats_called++;
  return 0;
}
static size_t fake_configure_dev_called;
static int fake_configure_dev(struct cras_iodev* iodev) {
  fake_configure_dev_called++;
  return 0;
}
static size_t fake_close_dev_called;
static int fake_close_dev(struct cras_iodev* iodev) {
  fake_close_dev_called++;
  return 0;
}
static size_t fake_output_underrun_called;
static int fake_output_underrun(struct cras_iodev* iodev) {
  fake_output_underrun_called++;
  return 0;
}
static size_t fake_frames_queued_called;
static int fake_frames_queued(const struct cras_iodev* iodev,
                              struct timespec* tstamp) {
  fake_frames_queued_called++;
  return 0;
}
static size_t fake_delay_frames_called;
static int fake_delay_frames(const struct cras_iodev* iodev) {
  fake_delay_frames_called++;
  return 0;
}
static size_t fake_get_buffer_called;
static int fake_get_buffer(struct cras_iodev* iodev,
                           struct cras_audio_area** area,
                           unsigned* frames) {
  fake_get_buffer_called++;
  return 0;
}
static size_t fake_put_buffer_called;
static int fake_put_buffer(struct cras_iodev* iodev, unsigned nwritten) {
  fake_put_buffer_called++;
  return 0;
}
static size_t fake_flush_buffer_called;
static int fake_flush_buffer(struct cras_iodev* iodev) {
  fake_flush_buffer_called++;
  return 0;
}
static size_t fake_update_active_node_called;
static void fake_update_active_node(struct cras_iodev* iodev,
                                    unsigned node_idx,
                                    unsigned dev_enabled) {
  fake_update_active_node_called++;
}
static size_t fake_start_called;
static int fake_start(struct cras_iodev* iodev) {
  fake_start_called++;
  return 0;
}
static size_t fake_no_stream_called;
static int fake_no_stream(struct cras_iodev* iodev, int enable) {
  fake_no_stream_called++;
  return 0;
}
static size_t fake_is_free_running_called;
static int fake_is_free_running(const struct cras_iodev* iodev) {
  fake_is_free_running_called++;
  return 0;
}
static size_t fake_get_valid_frames_called;
static int fake_get_valid_frames(struct cras_iodev* odev,
                                 struct timespec* tstamp) {
  fake_get_valid_frames_called++;
  return 0;
}

static void ResetStubData() {
  cras_bt_device_append_iodev_called = 0;
  cras_bt_device_rm_iodev_called = 0;
  cras_iodev_add_node_called = 0;
  cras_iodev_rm_node_called = 0;
  cras_iodev_set_active_node_called = 0;
  cras_iodev_free_format_called = 0;
  cras_iodev_free_resources_called = 0;
  cras_iodev_set_format_called = 0;
  hfp_set_call_status_called = 0;
  hfp_event_speaker_gain_called = 0;
  hfp_slc_get_selected_codec_return_val = HFP_CODEC_ID_CVSD;
  cras_floss_hfp_is_codec_format_supported_ret = false;
  cras_floss_hfp_get_active_codec_format_ret = HFP_CODEC_FORMAT_NONE;
  cras_iodev_sr_bt_adapter_create_called = 0;
  cras_iodev_sr_bt_adapter_destroy_called = 0;
  cras_iodev_sr_bt_adapter_frames_queued_called = 0;
  cras_iodev_sr_bt_adapter_delay_frames_called = 0;
  cras_iodev_sr_bt_adapter_get_buffer_called = 0;
  cras_iodev_sr_bt_adapter_put_buffer_called = 0;
  cras_iodev_sr_bt_adapter_flush_buffer_called = 0;

  fake_sco = reinterpret_cast<struct cras_sco*>(0x123);
  fake_device = reinterpret_cast<struct cras_bt_device*>(0x234);
  fake_slc = reinterpret_cast<struct hfp_slc_handle*>(0x345);
  fake_hfp = NULL;

  memset(&fake_sco_out, 0x00, sizeof(fake_sco_out));

  fake_sco_out.open_dev = fake_sco_in.open_dev = fake_open_dev;
  fake_open_dev_called = 0;

  fake_sco_out.update_supported_formats = fake_sco_in.update_supported_formats =
      fake_update_supported_formats;
  fake_update_supported_formats_called = 0;

  fake_sco_out.configure_dev = fake_sco_in.configure_dev = fake_configure_dev;
  fake_configure_dev_called = 0;

  fake_sco_out.close_dev = fake_sco_in.close_dev = fake_close_dev;
  fake_close_dev_called = 0;

  fake_sco_out.frames_queued = fake_sco_in.frames_queued = fake_frames_queued;
  fake_frames_queued_called = 0;

  fake_sco_out.delay_frames = fake_sco_in.delay_frames = fake_delay_frames;
  fake_delay_frames_called = 0;

  fake_sco_out.get_buffer = fake_sco_in.get_buffer = fake_get_buffer;
  fake_get_buffer_called = 0;

  fake_sco_out.put_buffer = fake_sco_in.put_buffer = fake_put_buffer;
  fake_put_buffer_called = 0;

  fake_sco_out.flush_buffer = fake_sco_in.flush_buffer = fake_flush_buffer;
  fake_flush_buffer_called = 0;

  fake_sco_out.update_active_node = fake_sco_in.update_active_node =
      fake_update_active_node;
  fake_update_active_node_called = 0;

  fake_sco_out.start = fake_sco_in.start = fake_start;
  fake_start_called = 0;

  fake_sco_out.no_stream = fake_sco_in.no_stream = fake_no_stream;
  fake_no_stream_called = 0;

  fake_sco_out.is_free_running = fake_sco_in.is_free_running =
      fake_is_free_running;
  fake_is_free_running_called = 0;

  fake_sco_out.output_underrun = fake_output_underrun;
  fake_output_underrun_called = 0;

  fake_sco_out.get_valid_frames = fake_get_valid_frames;
  fake_get_valid_frames_called = 0;
}

namespace {

class HfpAlsaIodev : public testing::Test {
 protected:
  virtual void SetUp() { ResetStubData(); }

  virtual void TearDown() {}
};

TEST_F(HfpAlsaIodev, CreateHfpAlsaOutputIodev) {
  struct cras_iodev* iodev;
  struct hfp_alsa_io* hfp_alsa_io;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  hfp_alsa_io = (struct hfp_alsa_io*)iodev;

  EXPECT_EQ(CRAS_STREAM_OUTPUT, iodev->direction);
  EXPECT_EQ(1, cras_bt_device_append_iodev_called);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_iodev_set_active_node_called);
  EXPECT_EQ(&fake_sco_out, hfp_alsa_io->aio);

  EXPECT_EQ(0, CRAS_BT_FLAG_FLOSS & iodev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_HFP, CRAS_BT_FLAG_HFP & iodev->active_node->btflags);

  hfp_alsa_iodev_destroy(iodev);

  EXPECT_EQ(1, cras_bt_device_rm_iodev_called);
  EXPECT_EQ(1, cras_iodev_rm_node_called);
  EXPECT_EQ(1, cras_iodev_free_resources_called);
}

TEST_F(HfpAlsaIodev, CreateHfpAlsaInputIodev) {
  struct cras_iodev* iodev;
  struct hfp_alsa_io* hfp_alsa_io;

  fake_sco_in.direction = CRAS_STREAM_INPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_in, fake_device, fake_slc, fake_sco,
                                NULL);
  hfp_alsa_io = (struct hfp_alsa_io*)iodev;

  EXPECT_EQ(CRAS_STREAM_INPUT, iodev->direction);
  EXPECT_EQ(1, cras_bt_device_append_iodev_called);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_iodev_set_active_node_called);
  EXPECT_EQ(&fake_sco_in, hfp_alsa_io->aio);
  // Input device does not use software gain.
  EXPECT_EQ(0, iodev->software_volume_needed);

  EXPECT_EQ(0, CRAS_BT_FLAG_FLOSS & iodev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_HFP, CRAS_BT_FLAG_HFP & iodev->active_node->btflags);

  hfp_alsa_iodev_destroy(iodev);

  EXPECT_EQ(1, cras_bt_device_rm_iodev_called);
  EXPECT_EQ(1, cras_iodev_rm_node_called);
  EXPECT_EQ(1, cras_iodev_free_resources_called);
}

TEST_F(HfpAlsaIodev, OpenDev) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->open_dev(iodev);

  EXPECT_EQ(1, fake_open_dev_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, UpdateSupportedFormat) {
  struct cras_iodev* iodev;
  size_t supported_rates[] = {8000, 0};
  size_t supported_channel_counts[] = {1, 0};
  snd_pcm_format_t supported_formats[] = {SND_PCM_FORMAT_S16_LE,
                                          (snd_pcm_format_t)0};

  fake_sco_out.supported_rates = supported_rates;
  fake_sco_out.supported_channel_counts = supported_channel_counts;
  fake_sco_out.supported_formats = supported_formats;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->update_supported_formats(iodev);

  // update_supported_format on alsa_io is not called.
  EXPECT_EQ(0, fake_update_supported_formats_called);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(supported_rates[i], iodev->supported_rates[i]);
    EXPECT_EQ(supported_channel_counts[i], iodev->supported_channel_counts[i]);
    EXPECT_EQ(supported_formats[i], iodev->supported_formats[i]);
  }

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, ConfigureDev) {
  struct cras_iodev* iodev;
  size_t buf_size = 8192;
  struct hfp_alsa_io* hfp_alsa_io;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  fake_sco_out.buffer_size = buf_size;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  hfp_alsa_io = (struct hfp_alsa_io*)iodev;
  iodev->format = &fake_format;
  iodev->supported_rates = (size_t*)calloc(2, sizeof(size_t));
  iodev->supported_rates[0] = 8000;

  iodev->configure_dev(iodev);

  EXPECT_EQ(fake_format.num_channels, hfp_alsa_io->aio->format->num_channels);
  EXPECT_EQ(fake_format.frame_rate, hfp_alsa_io->aio->format->frame_rate);
  EXPECT_EQ(fake_format.format, hfp_alsa_io->aio->format->format);
  for (int i = 0; i < CRAS_CH_MAX; i++) {
    EXPECT_EQ(fake_format.channel_layout[i],
              hfp_alsa_io->aio->format->channel_layout[i]);
  }

  EXPECT_EQ(1, fake_configure_dev_called);
  EXPECT_EQ(1, hfp_set_call_status_called);
  EXPECT_EQ(buf_size, iodev->buffer_size);

  iodev->close_dev(iodev);
  free(hfp_alsa_io->aio->format);  // aio->close_dev is fake
  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, CloseDev) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->close_dev(iodev);

  EXPECT_EQ(1, hfp_set_call_status_called);
  EXPECT_EQ(1, cras_iodev_free_format_called);
  EXPECT_EQ(1, fake_close_dev_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, FramesQueued) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->frames_queued(iodev, (struct timespec*)NULL);

  EXPECT_EQ(1, fake_frames_queued_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, DelayFrames) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->delay_frames(iodev);

  EXPECT_EQ(1, fake_delay_frames_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, GetBuffer) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->get_buffer(iodev, (struct cras_audio_area**)NULL, (unsigned*)NULL);

  EXPECT_EQ(1, fake_get_buffer_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, PutBuffer) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->put_buffer(iodev, 0xdeadbeef);

  EXPECT_EQ(1, fake_put_buffer_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, FlushBuffer) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->flush_buffer(iodev);

  EXPECT_EQ(1, fake_flush_buffer_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, UpdateActiveNode) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->update_active_node(iodev, 0xdeadbeef, 0xdeadbeef);

  EXPECT_EQ(1, fake_update_active_node_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, Start) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->start(iodev);

  EXPECT_EQ(1, fake_start_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, SetVolume) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->set_volume(iodev);

  EXPECT_EQ(1, hfp_event_speaker_gain_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, NoStream) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->min_cb_level = 0xab;
  iodev->max_cb_level = 0xcd;

  iodev->no_stream(iodev, 1);

  EXPECT_EQ(0xab, fake_sco_out.min_cb_level);
  EXPECT_EQ(0xcd, fake_sco_out.max_cb_level);
  EXPECT_EQ(1, fake_no_stream_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, IsFreeRunning) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->is_free_running(iodev);

  EXPECT_EQ(1, fake_is_free_running_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, OutputUnderrun) {
  struct cras_iodev* iodev;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);
  iodev->min_cb_level = 0xab;
  iodev->max_cb_level = 0xcd;

  iodev->output_underrun(iodev);

  EXPECT_EQ(0xab, fake_sco_out.min_cb_level);
  EXPECT_EQ(0xcd, fake_sco_out.max_cb_level);
  EXPECT_EQ(1, fake_output_underrun_called);

  hfp_alsa_iodev_destroy(iodev);
}

TEST_F(HfpAlsaIodev, GetValidFrames) {
  struct cras_iodev* iodev;
  struct timespec ts;

  fake_sco_out.direction = CRAS_STREAM_OUTPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_out, fake_device, fake_slc, fake_sco,
                                NULL);

  iodev->get_valid_frames(iodev, &ts);

  EXPECT_EQ(1, fake_get_valid_frames_called);

  hfp_alsa_iodev_destroy(iodev);
}

struct HfpAlsaIodevSrTestParam {
  bool is_cras_sr_enabled;
  enum HFP_CODEC_FORMAT active_codec_format;
  bool is_offload;
  enum CRAS_STREAM_DIRECTION direction;
  size_t expected_sample_rate;
};

class HfpAlsaIodevSrTest
    : public testing::TestWithParam<HfpAlsaIodevSrTestParam> {
 protected:
  virtual void SetUp() {
    ResetStubData();

    if (GetParam().is_cras_sr_enabled) {
      enable_cras_sr_bt();
    } else {
      disable_cras_sr_bt();
    }

    if (GetParam().active_codec_format == HFP_CODEC_FORMAT_MSBC) {
      hfp_slc_get_selected_codec_return_val = HFP_CODEC_ID_MSBC;
      cras_floss_hfp_get_active_codec_format_ret = HFP_CODEC_FORMAT_MSBC;
    } else {
      hfp_slc_get_selected_codec_return_val = HFP_CODEC_ID_CVSD;
      cras_floss_hfp_get_active_codec_format_ret = HFP_CODEC_FORMAT_CVSD;
    }

    if (GetParam().is_offload) {
      fake_device = NULL;
      fake_hfp = reinterpret_cast<struct cras_hfp*>(0x234);
    } else {
      fake_device = reinterpret_cast<struct cras_bt_device*>(0x234);
      fake_hfp = NULL;
    }
  }

  virtual void TearDown() { disable_cras_sr_bt(); }
};

TEST_P(HfpAlsaIodevSrTest, TestSampleRate) {
  const ParamType& param = GetParam();
  struct cras_iodev* iodev;

  fake_sco_in.direction = param.direction;
  iodev = hfp_alsa_iodev_create(&fake_sco_in, fake_device, fake_slc, fake_sco,
                                fake_hfp);

  iodev->open_dev(iodev);
  EXPECT_EQ(
      param.is_cras_sr_enabled && param.direction == CRAS_STREAM_INPUT ? 1 : 0,
      cras_iodev_sr_bt_adapter_create_called);

  iodev->update_supported_formats(iodev);
  EXPECT_EQ(param.expected_sample_rate, iodev->supported_rates[0]);
  EXPECT_EQ(NULL, iodev->supported_rates[1]);

  hfp_alsa_iodev_destroy(iodev);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    HfpAlsaIodevSrTest,
    testing::Values(
        HfpAlsaIodevSrTestParam({.is_cras_sr_enabled = false,
                                 .active_codec_format = HFP_CODEC_FORMAT_CVSD,
                                 .is_offload = false,
                                 .direction = CRAS_STREAM_INPUT,
                                 .expected_sample_rate = 8000}),
        HfpAlsaIodevSrTestParam({.is_cras_sr_enabled = false,
                                 .active_codec_format = HFP_CODEC_FORMAT_MSBC,
                                 .is_offload = false,
                                 .direction = CRAS_STREAM_INPUT,
                                 .expected_sample_rate = 16000}),
        HfpAlsaIodevSrTestParam({.is_cras_sr_enabled = false,
                                 .active_codec_format = HFP_CODEC_FORMAT_CVSD,
                                 .is_offload = true,
                                 .direction = CRAS_STREAM_INPUT,
                                 .expected_sample_rate = 8000}),
        HfpAlsaIodevSrTestParam({.is_cras_sr_enabled = false,
                                 .active_codec_format = HFP_CODEC_FORMAT_MSBC,
                                 .is_offload = true,
                                 .direction = CRAS_STREAM_INPUT,
                                 .expected_sample_rate = 16000}),
        // sr enabled
        HfpAlsaIodevSrTestParam({.is_cras_sr_enabled = true,
                                 .active_codec_format = HFP_CODEC_FORMAT_CVSD,
                                 .is_offload = false,
                                 .direction = CRAS_STREAM_INPUT,
                                 .expected_sample_rate = 24000}),
        HfpAlsaIodevSrTestParam({.is_cras_sr_enabled = true,
                                 .active_codec_format = HFP_CODEC_FORMAT_MSBC,
                                 .is_offload = false,
                                 .direction = CRAS_STREAM_INPUT,
                                 .expected_sample_rate = 24000}),
        HfpAlsaIodevSrTestParam({.is_cras_sr_enabled = true,
                                 .active_codec_format = HFP_CODEC_FORMAT_CVSD,
                                 .is_offload = true,
                                 .direction = CRAS_STREAM_INPUT,
                                 .expected_sample_rate = 24000}),
        HfpAlsaIodevSrTestParam({.is_cras_sr_enabled = true,
                                 .active_codec_format = HFP_CODEC_FORMAT_MSBC,
                                 .is_offload = true,
                                 .direction = CRAS_STREAM_INPUT,
                                 .expected_sample_rate = 24000}),
        // output
        HfpAlsaIodevSrTestParam({.is_cras_sr_enabled = true,
                                 .active_codec_format = HFP_CODEC_FORMAT_MSBC,
                                 .direction = CRAS_STREAM_OUTPUT,
                                 .expected_sample_rate = 16000})));

TEST_F(HfpAlsaIodev, TestWithSrBtAdapter) {
  enable_cras_sr_bt();

  struct cras_iodev* iodev;

  fake_sco_in.direction = CRAS_STREAM_INPUT;
  iodev = hfp_alsa_iodev_create(&fake_sco_in, fake_device, fake_slc, fake_sco,
                                NULL);

  iodev->open_dev(iodev);

  iodev->frames_queued(iodev, NULL);
  EXPECT_EQ(1, cras_iodev_sr_bt_adapter_frames_queued_called);

  iodev->delay_frames(iodev);
  EXPECT_EQ(1, cras_iodev_sr_bt_adapter_delay_frames_called);

  iodev->get_buffer(iodev, NULL, NULL);
  EXPECT_EQ(1, cras_iodev_sr_bt_adapter_get_buffer_called);

  iodev->put_buffer(iodev, 1);
  EXPECT_EQ(1, cras_iodev_sr_bt_adapter_put_buffer_called);

  iodev->flush_buffer(iodev);
  EXPECT_EQ(1, cras_iodev_sr_bt_adapter_flush_buffer_called);

  hfp_alsa_iodev_destroy(iodev);

  disable_cras_sr_bt();
}
}  // namespace

extern "C" {

int cras_iodev_set_format(struct cras_iodev* iodev,
                          const struct cras_audio_format* fmt) {
  cras_iodev_set_format_called++;
  return 0;
}

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

size_t cras_system_get_volume() {
  return 0;
}

const char* cras_bt_device_name(const struct cras_bt_device* device) {
  return "fake-device-name";
}

const char* cras_bt_device_address(const struct cras_bt_device* device) {
  return "1A:2B:3C:4D:5E:6F";
}

void cras_bt_device_append_iodev(struct cras_bt_device* device,
                                 struct cras_iodev* iodev,
                                 enum CRAS_BT_FLAGS btflag) {
  cras_bt_device_append_iodev_called++;
}

void cras_bt_device_rm_iodev(struct cras_bt_device* device,
                             struct cras_iodev* iodev) {
  cras_bt_device_rm_iodev_called++;
}

const char* cras_bt_device_object_path(const struct cras_bt_device* device) {
  return "/fake/object/path";
}

int cras_bt_device_get_stable_id(const struct cras_bt_device* device) {
  return 123;
}

void cras_iodev_free_resources(struct cras_iodev* iodev) {
  cras_iodev_free_resources_called++;
}

int hfp_set_call_status(struct hfp_slc_handle* handle, int call) {
  hfp_set_call_status_called++;
  return 0;
}

int hfp_event_speaker_gain(struct hfp_slc_handle* handle, int gain) {
  hfp_event_speaker_gain_called++;
  return 0;
}

bool hfp_slc_get_wideband_speech_supported(struct hfp_slc_handle* handle) {
  return false;
}

int hfp_slc_codec_connection_setup(struct hfp_slc_handle* handle) {
  return 0;
}

int cras_bt_device_sco_connect(struct cras_bt_device* device,
                               int codec,
                               bool use_offload) {
  return 0;
}

int cras_sco_add_iodev(struct cras_sco* sco,
                       enum CRAS_STREAM_DIRECTION direction,
                       struct cras_audio_format* format) {
  return 0;
}

int cras_sco_rm_iodev(struct cras_sco* sco,
                      enum CRAS_STREAM_DIRECTION direction) {
  return 0;
}

int cras_sco_has_iodev(struct cras_sco* sco) {
  return 0;
}

int cras_sco_set_fd(struct cras_sco* sco, int fd) {
  return 0;
}

int cras_sco_get_fd(struct cras_sco* sco) {
  return -1;
}
int cras_sco_close_fd(struct cras_sco* sco) {
  return 0;
}

int hfp_slc_get_selected_codec(struct hfp_slc_handle* handle) {
  return hfp_slc_get_selected_codec_return_val;
}

const uint32_t cras_floss_hfp_get_stable_id(struct cras_hfp* hfp) {
  return 0;
}

int cras_floss_hfp_start(struct cras_hfp* hfp,
                         thread_callback cb,
                         enum CRAS_STREAM_DIRECTION dir) {
  return 0;
}

int cras_floss_hfp_stop(struct cras_hfp* hfp, enum CRAS_STREAM_DIRECTION dir) {
  return 0;
}

void cras_floss_hfp_set_volume(struct cras_hfp* hfp, unsigned int volume) {}

bool cras_floss_hfp_is_codec_format_supported(struct cras_hfp* hfp,
                                              enum HFP_CODEC_FORMAT codec) {
  return cras_floss_hfp_is_codec_format_supported_ret;
}

enum HFP_CODEC_FORMAT cras_floss_hfp_get_active_codec_format(
    struct cras_hfp* hfp) {
  return cras_floss_hfp_get_active_codec_format_ret;
}

const char* cras_floss_hfp_get_display_name(struct cras_hfp* hfp) {
  return "Floss device fake name";
}

void* cras_iodev_sr_bt_adapter_create(void*, void*) {
  ++cras_iodev_sr_bt_adapter_create_called;
  return (void*)0x123;
}

void cras_iodev_sr_bt_adapter_destroy(void*) {
  ++cras_iodev_sr_bt_adapter_destroy_called;
}

int cras_iodev_sr_bt_adapter_frames_queued(void*, void*) {
  ++cras_iodev_sr_bt_adapter_frames_queued_called;
  return 0;
};

int cras_iodev_sr_bt_adapter_delay_frames(void*) {
  ++cras_iodev_sr_bt_adapter_delay_frames_called;
  return 0;
};

int cras_iodev_sr_bt_adapter_get_buffer(void*, void**, void*) {
  ++cras_iodev_sr_bt_adapter_get_buffer_called;
  return 0;
};

int cras_iodev_sr_bt_adapter_put_buffer(void*, const unsigned) {
  ++cras_iodev_sr_bt_adapter_put_buffer_called;
  return 0;
};

int cras_iodev_sr_bt_adapter_flush_buffer(void*) {
  ++cras_iodev_sr_bt_adapter_flush_buffer_called;
  return 0;
};

}  // extern "C"
