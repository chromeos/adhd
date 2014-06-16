// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "audio_thread.c"
}

#include <stdio.h>
#include <sys/select.h>
#include <gtest/gtest.h>

extern "C" {

static int cras_mix_add_stream_dont_fill_next;
static unsigned int cras_mix_add_stream_count;
static unsigned int cras_mix_mute_count;
static int cras_rstream_audio_ready_count;
static unsigned int cras_rstream_request_audio_called;
static unsigned int cras_rstream_audio_ready_called;
static unsigned int cras_rstream_destroy_called;
static unsigned int cras_metrics_log_histogram_called;
static const char *cras_metrics_log_histogram_name;
static unsigned int cras_metrics_log_histogram_sample;
static unsigned int cras_metrics_log_event_called;

static void (*cras_system_add_select_fd_callback)(void *data);
static void *cras_system_add_select_fd_callback_data;

static int select_return_value;
static struct timeval select_timeval;
static int select_max_fd;
static fd_set select_in_fds;
static fd_set select_out_fds;
static uint32_t *select_write_ptr;
static uint32_t select_write_value;
static unsigned int cras_iodev_set_format_called;
static unsigned int cras_iodev_set_playback_timestamp_called;
static unsigned int cras_system_get_volume_return;

static int cras_dsp_get_pipeline_called;
static int cras_dsp_get_pipeline_ret;
static int cras_dsp_put_pipeline_called;
static int cras_dsp_pipeline_get_source_buffer_called;
static int cras_dsp_pipeline_get_sink_buffer_called;
static float cras_dsp_pipeline_source_buffer[2][DSP_BUFFER_SIZE];
static float cras_dsp_pipeline_sink_buffer[2][DSP_BUFFER_SIZE];
static int cras_dsp_pipeline_get_delay_called;
static int cras_dsp_pipeline_apply_called;
static int cras_dsp_pipeline_apply_sample_count;
static struct timespec time_now;
static int cras_fmt_conversion_needed_return_val;
static struct cras_audio_area *dummy_audio_area1;
static struct cras_audio_area *dummy_audio_area2;

}

// Number of frames past target that will be added to sleep times to insure that
// all frames are ready.
static const int CAP_EXTRA_SLEEP_FRAMES = 16;

//  Test the audio capture path.
class ReadStreamSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      memset(&fmt_, 0, sizeof(fmt_));
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;

      memset(&iodev_, 0, sizeof(iodev_));
      iodev_.format = &fmt_;
      iodev_.buffer_size = 16384;
      cb_threshold_ = 480;
      iodev_.direction = CRAS_STREAM_INPUT;

      iodev_.frames_queued = frames_queued;
      iodev_.delay_frames = delay_frames;
      iodev_.get_buffer = get_buffer;
      iodev_.put_buffer = put_buffer;
      iodev_.is_open = is_open;
      iodev_.open_dev = open_dev;
      iodev_.close_dev = close_dev;
      iodev_.dev_running = dev_running;

      memcpy(&output_dev_, &iodev_, sizeof(output_dev_));
      output_dev_.direction = CRAS_STREAM_OUTPUT;

      SetupRstream(&rstream_, 1);
      shm_ = cras_rstream_input_shm(rstream_);
      SetupRstream(&rstream2_, 2);
      shm2_ = cras_rstream_input_shm(rstream2_);

      dummy_audio_area1 = (cras_audio_area*)calloc(1,
          sizeof(cras_audio_area) + 2 * sizeof(cras_channel_area));
      dummy_audio_area1->num_channels = 2;
      channel_area_set_channel(&dummy_audio_area1->channels[0], CRAS_CH_FL);
      channel_area_set_channel(&dummy_audio_area1->channels[1], CRAS_CH_FR);
      rstream_->input_audio_area = dummy_audio_area1;
      dummy_audio_area2 = (cras_audio_area*)calloc(1,
          sizeof(cras_audio_area) + 2 * sizeof(cras_channel_area));
      dummy_audio_area2->num_channels = 2;
      channel_area_set_channel(&dummy_audio_area2->channels[0], CRAS_CH_FL);
      channel_area_set_channel(&dummy_audio_area2->channels[1], CRAS_CH_FR);
      rstream2_->input_audio_area = dummy_audio_area2;

      cras_mix_add_stream_dont_fill_next = 0;
      cras_mix_add_stream_count = 0;
      cras_rstream_audio_ready_called = 0;
      dev_running_called_ = 0;
      is_open_ = 0;
      close_dev_called_ = 0;

      cras_dsp_get_pipeline_called = 0;
      cras_dsp_get_pipeline_ret = 0;
      cras_dsp_put_pipeline_called = 0;
      cras_dsp_pipeline_get_source_buffer_called = 0;
      cras_dsp_pipeline_get_sink_buffer_called = 0;
      memset(&cras_dsp_pipeline_source_buffer, 0,
             sizeof(cras_dsp_pipeline_source_buffer));
      memset(&cras_dsp_pipeline_sink_buffer, 0,
             sizeof(cras_dsp_pipeline_sink_buffer));
      cras_dsp_pipeline_get_delay_called = 0;
      cras_dsp_pipeline_apply_called = 0;
      cras_dsp_pipeline_apply_sample_count = 0;
      cras_iodev_set_format_called = 0;
      cras_iodev_set_playback_timestamp_called = 0;
    }

    virtual void TearDown() {
      free(shm_->area);
      free(rstream_);
      free(shm2_->area);
      free(rstream2_);
      free(dummy_audio_area1);
      free(dummy_audio_area2);
    }

    void SetupRstream(struct cras_rstream **rstream, int fd) {
      struct cras_audio_shm *shm;

      *rstream = (struct cras_rstream *)calloc(1, sizeof(**rstream));
      memcpy(&(*rstream)->format, &fmt_, sizeof(fmt_));
      (*rstream)->direction = CRAS_STREAM_INPUT;
      (*rstream)->cb_threshold = cb_threshold_;
      (*rstream)->client = (struct cras_rclient *)this;

      shm = cras_rstream_input_shm(*rstream);
      shm->area = (struct cras_audio_shm_area *)calloc(1,
          sizeof(*shm->area) + cb_threshold_ * 8);
      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(
          shm, cb_threshold_ * cras_shm_frame_bytes(shm));
    }

    unsigned int GetCaptureSleepFrames() {
      // Account for padding the sleep interval to ensure the wake up happens
      // after the last desired frame is received.
      return cb_threshold_ + 16;
    }

    // Stub functions for the iodev structure.
    static int frames_queued(const cras_iodev* iodev) {
      return frames_queued_;
    }

    static int delay_frames(const cras_iodev* iodev) {
      return delay_frames_;
    }

    static int get_buffer(cras_iodev* iodev,
                          struct cras_audio_area** area,
                          unsigned int* num) {
      size_t sz = sizeof(*area_) + sizeof(struct cras_channel_area) * 2;

      if (audio_buffer_size_ < *num)
	      *num = audio_buffer_size_;

      area_ = (cras_audio_area*)calloc(1, sz);
      area_->frames = *num;
      area_->num_channels = 2;
      area_->channels[0].buf = audio_buffer_;
      channel_area_set_channel(&area_->channels[0], CRAS_CH_FL);
      area_->channels[0].step_bytes = 4;
      area_->channels[1].buf = audio_buffer_ + 2;
      channel_area_set_channel(&area_->channels[1], CRAS_CH_FR);
      area_->channels[1].step_bytes = 4;

      *area = area_;
      return 0;
    }

    static int put_buffer(cras_iodev* iodev,
                          unsigned int num) {
      free(area_);
      return 0;
    }

    static int is_open(const cras_iodev* iodev) {
      return is_open_;
    }

    static int open_dev(cras_iodev* iodev) {
      return 0;
    }

    static int close_dev(cras_iodev* iodev) {
      close_dev_called_++;
      return 0;
    }

    static int dev_running(const cras_iodev* iodev) {
      dev_running_called_++;
      return 1;
    }


  struct cras_iodev iodev_;
  struct cras_iodev output_dev_;
  static int is_open_;
  static int frames_queued_;
  static int delay_frames_;
  static unsigned int cb_threshold_;
  static uint8_t audio_buffer_[8192];
  static struct cras_audio_area *area_;
  static unsigned int audio_buffer_size_;
  static unsigned int dev_running_called_;
  static unsigned int close_dev_called_;
  struct cras_rstream *rstream_;
  struct cras_rstream *rstream2_;
  struct cras_audio_format fmt_;
  struct cras_audio_shm *shm_;
  struct cras_audio_shm *shm2_;
};

int ReadStreamSuite::is_open_ = 0;
int ReadStreamSuite::frames_queued_ = 0;
int ReadStreamSuite::delay_frames_ = 0;
unsigned int ReadStreamSuite::close_dev_called_ = 0;
uint8_t ReadStreamSuite::audio_buffer_[8192];
unsigned int ReadStreamSuite::audio_buffer_size_ = 0;
unsigned int ReadStreamSuite::dev_running_called_ = 0;
unsigned int ReadStreamSuite::cb_threshold_ = 0;
struct cras_audio_area *ReadStreamSuite::area_;

