// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>
#include <unistd.h>

extern "C" {
#include "audio_thread.h"
#include "cras_messages.h"
#include "cras_rclient.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
}

//  Stub data.
static int cras_rstream_create_return;
static struct cras_rstream *cras_rstream_create_stream_out;
static int cras_iodev_attach_stream_retval;
static size_t cras_system_set_volume_value;
static int cras_system_set_volume_called;
static size_t cras_system_set_capture_gain_value;
static int cras_system_set_capture_gain_called;
static size_t cras_system_set_mute_value;
static int cras_system_set_mute_called;
static size_t cras_system_set_user_mute_value;
static int cras_system_set_user_mute_called;
static size_t cras_system_set_mute_locked_value;
static int cras_system_set_mute_locked_called;
static size_t cras_system_set_capture_mute_value;
static int cras_system_set_capture_mute_called;
static size_t cras_system_set_capture_mute_locked_value;
static int cras_system_set_capture_mute_locked_called;
static size_t cras_make_fd_nonblocking_called;
static audio_thread* iodev_get_thread_return;
static int stream_list_add_stream_return;
static unsigned int stream_list_add_stream_called;
static unsigned int stream_list_disconnect_stream_called;
static unsigned int cras_iodev_list_rm_input_called;
static unsigned int cras_iodev_list_rm_output_called;
static struct cras_rstream dummy_rstream;

void ResetStubData() {
  cras_rstream_create_return = 0;
  cras_rstream_create_stream_out = (struct cras_rstream *)NULL;
  cras_iodev_attach_stream_retval = 0;
  cras_system_set_volume_value = 0;
  cras_system_set_volume_called = 0;
  cras_system_set_capture_gain_value = 0;
  cras_system_set_capture_gain_called = 0;
  cras_system_set_mute_value = 0;
  cras_system_set_mute_called = 0;
  cras_system_set_user_mute_value = 0;
  cras_system_set_user_mute_called = 0;
  cras_system_set_mute_locked_value = 0;
  cras_system_set_mute_locked_called = 0;
  cras_system_set_capture_mute_value = 0;
  cras_system_set_capture_mute_called = 0;
  cras_system_set_capture_mute_locked_value = 0;
  cras_system_set_capture_mute_locked_called = 0;
  cras_make_fd_nonblocking_called = 0;
  iodev_get_thread_return = reinterpret_cast<audio_thread*>(0xad);
  stream_list_add_stream_return = 0;
  stream_list_add_stream_called = 0;
  stream_list_disconnect_stream_called = 0;
  cras_iodev_list_rm_output_called = 0;
  cras_iodev_list_rm_input_called = 0;
}

namespace {

TEST(RClientSuite, CreateSendMessage) {
  struct cras_rclient *rclient;
  int rc;
  struct cras_client_connected msg;
  int pipe_fds[2];

  ResetStubData();

  rc = pipe(pipe_fds);
  ASSERT_EQ(0, rc);

  rclient = cras_rclient_create(pipe_fds[1], 800);
  ASSERT_NE((void *)NULL, rclient);

  rc = read(pipe_fds[0], &msg, sizeof(msg));
  EXPECT_EQ(sizeof(msg), rc);
  EXPECT_EQ(CRAS_CLIENT_CONNECTED, msg.header.id);
  EXPECT_EQ(CRAS_CLIENT_CONNECTED, msg.header.id);

  cras_rclient_destroy(rclient);
  close(pipe_fds[0]);
  close(pipe_fds[1]);
}

class RClientMessagesSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      int rc;
      struct cras_client_connected msg;

      rc = pipe(pipe_fds_);
      if (rc < 0)
        return;
      rclient_ = cras_rclient_create(pipe_fds_[1], 800);
      rc = read(pipe_fds_[0], &msg, sizeof(msg));
      if (rc < 0)
        return;

      rstream_ = (struct cras_rstream *)calloc(1, sizeof(*rstream_));

