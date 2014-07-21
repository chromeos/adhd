/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>
#include <stdint.h>
#include <sys/socket.h>

#include <stdio.h>

extern "C" {
  #include "cras_hfp_slc.h"
}

static struct hfp_slc_handle *handle;
static int slc_initialized_cb_called;
static int slc_disconnected_cb_called;
static int cras_system_add_select_fd_called;
static void(*slc_cb)(void *data);
static void *slc_cb_data;
static int fake_errno;

int slc_initialized_cb(struct hfp_slc_handle *handle, void *data);
int slc_disconnected_cb(struct hfp_slc_handle *handle);

void ResetStubData() {
  slc_initialized_cb_called = 0;
  cras_system_add_select_fd_called = 0;
  slc_cb = NULL;
  slc_cb_data = NULL;
}

namespace {

TEST(HfpSlc, CreateSlcHandle) {
  ResetStubData();

  handle = hfp_slc_create(0, 0, slc_initialized_cb, handle,
                          slc_disconnected_cb);
  ASSERT_EQ(1, cras_system_add_select_fd_called);
  ASSERT_EQ(handle, slc_cb_data);

  hfp_slc_destroy(handle);
}

TEST(HfpSlc, InitializeSlc) {
  int err;
  int sock[2];
  char buf[256];
  char *chp;
  ResetStubData();

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  handle = hfp_slc_create(sock[0], 0, slc_initialized_cb, handle,
                          slc_disconnected_cb);

  err = write(sock[1], "AT+CIND=?\r", 10);
  ASSERT_EQ(10, err);
  slc_cb(slc_cb_data);
  err = read(sock[1], buf, 256);

  /* Assert "\r\n+CIND: ... \r\n" response is received */
  chp = strstr(buf, "\r\n");
  ASSERT_NE((void *)NULL, (void *)chp);
  ASSERT_EQ(0, strncmp("\r\n+CIND:", chp, 8));
  chp+=2;
  chp = strstr(chp, "\r\n");
  ASSERT_NE((void *)NULL, (void *)chp);

  /* Assert "\r\nOK\r\n" response is received */
  chp+=2;
  chp = strstr(chp, "\r\n");
  ASSERT_NE((void *)NULL, (void *)chp);
  ASSERT_EQ(0, strncmp("\r\nOK", chp, 4));

  err = write(sock[1], "AT+CMER=3,0,0,1\r", 16);
  ASSERT_EQ(16, err);
  slc_cb(slc_cb_data);

  ASSERT_EQ(1, slc_initialized_cb_called);

  /* Assert "\r\nOK\r\n" response is received */
  err = read(sock[1], buf, 256);

  chp = strstr(buf, "\r\n");
  ASSERT_NE((void *)NULL, (void *)chp);
  ASSERT_EQ(0, strncmp("\r\nOK", chp, 4));

  hfp_slc_destroy(handle);
}

TEST(HfpSlc, DisconnectSlc) {
  int sock[2];
  ResetStubData();

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  handle = hfp_slc_create(sock[0], 0, slc_initialized_cb, handle,
                          slc_disconnected_cb);
  /* Close socket right away to make read() get negative err code, and
   * fake the errno to ECONNRESET. */
  close(sock[0]);
  close(sock[1]);
  fake_errno = 104;
  slc_cb(slc_cb_data);

  ASSERT_EQ(1, slc_disconnected_cb_called);

  hfp_slc_destroy(handle);
}
} // namespace

int slc_initialized_cb(struct hfp_slc_handle *handle, void *data) {
  slc_initialized_cb_called++;
  return 0;
}

int slc_disconnected_cb(struct hfp_slc_handle *handle) {
  slc_disconnected_cb_called++;
  return 0;
}

extern "C" {
int cras_system_add_select_fd(int fd,
			      void (*callback)(void *data),
			      void *callback_data) {
  cras_system_add_select_fd_called++;
  slc_cb = callback;
  slc_cb_data = callback_data;
  return 0;
}

void cras_system_rm_select_fd(int fd) {
}

/* To return fake errno */
int *__errno_location() {
  return &fake_errno;
}
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