TEST_F(ReadStreamSuite, PossiblyReadGetAvailError) {
  struct timespec ts;
  int rc;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  thread_add_stream(thread, rstream_);
  EXPECT_EQ(1, cras_iodev_set_format_called);

  frames_queued_ = -4;
  is_open_ = 1;
  rc = unified_io(thread, &ts);
  EXPECT_EQ(-4, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
  EXPECT_EQ(0, cras_iodev_set_playback_timestamp_called);
  EXPECT_EQ(1, close_dev_called_);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

TEST_F(ReadStreamSuite, PossiblyReadEmpty) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  thread_add_stream(thread, rstream_);
  EXPECT_EQ(1, cras_iodev_set_format_called);

  //  If no samples are present, it should sleep for cb_threshold frames.
  frames_queued_ = 0;
  is_open_ = 1;
  nsec_expected = (GetCaptureSleepFrames()) * 1000000000ULL /
                  (uint64_t)fmt_.frame_rate;
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_EQ(0, shm_->area->write_offset[0]);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(1, dev_running_called_);
  EXPECT_EQ(0, cras_iodev_set_playback_timestamp_called);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

TEST_F(ReadStreamSuite, PossiblyReadTooLittleData) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;
  static const uint64_t num_frames_short = 40;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  thread_add_stream(thread, rstream_);

  frames_queued_ = cb_threshold_ - num_frames_short;
  is_open_ = 1;
  audio_buffer_size_ = frames_queued_;
  nsec_expected = ((uint64_t)num_frames_short + CAP_EXTRA_SLEEP_FRAMES) *
                  1000000000ULL / (uint64_t)fmt_.frame_rate;

  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_rstream_audio_ready_called);
  /* As much data as can be, should be read. */
  EXPECT_EQ(cb_threshold_ - num_frames_short,
            shm_->area->write_offset[0] / 4);
  EXPECT_EQ(0, shm_->area->write_buf_idx);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

TEST_F(ReadStreamSuite, PossiblyReadHasDataWriteStream) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = cb_threshold_ + 4;
  audio_buffer_size_ = frames_queued_;

  for (unsigned int i = 0; i < sizeof(audio_buffer_); i++)
	  audio_buffer_[i] = i;

  uint64_t sleep_frames = GetCaptureSleepFrames() - 4;
  nsec_expected = (uint64_t)sleep_frames * 1000000000ULL /
                  (uint64_t)fmt_.frame_rate;
  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(cb_threshold_, cras_rstream_audio_ready_count);
  EXPECT_EQ(1, cras_rstream_audio_ready_called);
  EXPECT_EQ(0, cras_iodev_set_playback_timestamp_called);
  for (size_t i = 0; i < cb_threshold_; i++)
    EXPECT_EQ(audio_buffer_[i], shm_->area->samples[i]);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

TEST_F(ReadStreamSuite, PossiblyReadHasDataWriteTwoStreams) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  rc = thread_add_stream(thread, rstream_);
  EXPECT_EQ(0, rc);
  rc = thread_add_stream(thread, rstream2_);
  EXPECT_EQ(0, rc);

  //  A full block plus 4 frames.
  frames_queued_ = cb_threshold_ + 4;
  audio_buffer_size_ = frames_queued_;

  for (unsigned int i = 0; i < sizeof(audio_buffer_); i++)
	  audio_buffer_[i] = i;

  uint64_t sleep_frames = GetCaptureSleepFrames() - 4;
  nsec_expected = (uint64_t)sleep_frames * 1000000000ULL /
                  (uint64_t)fmt_.frame_rate;
  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(2, cras_rstream_audio_ready_called);
  EXPECT_EQ(cb_threshold_, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < cb_threshold_; i++)
    EXPECT_EQ(audio_buffer_[i], shm_->area->samples[i]);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

TEST_F(ReadStreamSuite, PossiblyReadHasDataWriteTwoDifferentStreams) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  cb_threshold_ /= 2;
  rstream_->cb_threshold = cb_threshold_;

  rc = thread_add_stream(thread, rstream_);
  EXPECT_EQ(0, rc);
  rc = thread_add_stream(thread, rstream2_);
  EXPECT_EQ(0, rc);

  //  A full block plus 4 frames.
  frames_queued_ = cb_threshold_ + 4;
  audio_buffer_size_ = frames_queued_;

  uint64_t sleep_frames = GetCaptureSleepFrames() - 4;
  nsec_expected = (uint64_t)sleep_frames * 1000000000ULL /
                  (uint64_t)fmt_.frame_rate;
  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(1, cras_rstream_audio_ready_called);
  EXPECT_EQ(cb_threshold_, cras_rstream_audio_ready_count);

  frames_queued_ = cb_threshold_ + 5;
  sleep_frames = GetCaptureSleepFrames() - 5;
  nsec_expected = (uint64_t)sleep_frames * 1000000000ULL /
                  (uint64_t)fmt_.frame_rate;
  audio_buffer_size_ = frames_queued_;
  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(3, cras_rstream_audio_ready_called);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

TEST_F(ReadStreamSuite, PossiblyReadHasDataWriteTwoStreamsOneUnified) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;
  struct audio_thread *thread;
  struct cras_audio_shm *shm1, *shm2;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);
  thread_add_active_dev(thread, &output_dev_);

  cb_threshold_ /= 2;
  rstream_->cb_threshold = cb_threshold_;

  rc = thread_add_stream(thread, rstream_);
  EXPECT_EQ(0, rc);
  rstream2_->direction = CRAS_STREAM_UNIFIED;

  shm1 = cras_rstream_output_shm(rstream_);
  shm1->area = (struct cras_audio_shm_area *)calloc(1,
		  sizeof(*shm1->area) + cb_threshold_ * 8);
  cras_shm_set_frame_bytes(shm1, 4);
  cras_shm_set_used_size(
		  shm1, cb_threshold_ * cras_shm_frame_bytes(shm1));
  shm1->area->write_offset[0] = cras_shm_used_size(shm1);

  shm2 = cras_rstream_output_shm(rstream2_);
  shm2->area = (struct cras_audio_shm_area *)calloc(1,
		  sizeof(*shm2->area) + cb_threshold_ * 8);
  cras_shm_set_frame_bytes(shm2, 4);
  cras_shm_set_used_size(
		  shm2, cb_threshold_ * cras_shm_frame_bytes(shm2));
  shm2->area->write_offset[0] = cras_shm_used_size(shm2);
  shm2->area->write_offset[1] = cras_shm_used_size(shm2);


  rc = thread_add_stream(thread, rstream2_);
  EXPECT_EQ(0, rc);

  //  A full block plus 240 frames.
  frames_queued_ = cb_threshold_ + 240;
  audio_buffer_size_ = frames_queued_;

  uint64_t sleep_frames = GetCaptureSleepFrames() - 240;
  nsec_expected = (uint64_t)sleep_frames * 1000000000ULL /
                  (uint64_t)fmt_.frame_rate;
  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(1, cras_rstream_audio_ready_called);
  EXPECT_EQ(rstream_->cb_threshold, cras_rstream_audio_ready_count);

  frames_queued_ = cb_threshold_ + 5;
  audio_buffer_size_ = frames_queued_;
  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_EQ(3, cras_rstream_audio_ready_called);

  thread->streams = 0;
  audio_thread_destroy(thread);

  free(shm1->area);
  free(shm2->area);
}

TEST_F(ReadStreamSuite, PossiblyReadWriteTwoBuffers) {
  struct timespec ts;
  int rc;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = cb_threshold_ + 4;
  audio_buffer_size_ = frames_queued_;

  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;

  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_shm_num_overruns(shm_));
  EXPECT_EQ(rstream_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < cb_threshold_; i++)
    EXPECT_EQ(audio_buffer_[i], shm_->area->samples[i]);
  cras_shm_buffer_read(shm_, cras_rstream_audio_ready_count);

  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_shm_num_overruns(shm_));
  EXPECT_EQ(rstream_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < cb_threshold_; i++)
    EXPECT_EQ(audio_buffer_[i],
        shm_->area->samples[i + cras_shm_used_size(shm_)]);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

TEST_F(ReadStreamSuite, PossiblyReadWriteThreeBuffers) {
  struct timespec ts;
  int rc;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = cb_threshold_ + 4;
  audio_buffer_size_ = frames_queued_;
  is_open_ = 1;

  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_shm_num_overruns(shm_));
  EXPECT_EQ(cb_threshold_, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < cb_threshold_; i++)
    EXPECT_EQ(audio_buffer_[i], shm_->area->samples[i]);
  cras_shm_buffer_read(shm_, cras_rstream_audio_ready_count);

  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_shm_num_overruns(shm_));
  EXPECT_EQ(cb_threshold_, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < cb_threshold_; i++)
    EXPECT_EQ(audio_buffer_[i],
        shm_->area->samples[i + cras_shm_used_size(shm_)]);

  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_shm_num_overruns(shm_));  //  Should have overrun.
  EXPECT_EQ(cb_threshold_, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < cb_threshold_; i++)
    EXPECT_EQ(audio_buffer_[i], shm_->area->samples[i]);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