      stream_id_ = 0x10002;
      connect_msg_.header.id = CRAS_SERVER_CONNECT_STREAM;
      connect_msg_.header.length = sizeof(connect_msg_);
      connect_msg_.stream_type = CRAS_STREAM_TYPE_DEFAULT;
      connect_msg_.direction = CRAS_STREAM_OUTPUT;
      connect_msg_.stream_id = stream_id_;
      connect_msg_.buffer_frames = 480;
      connect_msg_.cb_threshold = 240;
      connect_msg_.flags = 0;
      connect_msg_.format.num_channels = 2;
      connect_msg_.format.frame_rate = 48000;
      connect_msg_.format.format = SND_PCM_FORMAT_S16_LE;
      connect_msg_.dev_idx = NO_DEVICE;

      ResetStubData();
    }

    virtual void TearDown() {
      cras_rclient_destroy(rclient_);
      free(rstream_);
      close(pipe_fds_[0]);
      close(pipe_fds_[1]);
    }

    struct cras_connect_message connect_msg_;
    struct cras_rclient *rclient_;
    struct cras_rstream *rstream_;
    size_t stream_id_;
    int pipe_fds_[2];
};

TEST_F(RClientMessagesSuite, AudThreadAttachFail) {
  struct cras_client_stream_connected out_msg;
  int rc;

  cras_rstream_create_stream_out = rstream_;
  stream_list_add_stream_return = -EINVAL;

  rc = cras_rclient_message_from_client(rclient_, &connect_msg_.header, 100);
  EXPECT_EQ(0, rc);

  rc = read(pipe_fds_[0], &out_msg, sizeof(out_msg));
  EXPECT_EQ(sizeof(out_msg), rc);
  EXPECT_EQ(stream_id_, out_msg.stream_id);
  EXPECT_NE(0, out_msg.err);
  EXPECT_EQ(0, cras_iodev_list_rm_output_called);
  EXPECT_EQ(1, stream_list_add_stream_called);
  EXPECT_EQ(0, stream_list_disconnect_stream_called);
}

TEST_F(RClientMessagesSuite, ConnectMsgWithBadFd) {
  struct cras_client_stream_connected out_msg;
  int rc;

  rc = cras_rclient_message_from_client(rclient_, &connect_msg_.header, -1);
  EXPECT_EQ(0, rc);

  rc = read(pipe_fds_[0], &out_msg, sizeof(out_msg));
  EXPECT_EQ(sizeof(out_msg), rc);
  EXPECT_EQ(stream_id_, out_msg.stream_id);
  EXPECT_NE(0, out_msg.err);
  EXPECT_EQ(stream_list_add_stream_called,
            stream_list_disconnect_stream_called);
}

TEST_F(RClientMessagesSuite, SuccessReply) {
  struct cras_client_stream_connected out_msg;
  int rc;

  cras_rstream_create_stream_out = rstream_;
  cras_iodev_attach_stream_retval = 0;

  rc = cras_rclient_message_from_client(rclient_, &connect_msg_.header, 100);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_make_fd_nonblocking_called);

  rc = read(pipe_fds_[0], &out_msg, sizeof(out_msg));
  EXPECT_EQ(sizeof(out_msg), rc);
  EXPECT_EQ(stream_id_, out_msg.stream_id);
  EXPECT_EQ(0, out_msg.err);
  EXPECT_EQ(1, stream_list_add_stream_called);
  EXPECT_EQ(0, stream_list_disconnect_stream_called);
}

TEST_F(RClientMessagesSuite, SuccessCreateThreadReply) {
  struct cras_client_stream_connected out_msg;
  int rc;

  cras_rstream_create_stream_out = rstream_;
  cras_iodev_attach_stream_retval = 0;

  rc = cras_rclient_message_from_client(rclient_, &connect_msg_.header, 100);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_make_fd_nonblocking_called);

  rc = read(pipe_fds_[0], &out_msg, sizeof(out_msg));
  EXPECT_EQ(sizeof(out_msg), rc);
  EXPECT_EQ(stream_id_, out_msg.stream_id);
  EXPECT_EQ(0, out_msg.err);
  EXPECT_EQ(1, stream_list_add_stream_called);
  EXPECT_EQ(0, stream_list_disconnect_stream_called);
}

