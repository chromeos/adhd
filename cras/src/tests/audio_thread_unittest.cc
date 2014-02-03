// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sys/select.h>
#include <gtest/gtest.h>

extern "C" {

#include "cras_iodev.h"
#include "cras_rstream.h"
#include "cras_shm.h"
#include "cras_types.h"
#include "audio_thread.h"
#include "utlist.h"

int thread_add_stream(audio_thread* thread,
                      cras_rstream* stream);
int thread_remove_stream(audio_thread* thread,
                         cras_rstream* stream);
int unified_io(audio_thread* thread, timespec* ts);

static int cras_mix_add_stream_dont_fill_next;
static unsigned int cras_mix_add_stream_count;
static unsigned int cras_mix_mute_count;
static int cras_rstream_audio_ready_count;
static unsigned int cras_rstream_request_audio_called;
static unsigned int cras_rstream_audio_ready_called;
static int select_return_value;
static struct timeval select_timeval;
static int select_max_fd;
static fd_set select_in_fds;
static fd_set select_out_fds;
static uint32_t *select_write_ptr;
static uint32_t select_write_value;
static unsigned int cras_iodev_config_params_for_streams_called;
static unsigned int cras_iodev_config_params_for_streams_buffer_size;
static unsigned int cras_iodev_config_params_for_streams_threshold;
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

// Stub volume scalers.
float softvol_scalers[101];

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
      iodev_.cb_threshold = 480;
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
    }

    void SetupRstream(struct cras_rstream **rstream, int fd) {
      struct cras_audio_shm *shm;

      *rstream = (struct cras_rstream *)calloc(1, sizeof(**rstream));
      memcpy(&(*rstream)->format, &fmt_, sizeof(fmt_));
      (*rstream)->direction = CRAS_STREAM_INPUT;
      (*rstream)->cb_threshold = iodev_.cb_threshold;

      shm = cras_rstream_input_shm(*rstream);
      shm->area = (struct cras_audio_shm_area *)calloc(1,
          sizeof(*shm->area) + iodev_.cb_threshold * 8);
      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(
          shm, iodev_.cb_threshold * cras_shm_frame_bytes(shm));
    }

    unsigned int GetCaptureSleepFrames() {
      // Account for padding the sleep interval to ensure the wake up happens
      // after the last desired frame is received.
      return iodev_.cb_threshold + 16;
    }

    // Stub functions for the iodev structure.
    static int frames_queued(const cras_iodev* iodev) {
      return frames_queued_;
    }

    static int delay_frames(const cras_iodev* iodev) {
      return delay_frames_;
    }

    static int get_buffer(cras_iodev* iodev,
                          uint8_t** buf,
                          unsigned int* num) {
      *buf = audio_buffer_;
      if (audio_buffer_size_ < *num)
	      *num = audio_buffer_size_;
      return 0;
    }

    static int put_buffer(cras_iodev* iodev,
                          unsigned int num) {
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
  static uint8_t audio_buffer_[8192];
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

TEST_F(ReadStreamSuite, PossiblyReadGetAvailError) {
  struct timespec ts;
  int rc;
  struct audio_thread *thread;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;

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
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;

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
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;

  thread_add_stream(thread, rstream_);

  frames_queued_ = iodev_.cb_threshold - num_frames_short;
  is_open_ = 1;
  audio_buffer_size_ = frames_queued_;
  nsec_expected = ((uint64_t)num_frames_short + CAP_EXTRA_SLEEP_FRAMES) *
                  1000000000ULL / (uint64_t)fmt_.frame_rate;

  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_rstream_audio_ready_called);
  /* As much data as can be, should be read. */
  EXPECT_EQ(iodev_.cb_threshold - num_frames_short,
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
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;

  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = iodev_.cb_threshold + 4;
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
  EXPECT_EQ(iodev_.cb_threshold, cras_rstream_audio_ready_count);
  EXPECT_EQ(1, cras_rstream_audio_ready_called);
  EXPECT_EQ(0, cras_iodev_set_playback_timestamp_called);
  for (size_t i = 0; i < iodev_.cb_threshold; i++)
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
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;

  rc = thread_add_stream(thread, rstream_);
  EXPECT_EQ(0, rc);
  rc = thread_add_stream(thread, rstream2_);
  EXPECT_EQ(0, rc);

  //  A full block plus 4 frames.
  frames_queued_ = iodev_.cb_threshold + 4;
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
  EXPECT_EQ(iodev_.cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < iodev_.cb_threshold; i++)
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
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;

  iodev_.cb_threshold /= 2;
  rstream_->cb_threshold = iodev_.cb_threshold;

  rc = thread_add_stream(thread, rstream_);
  EXPECT_EQ(0, rc);
  rc = thread_add_stream(thread, rstream2_);
  EXPECT_EQ(0, rc);

  //  A full block plus 4 frames.
  frames_queued_ = iodev_.cb_threshold + 4;
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
  EXPECT_EQ(iodev_.cb_threshold, cras_rstream_audio_ready_count);

  frames_queued_ = iodev_.cb_threshold + 5;
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
  audio_thread_set_input_dev(thread, &iodev_);
  audio_thread_set_output_dev(thread, &output_dev_);

  iodev_.thread = thread;

  iodev_.cb_threshold /= 2;
  output_dev_.cb_threshold /= 2;
  rstream_->cb_threshold = iodev_.cb_threshold;

  rc = thread_add_stream(thread, rstream_);
  EXPECT_EQ(0, rc);
  rstream2_->direction = CRAS_STREAM_UNIFIED;

  shm1 = cras_rstream_output_shm(rstream_);
  shm1->area = (struct cras_audio_shm_area *)calloc(1,
		  sizeof(*shm1->area) + iodev_.cb_threshold * 8);
  cras_shm_set_frame_bytes(shm1, 4);
  cras_shm_set_used_size(
		  shm1, iodev_.cb_threshold * cras_shm_frame_bytes(shm1));
  shm1->area->write_offset[0] = cras_shm_used_size(shm1);

  shm2 = cras_rstream_output_shm(rstream2_);
  shm2->area = (struct cras_audio_shm_area *)calloc(1,
		  sizeof(*shm2->area) + iodev_.cb_threshold * 8);
  cras_shm_set_frame_bytes(shm2, 4);
  cras_shm_set_used_size(
		  shm2, iodev_.cb_threshold * cras_shm_frame_bytes(shm2));
  shm2->area->write_offset[0] = cras_shm_used_size(shm2);
  shm2->area->write_offset[1] = cras_shm_used_size(shm2);


  rc = thread_add_stream(thread, rstream2_);
  EXPECT_EQ(0, rc);

  //  A full block plus 4 frames.
  frames_queued_ = iodev_.cb_threshold + 4;
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
  EXPECT_EQ(rstream_->cb_threshold, cras_rstream_audio_ready_count);

  frames_queued_ = iodev_.cb_threshold + 5;
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
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;

  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = iodev_.cb_threshold + 4;
  audio_buffer_size_ = frames_queued_;

  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;

  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_shm_num_overruns(shm_));
  EXPECT_EQ(rstream_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < iodev_.cb_threshold; i++)
    EXPECT_EQ(audio_buffer_[i], shm_->area->samples[i]);
  cras_shm_buffer_read(shm_, cras_rstream_audio_ready_count);

  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_shm_num_overruns(shm_));
  EXPECT_EQ(rstream_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < iodev_.cb_threshold; i++)
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
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;

  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = iodev_.cb_threshold + 4;
  audio_buffer_size_ = frames_queued_;
  is_open_ = 1;

  //  Give it some samples to copy.
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_shm_num_overruns(shm_));
  EXPECT_EQ(iodev_.cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < iodev_.cb_threshold; i++)
    EXPECT_EQ(audio_buffer_[i], shm_->area->samples[i]);
  cras_shm_buffer_read(shm_, cras_rstream_audio_ready_count);

  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_shm_num_overruns(shm_));
  EXPECT_EQ(iodev_.cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < iodev_.cb_threshold; i++)
    EXPECT_EQ(audio_buffer_[i],
        shm_->area->samples[i + cras_shm_used_size(shm_)]);

  cras_rstream_audio_ready_count = 999;
  is_open_ = 1;
  rc = unified_io(thread, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_shm_num_overruns(shm_));  //  Should have overrun.
  EXPECT_EQ(iodev_.cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < iodev_.cb_threshold; i++)
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
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;

  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = iodev_.cb_threshold + 4;
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
  audio_thread_set_input_dev(thread, &iodev_);

  iodev_.thread = thread;
  thread_add_stream(thread, rstream_);

  //  A full block plus 4 frames.
  frames_queued_ = iodev_.cb_threshold + 4;
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
  EXPECT_EQ(iodev_.cb_threshold, cras_dsp_pipeline_apply_sample_count);

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
      iodev_.used_size = 480;
      iodev_.cb_threshold = 96;
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

      SetupRstream(&rstream_, 1);
      shm_ = cras_rstream_output_shm(rstream_);
      SetupRstream(&rstream2_, 2);
      shm2_ = cras_rstream_output_shm(rstream2_);

      thread_ = audio_thread_create();
      ASSERT_TRUE(thread_);
      audio_thread_set_output_dev(thread_, &iodev_);

      iodev_.thread = thread_;
      thread_->output_dev = &iodev_;
      thread_->input_dev = NULL;

      cras_mix_add_stream_dont_fill_next = 0;
      cras_mix_add_stream_count = 0;
      select_max_fd = -1;
      select_write_ptr = NULL;
      cras_rstream_request_audio_called = 0;
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
      (*rstream)->cb_threshold = 96;

      shm = cras_rstream_output_shm(*rstream);
      shm->area = (struct cras_audio_shm_area *)calloc(1,
          sizeof(*shm->area) + iodev_.used_size * 8);
      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(
          shm, iodev_.used_size * cras_shm_frame_bytes(shm));
    }

    uint64_t GetCaptureSleepFrames() {
      // Account for padding the sleep interval to ensure the wake up happens
      // after the last desired frame is received.
      return iodev_.cb_threshold + CAP_EXTRA_SLEEP_FRAMES;
    }

    // Stub functions for the iodev structure.
    static int frames_queued(const cras_iodev* iodev) {
      return frames_queued_ + frames_written_;
    }

    static int delay_frames(const cras_iodev* iodev) {
      return delay_frames_;
    }

    static int get_buffer(cras_iodev* iodev,
                          uint8_t** buf,
                          unsigned int* num) {
      *buf = audio_buffer_;
      if (audio_buffer_size_ < *num)
	      *num = audio_buffer_size_;
      return 0;
    }

    static int put_buffer(cras_iodev* iodev,
                          unsigned int num) {
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
      return 0;
    }

    static int close_dev(cras_iodev* iodev) {
      close_dev_called_++;
      return 0;
    }

  struct cras_iodev iodev_;
  static int is_open_;
  static int frames_queued_;
  static int frames_written_;
  static int delay_frames_;
  static uint8_t audio_buffer_[8192];
  static unsigned int audio_buffer_size_;
  static int dev_running_;
  static unsigned int dev_running_called_;
  static unsigned int close_dev_called_;
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
uint8_t WriteStreamSuite::audio_buffer_[8192];
unsigned int WriteStreamSuite::audio_buffer_size_ = 0;
int WriteStreamSuite::dev_running_ = 1;
unsigned int WriteStreamSuite::dev_running_called_ = 0;
unsigned int WriteStreamSuite::close_dev_called_ = 0;

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
  frames_queued_ = iodev_.cb_threshold * 2;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;

  nsec_expected = (iodev_.cb_threshold) * 1000000000ULL /
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
  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;
  nsec_expected = (uint64_t)iodev_.cb_threshold *
      1000000000ULL / (uint64_t)fmt_.frame_rate;

  // shm has plenty of data in it.
  shm_->area->write_offset[0] = iodev_.cb_threshold * 4;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;
  is_open_ = 1;

  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(iodev_.cb_threshold, cras_mix_add_stream_count);
  EXPECT_EQ(0, cras_rstream_request_audio_called);
  EXPECT_EQ(-1, select_max_fd);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromStreamMinSet) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  // Have cb_threshold samples left.
  frames_queued_ = iodev_.cb_threshold * 2;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;
  // Setting the min_buffer_level should shorten the sleep time.
  iodev_.min_buffer_level = iodev_.cb_threshold;

  // shm has is empty.
  shm_->area->write_offset[0] = 0;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;
  is_open_ = 1;
  // Set write offset after call to select.
  select_write_ptr = &shm_->area->write_offset[0];
  select_write_value = iodev_.cb_threshold * 4;

  // After the callback there will be cb_thresh of data in the buffer and
  // cb_thresh x 2 data in the hardware (frames_queued_) = 3 cb_thresh total.
  // It should sleep until there is a total of cb_threshold + min_buffer_level
  // left,  or 3 - 2 = 1 cb_thresh worth of delay.
  nsec_expected = (uint64_t)(iodev_.cb_threshold) *
      1000000000ULL / (uint64_t)fmt_.frame_rate;

  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(iodev_.cb_threshold, cras_mix_add_stream_count);
  EXPECT_EQ(1, cras_rstream_request_audio_called);
}