TEST_F(ReadStreamSuite, PossiblyReadWithoutPipeline) {
  struct timespec ts;
  int rc;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = cb_threshold_ + 4;
  audio_buffer_size_ = frames_queued_;
  iodev_.dsp_context = reinterpret_cast<cras_dsp_context *>(0x5);
  is_open_ = 1;

  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(2, cras_dsp_get_pipeline_called);
  EXPECT_EQ(0, cras_dsp_put_pipeline_called);
  EXPECT_EQ(0, cras_dsp_pipeline_get_source_buffer_called);
  EXPECT_EQ(0, cras_dsp_pipeline_get_sink_buffer_called);
  EXPECT_EQ(0, cras_dsp_pipeline_get_delay_called);
  EXPECT_EQ(0, cras_dsp_pipeline_apply_called);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

TEST_F(ReadStreamSuite, PossiblyReadWithPipeline) {
  struct timespec ts;
  int rc;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = cb_threshold_ + 4;
  audio_buffer_size_ = frames_queued_;
  iodev_.dsp_context = reinterpret_cast<cras_dsp_context *>(0x5);
  cras_dsp_get_pipeline_ret = 0x6;
  is_open_ = 1;

  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(2, cras_dsp_get_pipeline_called);
  EXPECT_EQ(2, cras_dsp_put_pipeline_called);
  EXPECT_EQ(1, cras_dsp_pipeline_get_delay_called);
  EXPECT_EQ(1, cras_dsp_pipeline_apply_called);
  EXPECT_EQ(cb_threshold_, cras_dsp_pipeline_apply_sample_count);

  thread->streams = 0;
  audio_thread_destroy(thread);
}

//  Test the audio playback path.
class WriteStreamSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      memset(&fmt_, 0, sizeof(fmt_));
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;

      memset(&iodev_, 0, sizeof(iodev_));
      iodev_.format = &fmt_;
      iodev_.buffer_size = 16384;
      iodev_.direction = CRAS_STREAM_OUTPUT;
      iodev_.software_volume_scaler = 1.0;

      iodev_.frames_queued = frames_queued;
      iodev_.delay_frames = delay_frames;
      iodev_.get_buffer = get_buffer;
      iodev_.put_buffer = put_buffer;
      iodev_.dev_running = dev_running;
      iodev_.is_open = is_open;
      iodev_.open_dev = open_dev;
      iodev_.close_dev = close_dev;

      buffer_frames_ = 480;
      cb_threshold_ = 96;
      SetupRstream(&rstream_, 1);
      shm_ = cras_rstream_output_shm(rstream_);
      SetupRstream(&rstream2_, 2);
      shm2_ = cras_rstream_output_shm(rstream2_);

      thread_ = audio_thread_create();
      ASSERT_TRUE(thread_);
      thread_add_active_dev(thread_, &iodev_);

      cras_mix_add_stream_dont_fill_next = 0;
      cras_mix_add_stream_count = 0;
      select_max_fd = -1;
      select_write_ptr = NULL;
      cras_metrics_log_event_called = 0;
      cras_rstream_request_audio_called = 0;
      cras_rstream_destroy_called = 0;
      cras_dsp_get_pipeline_called = 0;
      is_open_ = 0;
      close_dev_called_ = 0;

      cras_dsp_get_pipeline_ret = 0;
      cras_dsp_put_pipeline_called = 0;
      cras_dsp_pipeline_get_source_buffer_called = 0;
      cras_dsp_pipeline_get_sink_buffer_called = 0;
      memset(&cras_dsp_pipeline_source_buffer, 0,
             sizeof(cras_dsp_pipeline_source_buffer));
      memset(&cras_dsp_pipeline_sink_buffer, 0,
             sizeof(cras_dsp_pipeline_sink_buffer));
      cras_dsp_pipeline_get_delay_called = 0;
      cras_dsp_pipeline_apply_called = 0;
      cras_dsp_pipeline_apply_sample_count = 0;

      dev_running_called_ = 0;
      frames_written_ = 0;

      thread_add_stream(thread_, rstream_);
    }

    virtual void TearDown() {
      free(shm_->area);
      free(rstream_);
      free(shm2_->area);
      free(rstream2_);
      thread_->streams = 0;
      audio_thread_destroy(thread_);
    }

    void SetupRstream(struct cras_rstream **rstream, int fd) {
      struct cras_audio_shm *shm;

      *rstream = (struct cras_rstream *)calloc(1, sizeof(**rstream));
      memcpy(&(*rstream)->format, &fmt_, sizeof(fmt_));
      (*rstream)->fd = fd;
      (*rstream)->buffer_frames = buffer_frames_;
      (*rstream)->cb_threshold = cb_threshold_;
      (*rstream)->client = (struct cras_rclient *)this;

      shm = cras_rstream_output_shm(*rstream);
      shm->area = (struct cras_audio_shm_area *)calloc(1,
          sizeof(*shm->area) + cb_threshold_ * 8);
      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(
          shm, buffer_frames_ * cras_shm_frame_bytes(shm));
    }

    uint64_t GetCaptureSleepFrames() {
      // Account for padding the sleep interval to ensure the wake up happens
      // after the last desired frame is received.
      return cb_threshold_ + CAP_EXTRA_SLEEP_FRAMES;
    }

    // Stub functions for the iodev structure.
    static int frames_queued(const cras_iodev* iodev) {
      return frames_queued_ + frames_written_;
    }

    static int delay_frames(const cras_iodev* iodev) {
      return delay_frames_;
    }

    static int get_buffer(cras_iodev* iodev,
                          struct cras_audio_area** area,
                          unsigned int* num) {
      size_t sz = sizeof(*area_) + sizeof(struct cras_channel_area) * 2;

      if (audio_buffer_size_ < *num)
	      *num = audio_buffer_size_;

      area_ = (cras_audio_area*)calloc(1, sz);
      area_->frames = *num;
      area_->num_channels = 2;
      area_->channels[0].buf = audio_buffer_;
      channel_area_set_channel(&area_->channels[0], CRAS_CH_FL);
      area_->channels[0].step_bytes = 4;
      area_->channels[1].buf = audio_buffer_ + 2;
      channel_area_set_channel(&area_->channels[1], CRAS_CH_FR);
      area_->channels[1].step_bytes = 4;

      *area = area_;
      return 0;
    }

    static int put_buffer(cras_iodev* iodev,
                          unsigned int num) {
      free(area_);
      frames_written_ += num;
      return 0;
    }

    static int dev_running(const cras_iodev* iodev) {
      dev_running_called_++;
      return dev_running_;
    }

    static int is_open(const cras_iodev* iodev) {
      return is_open_;
    }

    static int open_dev(cras_iodev* iodev) {
      is_open_ = 1;
      open_dev_called_++;
      return 0;
    }

    static int close_dev(cras_iodev* iodev) {
      close_dev_called_++;
      is_open_ = 0;
      return 0;
    }

  struct cras_iodev iodev_;
  static int is_open_;
  static int frames_queued_;
  static int frames_written_;
  static int delay_frames_;
  static unsigned int cb_threshold_;
  static unsigned int buffer_frames_;
  static uint8_t audio_buffer_[8192];
  static unsigned int audio_buffer_size_;
  static int dev_running_;
  static unsigned int dev_running_called_;
  static unsigned int close_dev_called_;
  static unsigned int open_dev_called_;
  static struct cras_audio_area *area_;
  struct cras_audio_format fmt_;
  struct cras_rstream* rstream_;
  struct cras_rstream* rstream2_;
  struct cras_audio_shm* shm_;
  struct cras_audio_shm* shm2_;
  struct audio_thread *thread_;
};

int WriteStreamSuite::is_open_ = 0;
int WriteStreamSuite::frames_queued_ = 0;
int WriteStreamSuite::frames_written_ = 0;
int WriteStreamSuite::delay_frames_ = 0;
unsigned int WriteStreamSuite::cb_threshold_ = 0;
unsigned int WriteStreamSuite::buffer_frames_ = 0;
uint8_t WriteStreamSuite::audio_buffer_[8192];
unsigned int WriteStreamSuite::audio_buffer_size_ = 0;
int WriteStreamSuite::dev_running_ = 1;
unsigned int WriteStreamSuite::dev_running_called_ = 0;
unsigned int WriteStreamSuite::close_dev_called_ = 0;
unsigned int WriteStreamSuite::open_dev_called_ = 0;
struct cras_audio_area *WriteStreamSuite::area_;

TEST_F(WriteStreamSuite, PossiblyFillGetAvailError) {
  struct timespec ts;
  int rc;

  frames_queued_ = -4;
  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(-4, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
  EXPECT_EQ(1, close_dev_called_);
}

TEST_F(WriteStreamSuite, PossiblyFillEarlyWake) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  If woken and still have tons of data to play, go back to sleep.
  frames_queued_ = cb_threshold_ * 2;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;

  nsec_expected = (cb_threshold_) * 1000000000ULL /
                  (uint64_t)fmt_.frame_rate;
  iodev_.direction = CRAS_STREAM_OUTPUT;
  is_open_ = 1;

  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromStreamFull) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  // Have cb_threshold samples left.
  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;
  nsec_expected = (uint64_t)cb_threshold_ *
      1000000000ULL / (uint64_t)fmt_.frame_rate;

  // shm has plenty of data in it.
  shm_->area->write_offset[0] = cb_threshold_ * 4;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;
  is_open_ = 1;

  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(cb_threshold_, cras_mix_add_stream_count);
  EXPECT_EQ(0, cras_rstream_request_audio_called);
  EXPECT_EQ(-1, select_max_fd);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromStreamMinSet) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  // Have cb_threshold samples left.
  frames_queued_ = cb_threshold_ * 2;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;
  // Setting the min_buffer_level should shorten the sleep time.
  iodev_.min_buffer_level = cb_threshold_;

  // shm has is empty.
  shm_->area->write_offset[0] = 0;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;
  is_open_ = 1;
  // Set write offset after call to select.
  select_write_ptr = &shm_->area->write_offset[0];
  select_write_value = cb_threshold_ * 4;

  // After the callback there will be cb_thresh of data in the buffer and
  // cb_thresh x 2 data in the hardware (frames_queued_) = 3 cb_thresh total.
  // It should sleep until there is a total of cb_threshold + min_buffer_level
  // left,  or 3 - 2 = 1 cb_thresh worth of delay.
  nsec_expected = (uint64_t)cb_threshold_ *
      1000000000ULL / (uint64_t)fmt_.frame_rate;

  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(cb_threshold_, cras_mix_add_stream_count);
  EXPECT_EQ(1, cras_rstream_request_audio_called);
}