TEST_F(RClientMessagesSuite, SetVolume) {
  struct cras_set_system_volume msg;
  int rc;

  msg.header.id = CRAS_SERVER_SET_SYSTEM_VOLUME;
  msg.header.length = sizeof(msg);
  msg.volume = 66;

  rc = cras_rclient_message_from_client(rclient_, &msg.header, -1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_volume_called);
  EXPECT_EQ(66, cras_system_set_volume_value);
}

TEST_F(RClientMessagesSuite, SetCaptureVolume) {
  struct cras_set_system_volume msg;
  int rc;

  msg.header.id = CRAS_SERVER_SET_SYSTEM_CAPTURE_GAIN;
  msg.header.length = sizeof(msg);
  msg.volume = 66;

  rc = cras_rclient_message_from_client(rclient_, &msg.header, -1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_capture_gain_called);
  EXPECT_EQ(66, cras_system_set_capture_gain_value);
}

TEST_F(RClientMessagesSuite, SetMute) {
  struct cras_set_system_mute msg;
  int rc;

  msg.header.id = CRAS_SERVER_SET_SYSTEM_MUTE;
  msg.header.length = sizeof(msg);
  msg.mute = 1;

  rc = cras_rclient_message_from_client(rclient_, &msg.header, -1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_mute_called);
  EXPECT_EQ(1, cras_system_set_mute_value);

  msg.header.id = CRAS_SERVER_SET_SYSTEM_MUTE_LOCKED;
  rc = cras_rclient_message_from_client(rclient_, &msg.header, -1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_mute_locked_called);
  EXPECT_EQ(1, cras_system_set_mute_locked_value);
}

TEST_F(RClientMessagesSuite, SetUserMute) {
  struct cras_set_system_mute msg;
  int rc;

  msg.header.id = CRAS_SERVER_SET_USER_MUTE;
  msg.header.length = sizeof(msg);
  msg.mute = 1;

  rc = cras_rclient_message_from_client(rclient_, &msg.header, -1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_user_mute_called);
  EXPECT_EQ(1, cras_system_set_user_mute_value);
}