TEST_F(WriteStreamSuite, PossiblyFillFramesQueued) {
  struct timespec ts;
  int rc;

  // Have cb_threshold samples left.
  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;

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
  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;

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
  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;

  //  shm is out of data.
  shm_->area->write_offset[0] = 0;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;
  // Set write offset after call to select.
  select_write_ptr = &shm_->area->write_offset[0];
  select_write_value = (iodev_.used_size - iodev_.cb_threshold) * 4;

  nsec_expected = (iodev_.used_size - iodev_.cb_threshold) *
      1000000000ULL / (uint64_t)fmt_.frame_rate;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(iodev_.used_size - iodev_.cb_threshold, cras_mix_add_stream_count);
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
  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;

  //  Software volume config.
  iodev_.software_volume_scaler = 0.5;

  //  shm is out of data.
  shm_->area->write_offset[0] = 0;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  // Set write offset after call to select.
  select_write_ptr = &shm_->area->write_offset[0];
  select_write_value = (iodev_.used_size - iodev_.cb_threshold) * 4;

  nsec_expected = (iodev_.used_size - iodev_.cb_threshold) *
      1000000000ULL / (uint64_t)fmt_.frame_rate;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(iodev_.used_size - iodev_.cb_threshold,
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
  audio_buffer_size_ = iodev_.used_size - frames_queued_;
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
  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;
  written_expected = (iodev_.used_size - iodev_.cb_threshold);

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
  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;

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
  EXPECT_EQ(iodev_.cb_threshold, cras_mix_mute_count);
}

TEST_F(WriteStreamSuite, PossiblyFillGetFromTwoStreamsOneLimited) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;
  static const unsigned int smaller_frames = 10;

  //  Have cb_threshold samples left.
  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;
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

  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;
  iodev_.dsp_context = reinterpret_cast<cras_dsp_context *>(0x5);

  //  shm has plenty of data in it.
  shm_->area->write_offset[0] = cras_shm_used_size(shm_);

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  is_open_ = 1;
  rc = unified_io(thread_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(iodev_.used_size - iodev_.cb_threshold,
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
  frames_queued_ = iodev_.cb_threshold;
  audio_buffer_size_ = iodev_.used_size - frames_queued_;
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
  EXPECT_EQ(iodev_.used_size - iodev_.cb_threshold,
            cras_mix_add_stream_count);
  EXPECT_EQ(2, cras_dsp_get_pipeline_called);
  EXPECT_EQ(2, cras_dsp_put_pipeline_called);
  EXPECT_EQ(1, cras_dsp_pipeline_get_delay_called);
  EXPECT_EQ(1, cras_dsp_pipeline_apply_called);
  EXPECT_EQ(iodev_.used_size - iodev_.cb_threshold,
            cras_dsp_pipeline_apply_sample_count);
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
      iodev_.used_size = 480;
      iodev_.cb_threshold = 96;
      iodev_.direction = CRAS_STREAM_OUTPUT;

      iodev_.is_open = is_open;
      iodev_.open_dev = open_dev;
      iodev_.close_dev = close_dev;

      is_open_ = 0;
      is_open_called_ = 0;
      open_dev_called_ = 0;
      close_dev_called_ = 0;
      open_dev_return_val_ = 0;

      cras_iodev_config_params_for_streams_called = 0;
      cras_iodev_config_params_for_streams_buffer_size = 0;
      cras_iodev_config_params_for_streams_threshold = 0;
      cras_iodev_set_format_called = 0;
    }

    virtual void TearDown() {
    }

    unsigned int GetCaptureSleepFrames() {
      // Account for padding the sleep interval to ensure the wake up happens
      // after the last desired frame is received.
      return iodev_.cb_threshold + 16;
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
      struct cras_audio_format *fmt;
      struct audio_thread thread;

      memset(&thread, 0, sizeof(thread));

      fmt = (struct cras_audio_format *)malloc(sizeof(*fmt));
      memcpy(fmt, &fmt_, sizeof(fmt_));
      iodev_.format = fmt;
      iodev_.thread = &thread;
      iodev_.direction = direction;
      new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
      new_stream->fd = 55;
      new_stream->buffer_frames = 65;
      new_stream->cb_threshold = 80;
      new_stream->direction = direction;
      memcpy(&new_stream->format, fmt, sizeof(*fmt));

      if (direction == CRAS_STREAM_INPUT) {
        thread.output_dev = NULL;
        thread.input_dev = &iodev_;
      } else {
        thread.output_dev = &iodev_;
        thread.input_dev = NULL;
      }
      iodev_.thread = &thread;

      thread_add_stream(&thread, new_stream);
      EXPECT_EQ(2, is_open_called_);
      EXPECT_EQ(1, open_dev_called_);
      EXPECT_EQ(1, cras_iodev_config_params_for_streams_called);

      is_open_ = 1;

      second_stream = (struct cras_rstream *)calloc(1, sizeof(*second_stream));
      second_stream->fd = 56;
      second_stream->buffer_frames = 25;
      second_stream->cb_threshold = 12;
      second_stream->direction = direction;
      memcpy(&second_stream->format, fmt, sizeof(*fmt));
      thread_add_stream(&thread, second_stream);
      EXPECT_EQ(4, is_open_called_);
      EXPECT_EQ(1, open_dev_called_);
      EXPECT_EQ(2, cras_iodev_config_params_for_streams_called);
      EXPECT_EQ(25, cras_iodev_config_params_for_streams_buffer_size);
      EXPECT_EQ(12, cras_iodev_config_params_for_streams_threshold);

      //  Remove the streams.
      rc = thread_remove_stream(&thread, second_stream);
      EXPECT_EQ(1, rc);
      EXPECT_EQ(3, cras_iodev_config_params_for_streams_called);
      EXPECT_EQ(0, close_dev_called_);

      rc = thread_remove_stream(&thread, new_stream);
      EXPECT_EQ(0, rc);
      EXPECT_EQ(1, close_dev_called_);
      EXPECT_EQ(3, cras_iodev_config_params_for_streams_called);

      free(fmt);
      free(new_stream);
      free(second_stream);
    }

  struct cras_iodev iodev_;
  static int is_open_;
  static int is_open_called_;
  static int open_dev_called_;
  static int open_dev_return_val_;
  static int close_dev_called_;
  struct cras_audio_format fmt_;
};

int AddStreamSuite::is_open_ = 0;
int AddStreamSuite::is_open_called_ = 0;
int AddStreamSuite::open_dev_called_ = 0;
int AddStreamSuite::open_dev_return_val_ = 0;
int AddStreamSuite::close_dev_called_ = 0;

TEST_F(AddStreamSuite, SimpleAddOutputStream) {
  int rc;
  cras_rstream* new_stream;
  struct audio_thread thread;

  memset(&thread, 0, sizeof(thread));

  iodev_.format = &fmt_;
  iodev_.thread = &thread;
  new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
  new_stream->fd = 55;
  new_stream->buffer_frames = 65;
  new_stream->cb_threshold = 80;
  memcpy(&new_stream->format, &fmt_, sizeof(fmt_));

  thread.output_dev = &iodev_;
  iodev_.thread = &thread;

  rc = thread_add_stream(&thread, new_stream);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(2, is_open_called_);
  EXPECT_EQ(1, open_dev_called_);
  EXPECT_EQ(1, cras_iodev_config_params_for_streams_called);

  is_open_ = 1;

  //  remove the stream.
  rc = thread_remove_stream(&thread, new_stream);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, close_dev_called_);

  free(new_stream);
}