TEST_F(WriteStreamSuite, PossiblyFillFramesQueued) {
  struct timespec ts;
  int rc;

  // Have cb_threshold samples left.
  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;

  // shm has plenty of data in it.
  shm_->area->write_offset[0] = cras_shm_used_size(shm_);

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, dev_running_called_);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromStreamOneEmpty) {
  struct timespec ts;
  int rc;

  // Have cb_threshold samples left.
  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;

  // shm has plenty of data in it.
  shm_->area->write_offset[0] = cras_shm_used_size(shm_);

  // Test that nothing breaks if there is an empty stream.
  cras_mix_add_stream_dont_fill_next = 1;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_rstream_request_audio_called);
  EXPECT_EQ(-1, select_max_fd);
  EXPECT_EQ(0, shm_->area->read_offset[0]);
  EXPECT_EQ(0, shm_->area->read_offset[1]);
  EXPECT_EQ(cras_shm_used_size(shm_), shm_->area->write_offset[0]);
  EXPECT_EQ(0, shm_->area->write_offset[1]);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromStreamNeedFill) {
  struct timespec ts;
  uint64_t nsec_expected;
  int rc;

  //  Have cb_threshold samples left.
  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;

  //  shm is out of data.
  shm_->area->write_offset[0] = 0;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;
  // Set write offset after call to select.
  select_write_ptr = &shm_->area->write_offset[0];
  select_write_value = (buffer_frames_ - cb_threshold_) * 4;

  nsec_expected = (buffer_frames_ - cb_threshold_) *
      1000000000ULL / (uint64_t)fmt_.frame_rate;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(buffer_frames_ - cb_threshold_, cras_mix_add_stream_count);
  EXPECT_EQ(1, cras_rstream_request_audio_called);
  EXPECT_NE(-1, select_max_fd);
  EXPECT_EQ(0, memcmp(&select_out_fds, &select_in_fds, sizeof(select_in_fds)));
  EXPECT_EQ(0, shm_->area->read_offset[0]);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromStreamNeedFillWithScaler) {
  struct timespec ts;
  uint64_t nsec_expected;
  int rc;

  //  Have cb_threshold samples left.
  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;

  //  Software volume config.
  iodev_.software_volume_scaler = 0.5;

  //  shm is out of data.
  shm_->area->write_offset[0] = 0;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  // Set write offset after call to select.
  select_write_ptr = &shm_->area->write_offset[0];
  select_write_value = (buffer_frames_ - cb_threshold_) * 4;

  nsec_expected = (buffer_frames_ - cb_threshold_) *
      1000000000ULL / (uint64_t)fmt_.frame_rate;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(buffer_frames_ - cb_threshold_,
            cras_mix_add_stream_count);
  EXPECT_EQ(1, cras_rstream_request_audio_called);
  EXPECT_NE(-1, select_max_fd);
  EXPECT_EQ(0, memcmp(&select_out_fds, &select_in_fds, sizeof(select_in_fds)));
  EXPECT_EQ(0, shm_->area->read_offset[0]);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromTwoStreamsFull) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  Have cb_threshold samples left.
  frames_queued_ = cras_rstream_get_cb_threshold(rstream_);
  audio_buffer_size_ = buffer_frames_ - frames_queued_;
  nsec_expected = (uint64_t)cras_rstream_get_cb_threshold(rstream_) *
      1000000000ULL / (uint64_t)fmt_.frame_rate;

  //  shm has plenty of data in it.
  shm_->area->write_offset[0] = cras_rstream_get_cb_threshold(rstream_) * 4;
  shm2_->area->write_offset[0] = cras_rstream_get_cb_threshold(rstream2_) * 4;

  thread_add_stream(thread_, rstream2_);

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(cras_rstream_get_cb_threshold(rstream_),
            cras_mix_add_stream_count);
  EXPECT_EQ(0, cras_rstream_request_audio_called);
  EXPECT_EQ(-1, select_max_fd);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromTwoStreamsFullOneMixes) {
  struct timespec ts;
  int rc;
  size_t written_expected;

  //  Have cb_threshold samples left.
  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;
  written_expected = buffer_frames_ - cb_threshold_;

  //  shm has plenty of data in it.
  shm_->area->write_offset[0] = cras_shm_used_size(shm_);
  shm2_->area->write_offset[0] = cras_shm_used_size(shm2_);

  thread_add_stream(thread_, rstream2_);

  //  Test that nothing breaks if one stream doesn't fill.
  cras_mix_add_stream_dont_fill_next = 1;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_rstream_request_audio_called);
  EXPECT_EQ(0, shm_->area->read_offset[0]);  //  No write from first stream.
  EXPECT_EQ(written_expected * 4, shm2_->area->read_offset[0]);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromTwoStreamsNeedFill) {
  struct timespec ts;
  int rc;

  //  Have cb_threshold samples left.
  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;

  //  shm has nothing left.
  shm_->area->write_offset[0] = 0;
  shm2_->area->write_offset[0] = 0;

  thread_add_stream(thread_, rstream2_);

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  FD_SET(rstream2_->fd, &select_out_fds);
  select_return_value = 2;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_mix_mute_count);
  EXPECT_EQ(2, cras_rstream_request_audio_called);
  EXPECT_NE(-1, select_max_fd);

  /* should only mute buffer if underrun in imminent. */
  frames_queued_ = 0;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(cb_threshold_, cras_mix_mute_count);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromTwoStreamsOneLimited) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;
  static const unsigned int smaller_frames = 10;

  //  Have cb_threshold samples left.
  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;
  nsec_expected = (uint64_t)smaller_frames *
                  (1000000000ULL / (uint64_t)fmt_.frame_rate);

  //  One has too little the other is full.
  shm_->area->write_offset[0] = smaller_frames * 4;
  shm_->area->write_buf_idx = 1;
  shm2_->area->write_offset[0] = cras_shm_used_size(shm2_);
  shm2_->area->write_buf_idx = 1;

  thread_add_stream(thread_, rstream2_);

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(smaller_frames, cras_mix_add_stream_count);
  EXPECT_EQ(1, cras_rstream_request_audio_called);
  EXPECT_NE(-1, select_max_fd);
}

TEST_F(WriteStreamSuite, PossiblyFillWithoutPipeline) {
  struct timespec ts;
  int rc;

  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;
  iodev_.dsp_context = reinterpret_cast<cras_dsp_context *>(0x5);

  //  shm has plenty of data in it.
  shm_->area->write_offset[0] = cras_shm_used_size(shm_);

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(buffer_frames_ - cb_threshold_,
            cras_mix_add_stream_count);
  EXPECT_EQ(2, cras_dsp_get_pipeline_called);
  EXPECT_EQ(0, cras_dsp_put_pipeline_called);
  EXPECT_EQ(0, cras_dsp_pipeline_get_source_buffer_called);
  EXPECT_EQ(0, cras_dsp_pipeline_get_sink_buffer_called);
  EXPECT_EQ(0, cras_dsp_pipeline_get_delay_called);
  EXPECT_EQ(0, cras_dsp_pipeline_apply_called);
}

TEST_F(WriteStreamSuite, PossiblyFillWithPipeline) {
  struct timespec ts;
  int rc;

  //  Have cb_threshold samples left.
  frames_queued_ = cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;
  iodev_.dsp_context = reinterpret_cast<cras_dsp_context *>(0x5);
  cras_dsp_get_pipeline_ret = 0x6;

  //  shm has plenty of data in it.
  shm_->area->write_offset[0] = cras_shm_used_size(shm_);

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(buffer_frames_ - cb_threshold_,
            cras_mix_add_stream_count);
  EXPECT_EQ(2, cras_dsp_get_pipeline_called);
  EXPECT_EQ(2, cras_dsp_put_pipeline_called);
  EXPECT_EQ(1, cras_dsp_pipeline_get_delay_called);
  EXPECT_EQ(1, cras_dsp_pipeline_apply_called);
  EXPECT_EQ(buffer_frames_ - cb_threshold_,
            cras_dsp_pipeline_apply_sample_count);
}

TEST_F(WriteStreamSuite, DrainOutputBufferCompelete) {
  frames_queued_ = 3 * cb_threshold_;
  close_dev_called_ = 0;
  // All the audio in hw buffer are extra silent frames.
  iodev_.extra_silent_frames = frames_queued_ + 1;
  unsigned int sleep_frames;
  drain_output_buffer(thread_, &iodev_, &sleep_frames);
  EXPECT_EQ(1, close_dev_called_);
}


TEST_F(WriteStreamSuite, DrainOutputBufferWaitForPlayback) {
  // Hardware buffer is full.
  frames_queued_ = buffer_frames_;
  iodev_.extra_silent_frames = 0;
  unsigned int sleep_frames;
  close_dev_called_ = 0;
  drain_output_buffer(thread_, &iodev_, &sleep_frames);
  EXPECT_EQ(0, close_dev_called_);
  EXPECT_EQ(buffer_frames_ - cb_threshold_, sleep_frames);
}