TEST_F(RClientMessagesSuite, SetCaptureMute) {
  struct cras_set_system_mute msg;
  int rc;

  msg.header.id = CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE;
  msg.header.length = sizeof(msg);
  msg.mute = 1;

  rc = cras_rclient_message_from_client(rclient_, &msg.header, -1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_capture_mute_called);
  EXPECT_EQ(1, cras_system_set_capture_mute_value);

  msg.header.id = CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE_LOCKED;
  rc = cras_rclient_message_from_client(rclient_, &msg.header, -1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_capture_mute_locked_called);
  EXPECT_EQ(1, cras_system_set_capture_mute_locked_value);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/* stubs */
extern "C" {

struct audio_thread* cras_iodev_list_get_audio_thread() {
  return iodev_get_thread_return;
}

void cras_iodev_list_add_active_node(enum CRAS_STREAM_DIRECTION dir,
                                     cras_node_id_t node_id)
{
}

void cras_iodev_list_rm_active_node(enum CRAS_STREAM_DIRECTION dir,
                                    cras_node_id_t node_id)
{
}

int audio_thread_rm_stream(audio_thread* thread,
			   cras_rstream* stream) {
  return 0;
}

void audio_thread_add_output_dev(struct audio_thread *thread,
				 struct cras_iodev *odev)
{
}

int audio_thread_dump_thread_info(struct audio_thread *thread,
				  struct audio_debug_info *info)
{
  return 0;
}

int audio_thread_suspend(struct audio_thread *thread)
{
  return 0;
}

int audio_thread_resume(struct audio_thread *thread)
{
  return 0;
}

const char *cras_config_get_socket_file_dir()
{
  return "/tmp";
}

int cras_rstream_create(struct cras_rstream_config *stream_config,
			struct cras_rstream **stream_out)
{
  *stream_out = cras_rstream_create_stream_out;
  return cras_rstream_create_return;
}

int cras_iodev_move_stream_type(uint32_t type, uint32_t index)
{
  return 0;
}

int cras_iodev_list_rm_output(struct cras_iodev *output) {
  cras_iodev_list_rm_output_called++;
  return 0;
}

int cras_iodev_list_rm_input(struct cras_iodev *input) {
  cras_iodev_list_rm_input_called++;
  return 0;
}

int cras_server_disconnect_from_client_socket(int socket_fd) {
  return 0;
}

int cras_make_fd_nonblocking(int fd)
{
  cras_make_fd_nonblocking_called++;
  return 0;
}

void cras_system_set_volume(size_t volume)
{
  cras_system_set_volume_value = volume;
  cras_system_set_volume_called++;
}

void cras_system_set_capture_gain(long gain)
{
  cras_system_set_capture_gain_value = gain;
  cras_system_set_capture_gain_called++;
}

//  From system_state.
void cras_system_set_mute(int mute)
{
  cras_system_set_mute_value = mute;
  cras_system_set_mute_called++;
}
void cras_system_set_user_mute(int mute)
{
  cras_system_set_user_mute_value = mute;
  cras_system_set_user_mute_called++;
}
void cras_system_set_mute_locked(int mute)
{
  cras_system_set_mute_locked_value = mute;
  cras_system_set_mute_locked_called++;
}
void cras_system_set_capture_mute(int mute)
{
  cras_system_set_capture_mute_value = mute;
  cras_system_set_capture_mute_called++;
}
void cras_system_set_capture_mute_locked(int mute)
{
  cras_system_set_capture_mute_locked_value = mute;
  cras_system_set_capture_mute_locked_called++;
}

int cras_system_remove_alsa_card(size_t alsa_card_index)
{
	return -1;
}

void cras_system_set_suspended(int suspended) {
}

struct cras_server_state *cras_system_state_get_no_lock()
{
  return NULL;
}

key_t cras_sys_state_shm_key()
{
  return 1;
}

void cras_dsp_reload_ini()
{
}

void cras_dsp_dump_info()
{
}

int cras_iodev_list_set_node_attr(int dev_index, int node_index,
                                  enum ionode_attr attr, int value)
{
  return 0;
}

void cras_iodev_list_select_node(enum CRAS_STREAM_DIRECTION direction,
				 cras_node_id_t node_id)
{
}

void cras_iodev_list_add_test_dev(enum TEST_IODEV_TYPE type) {
}

struct stream_list *cras_iodev_list_get_stream_list()
{
  return NULL;
}

/* Handles sending a command to a test iodev. */
void cras_iodev_list_test_dev_command(unsigned int iodev_idx,
                                      enum CRAS_TEST_IODEV_CMD command,
                                      unsigned int data_len,
                                      const uint8_t *data) {
}

int stream_list_add(struct stream_list *list,
                    struct cras_rstream_config *config,
		    struct cras_rstream **stream)
{
  int ret;

  *stream = &dummy_rstream;

  stream_list_add_stream_called++;
  ret = stream_list_add_stream_return;
  if (ret)
    stream_list_add_stream_return = -EINVAL;

  dummy_rstream.direction = config->direction;
  dummy_rstream.stream_id = config->stream_id;

  return ret;
}

struct cras_rstream *stream_list_rm(struct stream_list *list,
                                    cras_stream_id_t id)
{
  stream_list_disconnect_stream_called++;
  return NULL;
}

struct cras_rstream *stream_list_rm_all_client_streams(
                struct stream_list *list, struct cras_rclient *rclient)
{
  return NULL;
}

int cras_send_with_fds(int sockfd, const void *buf, size_t len, int *fd,
                       unsigned int num_fds)
{
  return write(sockfd, buf, len);
}

}  // extern "C"