TEST_F(AddStreamSuite, AddStreamOpenFail) {
  struct audio_thread *thread;
  cras_rstream new_stream;

  thread = audio_thread_create();
  ASSERT_TRUE(thread);
  audio_thread_set_output_dev(thread, &iodev_);

  iodev_.thread = thread;

  open_dev_return_val_ = -1;
  new_stream.direction = CRAS_STREAM_OUTPUT;
  EXPECT_EQ(AUDIO_THREAD_OUTPUT_DEV_ERROR,
            thread_add_stream(thread, &new_stream));
  EXPECT_EQ(1, open_dev_called_);
  EXPECT_EQ(1, cras_iodev_set_format_called);
  EXPECT_EQ(0, thread->streams);
  audio_thread_destroy(thread);
}

TEST_F(AddStreamSuite, AddRmTwoOutputStreams) {
  add_rm_two_streams(CRAS_STREAM_OUTPUT);
}

TEST_F(AddStreamSuite, AddRmTwoInputStreams) {
  add_rm_two_streams(CRAS_STREAM_INPUT);
}

extern "C" {
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

void cras_iodev_config_params(struct cras_iodev *iodev,
                              unsigned int buffer_size,
                              unsigned int cb_threshold) {
  cras_iodev_config_params_for_streams_called++;
  cras_iodev_config_params_for_streams_buffer_size = buffer_size;
  cras_iodev_config_params_for_streams_threshold = cb_threshold;
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

size_t cras_system_get_volume() {
  return cras_system_get_volume_return;
}

size_t cras_system_get_mute() {
  return 0;
}

size_t cras_system_get_capture_mute() {
  return 0;
}

void loopback_iodev_set_format(struct loopback_iodev *loopback_dev,
                               const struct cras_audio_format *fmt) {
}

int loopback_iodev_add_audio(struct loopback_iodev *loopback_dev,
                             const uint8_t *audio,
                             unsigned int count,
                             struct cras_rstream *stream) {
  return 0;
}

int loopback_iodev_add_zeros(struct cras_iodev *dev,
                             unsigned int count) {
  return 0;
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

}  // extern "C"

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