TEST_F(WriteStreamSuite, DrainOutputBufferWaitForAudio) {
  // Hardware buffer is almost empty
  frames_queued_ = 30;
  iodev_.extra_silent_frames = 0;
  unsigned int sleep_frames;
  close_dev_called_ = 0;
  drain_output_buffer(thread_, &iodev_, &sleep_frames);
  EXPECT_LT(cb_threshold_ - frames_queued_, frames_written_);
  EXPECT_EQ(0, close_dev_called_);
  // We should sleep a little more than the audio data in the buffer.
  EXPECT_GT(frames_queued_ + CAP_EXTRA_SLEEP_FRAMES, sleep_frames);
  EXPECT_LE(frames_queued_, sleep_frames);
}

TEST_F(WriteStreamSuite, DrainOutputStream) {
  struct timespec ts;
  int rc;

  // Have 3 * cb_threshold samples in the hw buffer.
  // Have 4 * cb_threshold samples in the first stream's shm
  // Note: used_size = 5 * cb_threshold.
  frames_queued_ = 3 * cb_threshold_;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;
  shm_->area->write_offset[0] = 4 * cb_threshold_ * 4;

  is_open_ = 1;
  close_dev_called_ = 0;
  open_dev_called_ = 0;

  thread_disconnect_stream(thread_, rstream_);

  // We should be draining the audio.
  EXPECT_EQ(0, close_dev_called_);
  EXPECT_EQ(0, open_dev_called_);

  rc = unified_io(thread_, &ts);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(2 * cb_threshold_, frames_written_);
  EXPECT_EQ(0, open_dev_called_);
  EXPECT_EQ(0, close_dev_called_);

  // Clear the hardware buffer
  frames_queued_ = 0;
  frames_written_ = 0;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;

  rc = unified_io(thread_, &ts);

  // Verified that all data in stream1 is written.
  // The device is not closed before we have played all the content.
  EXPECT_EQ(0, rc);
  EXPECT_EQ(2 * cb_threshold_, frames_written_);
  EXPECT_EQ(1, cras_rstream_destroy_called);
  EXPECT_EQ(0, close_dev_called_);
  EXPECT_EQ(1, iodev_.is_draining);

  // Clear the hardware buffer again.
  frames_queued_ = 0;
  frames_written_ = 0;
  audio_buffer_size_ = buffer_frames_ - frames_queued_;

  rc = unified_io(thread_, &ts);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, close_dev_called_);
  EXPECT_EQ(0, thread_->buffer_frames[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(0, thread_->cb_threshold[CRAS_STREAM_OUTPUT]);
}

//  Test adding and removing streams.
class AddStreamSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      memset(&fmt_, 0, sizeof(fmt_));
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;

      memset(&iodev_, 0, sizeof(iodev_));
      iodev_.format = &fmt_;
      iodev_.buffer_size = 16384;
      used_size_ = 480;
      cb_threshold_ = 96;
      iodev_.direction = CRAS_STREAM_OUTPUT;

      iodev_.is_open = is_open;
      iodev_.open_dev = open_dev;
      iodev_.close_dev = close_dev;

      is_open_ = 0;
      is_open_called_ = 0;
      open_dev_called_ = 0;
      close_dev_called_ = 0;
      open_dev_return_val_ = 0;

      cras_iodev_set_format_called = 0;
      cras_rstream_destroy_called = 0;
      cras_metrics_log_histogram_called = 0;
      cras_metrics_log_histogram_name = NULL;
      cras_metrics_log_histogram_sample = 0;
    }

    virtual void TearDown() {
    }

    unsigned int GetCaptureSleepFrames() {
      // Account for padding the sleep interval to ensure the wake up happens
      // after the last desired frame is received.
      return cb_threshold_ + 16;
    }

    // Stub functions for the iodev structure.
    static int is_open(const cras_iodev* iodev) {
      is_open_called_++;
      return is_open_;
    }

    static int open_dev(cras_iodev* iodev) {
      open_dev_called_++;
      is_open_ = true;
      return open_dev_return_val_;
    }

    static int close_dev(cras_iodev* iodev) {
      close_dev_called_++;
      is_open_ = false;
      return 0;
    }

    void add_rm_two_streams(CRAS_STREAM_DIRECTION direction) {
      int rc;
      struct cras_rstream *new_stream, *second_stream;
      cras_audio_shm *shm;
      struct cras_audio_format *fmt;
      struct audio_thread thread;

      memset(&thread, 0, sizeof(thread));

      fmt = (struct cras_audio_format *)malloc(sizeof(*fmt));
      memcpy(fmt, &fmt_, sizeof(fmt_));
      iodev_.format = fmt;
      iodev_.direction = direction;
      new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
      new_stream->fd = 55;
      new_stream->buffer_frames = 65;
      new_stream->cb_threshold = 80;
      new_stream->direction = direction;
      memcpy(&new_stream->format, fmt, sizeof(*fmt));
      shm = cras_rstream_output_shm(new_stream);
      shm->area = (struct cras_audio_shm_area *)calloc(1, sizeof(*shm->area));

      if (direction == CRAS_STREAM_INPUT)
        thread_add_active_dev(&thread, &iodev_);
      else
        thread_add_active_dev(&thread, &iodev_);

      thread_add_stream(&thread, new_stream);
      EXPECT_EQ(1, thread.devs_open[direction]);
      EXPECT_EQ(1, open_dev_called_);
      EXPECT_EQ(65, thread.buffer_frames[direction]);
      if (direction == CRAS_STREAM_OUTPUT)
          EXPECT_EQ(32, thread.cb_threshold[direction]);
      else
	  EXPECT_EQ(80, thread.cb_threshold[direction]);

      is_open_ = 1;

      second_stream = (struct cras_rstream *)calloc(1, sizeof(*second_stream));
      second_stream->fd = 56;
      second_stream->buffer_frames = 25;
      second_stream->cb_threshold = 12;
      second_stream->direction = direction;
      memcpy(&second_stream->format, fmt, sizeof(*fmt));
      shm = cras_rstream_output_shm(second_stream);
      shm->area = (struct cras_audio_shm_area *)calloc(1, sizeof(*shm->area));

      is_open_called_ = 0;
      thread_add_stream(&thread, second_stream);
      EXPECT_EQ(1, thread.devs_open[direction]);
      EXPECT_EQ(1, open_dev_called_);
      EXPECT_EQ(25, thread.buffer_frames[direction]);
      EXPECT_EQ(12, thread.cb_threshold[direction]);

      //  Remove the streams.
      rc = thread_remove_stream(&thread, second_stream);
      EXPECT_EQ(1, rc);
      EXPECT_EQ(0, close_dev_called_);
      if (direction == CRAS_STREAM_OUTPUT)
          EXPECT_EQ(32, thread.cb_threshold[direction]);
      else
          EXPECT_EQ(80, thread.cb_threshold[direction]);

      rc = thread_remove_stream(&thread, new_stream);
      EXPECT_EQ(0, rc);

      // For output stream, we enter the draining mode;
      // for input stream, we close the device directly.
      if (direction == CRAS_STREAM_INPUT) {
        EXPECT_EQ(0, thread.devs_open[direction]);
        EXPECT_EQ(0, thread.buffer_frames[direction]);
        EXPECT_EQ(0, thread.cb_threshold[direction]);
      } else {
        EXPECT_EQ(1, iodev_.is_draining);
      }

      free(fmt);
      shm = cras_rstream_output_shm(new_stream);
      free(shm->area);
      free(new_stream);
      shm = cras_rstream_output_shm(second_stream);
      free(shm->area);
      free(second_stream);
    }

  struct cras_iodev iodev_;
  static int is_open_;
  static int is_open_called_;
  static int open_dev_called_;
  static int open_dev_return_val_;
  static int close_dev_called_;
  static int used_size_;
  static int cb_threshold_;
  struct cras_audio_format fmt_;
};

int AddStreamSuite::is_open_ = 0;
int AddStreamSuite::is_open_called_ = 0;
int AddStreamSuite::open_dev_called_ = 0;
int AddStreamSuite::open_dev_return_val_ = 0;
int AddStreamSuite::close_dev_called_ = 0;
int AddStreamSuite::used_size_ = 0;
int AddStreamSuite::cb_threshold_ = 0;

TEST_F(AddStreamSuite, SimpleAddOutputStream) {
  int rc;
  cras_rstream* new_stream;
  cras_audio_shm *shm;
  struct audio_thread thread;

  memset(&thread, 0, sizeof(thread));

  iodev_.format = &fmt_;
  new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
  new_stream->client = (struct cras_rclient* )this;
  new_stream->fd = 55;
  new_stream->buffer_frames = 65;
  new_stream->cb_threshold = 80;
  memcpy(&new_stream->format, &fmt_, sizeof(fmt_));

  shm = cras_rstream_output_shm(new_stream);
  shm->area = (struct cras_audio_shm_area *)calloc(1, sizeof(*shm->area));

  thread_add_active_dev(&thread, &iodev_);

  rc = thread_add_stream(&thread, new_stream);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(1, thread.devs_open[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(1, open_dev_called_);
  EXPECT_EQ(65, thread.buffer_frames[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(32, thread.cb_threshold[CRAS_STREAM_OUTPUT]);

  is_open_ = 1;

  //  remove the stream.
  rc = thread_remove_stream(&thread, new_stream);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, iodev_.is_draining);
  EXPECT_EQ(0, cras_metrics_log_histogram_called);
  EXPECT_EQ(0, cras_rstream_destroy_called);

  rc = thread_disconnect_stream(&thread, new_stream);
  EXPECT_EQ(1, cras_rstream_destroy_called);

  free(shm->area);
  free(new_stream);
}


TEST_F(AddStreamSuite, AddStreamOpenFail) {
  struct audio_thread *thread;
  cras_rstream new_stream;
  cras_audio_shm *shm;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  thread_add_active_dev(thread, &iodev_);

  shm = cras_rstream_output_shm(&new_stream);
  shm->area = (struct cras_audio_shm_area *)calloc(1, sizeof(*shm->area));

  open_dev_return_val_ = -1;
  new_stream.direction = CRAS_STREAM_OUTPUT;
  EXPECT_EQ(AUDIO_THREAD_OUTPUT_DEV_ERROR,
            thread_add_stream(thread, &new_stream));
  EXPECT_EQ(1, open_dev_called_);
  EXPECT_EQ(1, cras_iodev_set_format_called);
  EXPECT_EQ(0, thread->streams);
  audio_thread_destroy(thread);
  free(shm->area);
}

TEST_F(AddStreamSuite, AddRmTwoOutputStreams) {
  add_rm_two_streams(CRAS_STREAM_OUTPUT);
}

TEST_F(AddStreamSuite, AddRmTwoInputStreams) {
  add_rm_two_streams(CRAS_STREAM_INPUT);
}

TEST_F(AddStreamSuite, RmStreamLogLongestTimeout) {
  int rc;
  cras_rstream* new_stream;
  cras_audio_shm *shm;
  struct audio_thread *thread;

  thread = audio_thread_create();

  iodev_.format = &fmt_;
  new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
  new_stream->fd = 55;
  new_stream->buffer_frames = 65;
  new_stream->cb_threshold = 80;
  memcpy(&new_stream->format, &fmt_, sizeof(fmt_));

  shm = cras_rstream_output_shm(new_stream);
  shm->area = (struct cras_audio_shm_area *)calloc(1, sizeof(*shm->area));

  thread_add_active_dev(thread, &iodev_);
  rc = thread_add_stream(thread, new_stream);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(1, thread->devs_open[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(1, open_dev_called_);
  EXPECT_EQ(65, thread->buffer_frames[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(32, thread->cb_threshold[CRAS_STREAM_OUTPUT]);

  is_open_ = 1;
  cras_shm_set_longest_timeout(shm, 90);

  //  remove the stream.
  rc = thread_remove_stream(thread, new_stream);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, iodev_.is_draining);

  cras_system_add_select_fd_callback(cras_system_add_select_fd_callback_data);

  EXPECT_EQ(1, cras_metrics_log_histogram_called);
  EXPECT_STREQ(kStreamTimeoutMilliSeconds, cras_metrics_log_histogram_name);
  EXPECT_EQ(90, cras_metrics_log_histogram_sample);

  free(shm->area);
  free(new_stream);
  audio_thread_destroy(thread);
}

class ActiveDevicesSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      memset(&fmt_, 0, sizeof(fmt_));
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;

      memset(&iodev_, 0, sizeof(iodev_));
      memset(&iodev2_, 0, sizeof(iodev2_));
      iodev_.close_dev = close_dev;
      iodev_.is_open = is_open;
      iodev_.open_dev = open_dev;
      iodev_.delay_frames = delay_frames;
      iodev_.get_buffer = get_buffer;
      iodev_.put_buffer = put_buffer;
      iodev_.frames_queued = frames_queued;
      iodev_.delay_frames = delay_frames;
      iodev_.dev_running = dev_running;
      iodev_.format = &fmt_;
      iodev_.buffer_size = 2048;
      iodev2_.close_dev = close_dev;
      iodev2_.is_open = is_open;
      iodev2_.open_dev = open_dev;
      iodev2_.delay_frames = delay_frames;
      iodev2_.get_buffer = get_buffer;
      iodev2_.put_buffer = put_buffer;
      iodev2_.frames_queued = frames_queued;
      iodev2_.delay_frames = delay_frames;
      iodev2_.dev_running = dev_running;
      iodev2_.format = &fmt_;
      iodev2_.buffer_size = 2048;
      thread_ = audio_thread_create();
      ASSERT_TRUE(thread_);

      buffer_frames_ = 500;
      cb_threshold_ = 250;
      SetupRstream(&rstream_);
      SetupRstream(&rstream2_);
      rstream2_->buffer_frames -= 50;
      rstream2_->cb_threshold -= 50;

      dummy_audio_area1 = (cras_audio_area*)calloc(1,
          sizeof(cras_audio_area) + 2 * sizeof(cras_channel_area));
      dummy_audio_area1->num_channels = 2;
      channel_area_set_channel(&dummy_audio_area1->channels[0], CRAS_CH_FL);
      channel_area_set_channel(&dummy_audio_area1->channels[1], CRAS_CH_FR);
      rstream_->input_audio_area = dummy_audio_area1;
      dummy_audio_area2 = (cras_audio_area*)calloc(1,
          sizeof(cras_audio_area) + 2 * sizeof(cras_channel_area));
      dummy_audio_area2->num_channels = 2;
      channel_area_set_channel(&dummy_audio_area2->channels[0], CRAS_CH_FL);
      channel_area_set_channel(&dummy_audio_area2->channels[1], CRAS_CH_FR);
      rstream2_->input_audio_area = dummy_audio_area2;

      cras_iodev_set_format_called = 0;
      close_dev_called_ = 0;
      is_open_ = 0;
      cras_fmt_conversion_needed_return_val = 0;
      open_dev_val_idx_ = 0;
      delay_frames_val_idx_ = 0;
      frames_queued_val_idx_ = 0;
      frames_queued_[0] = 250;
      frames_queued_[1] = 250;
      get_buffer_val_idx_ = 0;
      put_buffer_val_idx_ = 0;
      for (int i = 0; i < 8; i++) {
        open_dev_val_[i] = 0;
        delay_frames_[i] = 0;
        audio_buffer_size_[i] = 250;
        get_buffer_rc_[i] = 0;
        put_buffer_rc_[i] = 0;
      }
    }

    virtual void TearDown() {
      struct cras_audio_shm *shm;
      audio_thread_destroy(thread_);
      shm = cras_rstream_output_shm(rstream_);
      free(shm->area);
      free(rstream_);
      free(dummy_audio_area1);
      free(dummy_audio_area2);
    }

    void SetupRstream(struct cras_rstream **rstream) {
      struct cras_audio_shm *shm;
      *rstream = (struct cras_rstream *)calloc(1, sizeof(**rstream));
      memcpy(&(*rstream)->format, &fmt_, sizeof(fmt_));
      (*rstream)->direction = CRAS_STREAM_OUTPUT;
      (*rstream)->buffer_frames = buffer_frames_;
      (*rstream)->cb_threshold = cb_threshold_;
      shm = cras_rstream_output_shm(*rstream);
      shm->area = (struct cras_audio_shm_area *)calloc(1,
          sizeof(*shm->area) + cb_threshold_ * 8);
      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(
          shm, buffer_frames_ * cras_shm_frame_bytes(shm));
      shm = cras_rstream_input_shm(*rstream);
      shm->area = (struct cras_audio_shm_area *)calloc(1,
	  sizeof(*shm->area) + buffer_frames_ * 8);
      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(
          shm, cb_threshold_* cras_shm_frame_bytes(shm));
    }

    static int close_dev(struct cras_iodev *iodev) {
      close_dev_called_++;
      return 0;
    }

    static int is_open(const cras_iodev *iodev) {
      return is_open_;
    }

    static int open_dev(struct cras_iodev *iodev) {
      open_dev_val_idx_ %= 8;
      is_open_ = 1;
      return open_dev_val_[open_dev_val_idx_++];
    }

    static int delay_frames(const cras_iodev* iodev) {
      delay_frames_val_idx_ %= 8;
      return delay_frames_[delay_frames_val_idx_++];
    }

    static int dev_running(const cras_iodev* iodev) {
      return 1;
    }

    static int frames_queued(const cras_iodev* iodev) {
      frames_queued_val_idx_ %= 2;
      return frames_queued_[frames_queued_val_idx_++];
    }

    static int get_buffer(cras_iodev* iodev,
                          struct cras_audio_area** area,
                          unsigned int* num) {
      size_t sz = sizeof(*area_) + sizeof(struct cras_channel_area) * 2;

      get_buffer_val_idx_ %= 8;
      if (audio_buffer_size_[get_buffer_val_idx_] < *num)
	      *num = audio_buffer_size_[get_buffer_val_idx_];

      area_ = (cras_audio_area*)calloc(1, sz);
      area_->frames = *num;
      area_->num_channels = 2;
      area_->channels[0].buf = audio_buffer_[get_buffer_val_idx_];
      channel_area_set_channel(&area_->channels[0], CRAS_CH_FL);
      area_->channels[0].step_bytes = 4;
      area_->channels[1].buf = audio_buffer_[get_buffer_val_idx_] + 2;
      channel_area_set_channel(&area_->channels[1], CRAS_CH_FR);
      area_->channels[1].step_bytes = 4;

      *area = area_;

      get_buffer_val_idx_++;
      return 0;
    }

    static int put_buffer(cras_iodev* iodev,
		          unsigned int num) {
      free(area_);
      put_buffer_val_idx_ %= 8;
      return put_buffer_rc_[put_buffer_val_idx_++];
    }

  static int is_open_;
  static int open_dev_val_[8];
  static int open_dev_val_idx_;
  static int close_dev_called_;
  static int buffer_frames_;
  static int cb_threshold_;
  static int frames_queued_[2];
  static int frames_queued_val_idx_;
  static uint8_t audio_buffer_[8][8192];
  static unsigned int audio_buffer_size_[8];
  static int get_buffer_rc_[8];
  static int put_buffer_rc_[8];
  static int get_buffer_val_idx_;
  static int put_buffer_val_idx_;
  struct cras_iodev iodev_;
  struct cras_iodev iodev2_;
  struct cras_audio_format fmt_;
  struct cras_rstream *rstream_;
  struct cras_rstream *rstream2_;
  struct audio_thread *thread_;
  static struct cras_audio_area *area_;
  static int delay_frames_val_idx_;
  static int delay_frames_[8];
};

int ActiveDevicesSuite::is_open_ = 0;
int ActiveDevicesSuite::buffer_frames_ = 0;
int ActiveDevicesSuite::cb_threshold_ = 0;
int ActiveDevicesSuite::frames_queued_[2];
int ActiveDevicesSuite::frames_queued_val_idx_;
int ActiveDevicesSuite::close_dev_called_ = 0;
int ActiveDevicesSuite::open_dev_val_[8];
int ActiveDevicesSuite::open_dev_val_idx_ = 0;
int ActiveDevicesSuite::delay_frames_val_idx_ = 0;
int ActiveDevicesSuite::delay_frames_[8];
uint8_t ActiveDevicesSuite::audio_buffer_[8][8192];
unsigned int ActiveDevicesSuite::audio_buffer_size_[8];
int ActiveDevicesSuite::get_buffer_val_idx_ = 0;
int ActiveDevicesSuite::put_buffer_val_idx_ = 0;
int ActiveDevicesSuite::get_buffer_rc_[8];
int ActiveDevicesSuite::put_buffer_rc_[8];
struct cras_audio_area *ActiveDevicesSuite::area_;

TEST_F(ActiveDevicesSuite, AddRemoveActiveDevice) {
  struct active_dev *adevs;
  iodev_.direction = CRAS_STREAM_INPUT;
  iodev2_.direction = CRAS_STREAM_INPUT;

  thread_add_active_dev(thread_, &iodev_);
  adevs = thread_->active_devs[CRAS_STREAM_INPUT];
  EXPECT_NE((void *)NULL, adevs);
  EXPECT_EQ(adevs->dev, &iodev_);
  EXPECT_EQ(1, iodev_.is_active);

  thread_add_active_dev(thread_, &iodev2_);
  EXPECT_NE((void *)NULL, adevs->next);
  EXPECT_EQ(adevs->next->dev, &iodev2_);
  EXPECT_EQ(1, iodev2_.is_active);

  thread_rm_active_dev(thread_, &iodev_);
  adevs = thread_->active_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ((void *)NULL, adevs->next);
  EXPECT_EQ(adevs->dev, &iodev2_);
  EXPECT_EQ(0, iodev_.is_active);

  iodev_.direction = CRAS_STREAM_POST_MIX_PRE_DSP;
  thread_add_active_dev(thread_, &iodev_);
  EXPECT_NE((void *)NULL, thread_->active_devs[CRAS_STREAM_POST_MIX_PRE_DSP]);
  EXPECT_EQ(1, iodev_.is_active);
}

TEST_F(ActiveDevicesSuite, ClearActiveDevices) {
  iodev_.direction = CRAS_STREAM_OUTPUT;
  iodev2_.direction = CRAS_STREAM_OUTPUT;

  thread_add_active_dev(thread_, &iodev_);
  thread_add_active_dev(thread_, &iodev2_);
  EXPECT_NE((void *)NULL, thread_->active_devs[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(1, iodev_.is_active);
  EXPECT_EQ(1, iodev2_.is_active);

  thread_clear_active_devs(thread_, CRAS_STREAM_OUTPUT);
  EXPECT_EQ((void *)NULL, thread_->active_devs[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(0, iodev_.is_active);
  EXPECT_EQ(0, iodev2_.is_active);
}

TEST_F(ActiveDevicesSuite, OpenActiveDevices) {
  iodev_.direction = CRAS_STREAM_OUTPUT;
  iodev2_.direction = CRAS_STREAM_OUTPUT;

  thread_add_active_dev(thread_, &iodev_);
  thread_add_active_dev(thread_, &iodev2_);
  thread_add_stream(thread_, rstream_);

  EXPECT_EQ(2, cras_iodev_set_format_called);
  EXPECT_EQ(500, thread_->buffer_frames[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(250, thread_->cb_threshold[CRAS_STREAM_OUTPUT]);
}

TEST_F(ActiveDevicesSuite, OpenFirstActiveDeviceFail) {
  int rc;
  iodev_.direction = CRAS_STREAM_OUTPUT;
  iodev2_.direction = CRAS_STREAM_OUTPUT;

  thread_add_active_dev(thread_, &iodev_);
  thread_add_active_dev(thread_, &iodev2_);

  open_dev_val_[0] = -1;
  rc = thread_add_stream(thread_, rstream_);
  EXPECT_EQ(rc, AUDIO_THREAD_OUTPUT_DEV_ERROR);
  EXPECT_EQ(1, cras_iodev_set_format_called);
  EXPECT_EQ(0, thread_->buffer_frames[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(0, thread_->cb_threshold[CRAS_STREAM_OUTPUT]);
}

TEST_F(ActiveDevicesSuite, OpenSecondActiveDeviceFail) {
  int rc;
  iodev_.direction = CRAS_STREAM_OUTPUT;
  iodev2_.direction = CRAS_STREAM_OUTPUT;

  thread_add_active_dev(thread_, &iodev_);
  thread_add_active_dev(thread_, &iodev2_);

  open_dev_val_[1] = -1;
  rc = thread_add_stream(thread_, rstream_);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(2, cras_iodev_set_format_called);
  EXPECT_EQ(500, thread_->buffer_frames[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(250, thread_->cb_threshold[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(0, close_dev_called_);
  EXPECT_EQ((void *)NULL, thread_->active_devs[CRAS_STREAM_OUTPUT]->next);
}

TEST_F(ActiveDevicesSuite, OpenSecondActiveDeviceFormatIncompatible) {
  int rc;
  iodev_.direction = CRAS_STREAM_OUTPUT;
  iodev2_.direction = CRAS_STREAM_OUTPUT;

  thread_add_active_dev(thread_, &iodev_);
  thread_add_active_dev(thread_, &iodev2_);

  cras_fmt_conversion_needed_return_val = 1;
  rc = thread_add_stream(thread_, rstream_);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(2, cras_iodev_set_format_called);
  EXPECT_EQ(500, thread_->buffer_frames[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(250, thread_->cb_threshold[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(1, close_dev_called_);
  EXPECT_EQ((void *)NULL, thread_->active_devs[CRAS_STREAM_OUTPUT]->next);
}

TEST_F(ActiveDevicesSuite, CloseActiveDevices) {
  iodev_.direction = CRAS_STREAM_OUTPUT;
  iodev2_.direction = CRAS_STREAM_OUTPUT;

  thread_add_active_dev(thread_, &iodev_);
  thread_add_active_dev(thread_, &iodev2_);

  thread_add_stream(thread_, rstream_);
  EXPECT_EQ(1, thread_->devs_open[CRAS_STREAM_OUTPUT]);
  EXPECT_EQ(2, cras_iodev_set_format_called);

  thread_add_stream(thread_, rstream2_);
  EXPECT_EQ(2, cras_iodev_set_format_called);

  thread_remove_stream(thread_, rstream2_);

  thread_remove_stream(thread_, rstream_);
  EXPECT_EQ(1, iodev_.is_draining);
  EXPECT_EQ(1, iodev2_.is_draining);
}

TEST_F(ActiveDevicesSuite, InputDelayFrames) {
  int fr;
  iodev_.direction = CRAS_STREAM_INPUT;
  iodev2_.direction = CRAS_STREAM_INPUT;

  thread_add_active_dev(thread_, &iodev_);
  thread_add_active_dev(thread_, &iodev2_);

  thread_add_stream(thread_, rstream_);
  delay_frames_[0] = 3;
  delay_frames_[1] = 33;
  fr = input_delay_frames(thread_->active_devs[CRAS_STREAM_INPUT]);
  EXPECT_EQ(33, fr);

  delay_frames_val_idx_ = 0;
  delay_frames_[1] = -1;
  fr = input_delay_frames(thread_->active_devs[CRAS_STREAM_INPUT]);
  EXPECT_EQ(-1, fr);
}

TEST_F(ActiveDevicesSuite, InputFramesQueued) {
  int fr;
  iodev_.direction = CRAS_STREAM_INPUT;
  iodev2_.direction = CRAS_STREAM_INPUT;
  thread_add_active_dev(thread_, &iodev_);
  thread_add_active_dev(thread_, &iodev2_);
  frames_queued_val_idx_ = 0;
  frames_queued_[0] = 195;
  frames_queued_[1] = 190;
  fr = input_min_frames_queued(thread_->active_devs[CRAS_STREAM_INPUT]);
  EXPECT_EQ(190, fr);

  /* Test error path. */
  frames_queued_val_idx_ = 0;
  frames_queued_[0] = -1;
  frames_queued_[1] = 190;
  fr = input_min_frames_queued(thread_->active_devs[CRAS_STREAM_INPUT]);
  EXPECT_EQ(-1, fr);
}

TEST_F(ActiveDevicesSuite, MixMultipleInputs) {
  struct timespec ts;
  struct cras_audio_shm *shm;
  int16_t *out_buff;

  iodev_.direction = CRAS_STREAM_INPUT;
  iodev2_.direction = CRAS_STREAM_INPUT;
  rstream_->direction = CRAS_STREAM_INPUT;
  rstream2_->direction = CRAS_STREAM_INPUT;

  for (unsigned int dev = 0; dev < 8; dev++) {
    int16_t *buff = (int16_t*)audio_buffer_[dev];
    for (unsigned int i = 0; i < 250; i++)
      buff[i] = i;
  }
  thread_add_active_dev(thread_, &iodev_);
  thread_add_active_dev(thread_, &iodev2_);

  /* Assert shm from rstream_ is used. */
  thread_add_stream(thread_, rstream_);
  unified_io(thread_, &ts);
  shm = cras_rstream_input_shm(rstream_);
  out_buff = (int16_t*)shm->area->samples;
  for (unsigned int i = 0; i < 250; i++)
    EXPECT_EQ(i * 2, out_buff[i]);

  thread_add_stream(thread_, rstream2_);
  unified_io(thread_, &ts);
  shm = cras_rstream_input_shm(rstream2_);
  out_buff = (int16_t*)shm->area->samples;
  for (unsigned int i = 0; i < 250; i++)
    EXPECT_EQ(i * 2, out_buff[i]);
}

extern "C" {

const char kNoCodecsFoundMetric[] = "Cras.NoCodecsFoundAtBoot";
const char kStreamTimeoutMilliSeconds[] = "Cras.StreamTimeoutMilliSeconds";

int cras_iodev_get_thread_poll_fd(const struct cras_iodev *iodev) {
  return 0;
}

int cras_iodev_read_thread_command(struct cras_iodev *iodev,
				   uint8_t *buf,
				   size_t max_len) {
  return 0;
}

int cras_iodev_send_command_response(struct cras_iodev *iodev, int rc) {
  return 0;
}

void cras_iodev_fill_time_from_frames(size_t frames,
                                      size_t frame_rate,
                                      struct timespec *ts) {
	uint64_t to_play_usec;

	ts->tv_sec = 0;
	/* adjust sleep time to target our callback threshold */
	to_play_usec = (uint64_t)frames * 1000000L / (uint64_t)frame_rate;

	while (to_play_usec > 1000000) {
		ts->tv_sec++;
		to_play_usec -= 1000000;
	}
	ts->tv_nsec = to_play_usec * 1000;
}

void cras_iodev_set_playback_timestamp(size_t frame_rate,
                                       size_t frames,
                                       struct cras_timespec *ts) {
  cras_iodev_set_playback_timestamp_called++;
}

void cras_iodev_set_capture_timestamp(size_t frame_rate,
                                      size_t frames,
                                      struct cras_timespec *ts) {
}

int cras_iodev_set_format(struct cras_iodev *iodev,
                          struct cras_audio_format *fmt)
{
  cras_iodev_set_format_called++;
  return 0;
}

//  From mixer.
size_t cras_mix_add_stream(struct cras_audio_shm *shm,
                           size_t num_channels,
                           uint8_t *dst,
                           size_t *count,
                           size_t *index) {
  int16_t *src;
  int16_t *target = (int16_t *)dst;
  size_t fr_written, fr_in_buf;
  size_t num_samples;
  size_t frames = 0;

  if (cras_mix_add_stream_dont_fill_next) {
    cras_mix_add_stream_dont_fill_next = 0;
    return 0;
  }
  cras_mix_add_stream_count = *count;

  /* We only copy the data from shm to dst, not actually mix them. */
  fr_in_buf = cras_shm_get_frames(shm);
  if (fr_in_buf == 0)
    return 0;
  if (fr_in_buf < *count)
    *count = fr_in_buf;

  fr_written = 0;
  while (fr_written < *count) {
    src = cras_shm_get_readable_frames(shm, fr_written,
                                       &frames);
    if (frames > *count - fr_written)
      frames = *count - fr_written;
    num_samples = frames * num_channels;
    memcpy(target, src, num_samples * 2);
    fr_written += frames;
    target += num_samples;
  }

  *index = *index + 1;
  return *count;
}

void cras_scale_buffer(int16_t *buffer, unsigned int count, float scaler) {
}

size_t cras_mix_mute_buffer(uint8_t *dst,
                            size_t frame_bytes,
                            size_t count) {
  cras_mix_mute_count = count;
  return count;
}

void cras_mix_add_clip(int16_t *dst, const int16_t *src, size_t count) {
  int32_t sum;
  unsigned int i;

  for (i = 0; i < count; i++) {
    sum = dst[i] + src[i];
    if (sum > 0x7fff)
      sum = 0x7fff;
    else if (sum < -0x8000)
      sum = -0x8000;
    dst[i] = sum;
  }
}

// From cras_metrics.c
void cras_metrics_log_event(const char *event)
{
  cras_metrics_log_event_called++;
}

void cras_metrics_log_histogram(const char *name, int sample, int min,
				int max, int nbuckets)
{
  cras_metrics_log_histogram_called++;
  cras_metrics_log_histogram_name = name;
  cras_metrics_log_histogram_sample = sample;
}

//  From util.
int cras_set_rt_scheduling(int rt_lim) {
  return 0;
}

int cras_set_thread_priority(int priority) {
  return 0;
}

//  From rstream.
int cras_rstream_request_audio(const struct cras_rstream *stream)
{
  cras_rstream_request_audio_called++;
  return 0;
}

int cras_rstream_get_audio_request_reply(const struct cras_rstream *stream) {
  return 0;
}

int cras_rstream_audio_ready(const struct cras_rstream *stream, size_t count) {
  cras_rstream_audio_ready_called++;
  cras_rstream_audio_ready_count = count;
  return 0;
}

struct pipeline *cras_dsp_get_pipeline(struct cras_dsp_context *ctx)
{
  cras_dsp_get_pipeline_called++;
  return reinterpret_cast<struct pipeline *>(cras_dsp_get_pipeline_ret);
}

void cras_dsp_put_pipeline(struct cras_dsp_context *ctx)
{
  cras_dsp_put_pipeline_called++;
}

float *cras_dsp_pipeline_get_source_buffer(struct pipeline *pipeline,
					   int index)
{
  cras_dsp_pipeline_get_source_buffer_called++;
  return cras_dsp_pipeline_source_buffer[index];
}

float *cras_dsp_pipeline_get_sink_buffer(struct pipeline *pipeline, int index)
{
  cras_dsp_pipeline_get_sink_buffer_called++;
  return cras_dsp_pipeline_sink_buffer[index];
}

int cras_dsp_pipeline_get_delay(struct pipeline *pipeline)
{
  cras_dsp_pipeline_get_delay_called++;
  return 0;
}

void cras_dsp_pipeline_apply(struct pipeline *pipeline, unsigned int channels,
			     uint8_t *buf, unsigned int frames)
{
  cras_dsp_pipeline_apply_called++;
  cras_dsp_pipeline_apply_sample_count = frames;
}

void cras_rstream_send_client_reattach(const struct cras_rstream *stream)
{
}

void cras_dsp_pipeline_add_statistic(struct pipeline *pipeline,
                                     const struct timespec *time_delta,
                                     int samples)
{
}

void cras_rstream_log_overrun(const struct cras_rstream *stream) {
}

int cras_system_add_select_fd(int fd,
			      void (*callback)(void *data),
			      void *callback_data)
{
  cras_system_add_select_fd_callback = callback;
  cras_system_add_select_fd_callback_data = callback_data;
  return 0;
}

void cras_system_rm_select_fd(int fd)
{
}

size_t cras_system_get_volume() {
  return cras_system_get_volume_return;
}

int cras_system_get_mute() {
  return 0;
}

int cras_system_get_capture_mute() {
  return 0;
}

void cras_rstream_destroy(struct cras_rstream *stream) {
  cras_rstream_destroy_called++;
}

void loopback_iodev_set_format(struct cras_iodev *loopback_dev,
                               const struct cras_audio_format *fmt) {
}

int loopback_iodev_add_audio(struct cras_iodev *loopback_dev,
                             const uint8_t *audio,
                             unsigned int count) {
  return 0;
}

int loopback_iodev_add_zeros(struct cras_iodev *dev,
                             unsigned int count) {
  return 0;
}

int cras_fmt_conversion_needed(const struct cras_audio_format *a,
			       const struct cras_audio_format *b)
{
  return cras_fmt_conversion_needed_return_val;
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area *area,
                                         const struct cras_audio_format *fmt,
                                         uint8_t *base_buffer) {
  unsigned int i;
  const int sample_size = snd_pcm_format_physical_width(fmt->format) / 8;

  /* TODO(dgreid) - assuming interleaved audio here for now. */
  for (i = 0 ; i < area->num_channels; i++) {
    area->channels[i].step_bytes = cras_get_format_bytes(fmt);
    area->channels[i].buf = base_buffer + i * sample_size;
  }
}

//  Override select so it can be stubbed.
int select(int nfds,
           fd_set *readfds,
           fd_set *writefds,
           fd_set *exceptfds,
           struct timeval *timeout) {
  select_max_fd = nfds;
  select_timeval.tv_sec = timeout->tv_sec;
  select_timeval.tv_usec = timeout->tv_usec;
  select_in_fds = *readfds;
  *readfds = select_out_fds;
  if (select_write_ptr)
	  *select_write_ptr = select_write_value;
  return select_return_value;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  *tp = time_now;
  return 0;
}

}  // extern "C"

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
