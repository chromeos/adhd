/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <deque>
#include <gtest/gtest.h>
#include <syslog.h>

#include "cras/src/server/cras_dlc_manager_test_only.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/rust/include/cras_dlc.h"

extern "C" {

// Fake implementation of cras_tm.

struct cras_timer {
  void (*cb)(struct cras_timer* t, void* data);
  void* cb_data;
};

struct cras_tm {
  std::deque<struct cras_timer*> timers;
};

static struct cras_tm fake_tm = {};

struct cras_tm* cras_tm_init() {
  return &fake_tm;
}

void cras_tm_deinit(struct cras_tm* tm) {
  for (struct cras_timer* timer : tm->timers) {
    free(timer);
  }
  tm->timers.clear();
}

struct cras_timer* cras_tm_create_timer(struct cras_tm* tm,
                                        unsigned int ms,
                                        void (*cb)(struct cras_timer* t,
                                                   void* data),
                                        void* cb_data) {
  struct cras_timer* timer =
      (struct cras_timer*)calloc(1, sizeof(struct cras_timer));
  timer->cb = cb;
  timer->cb_data = cb_data;
  tm->timers.push_back(timer);
  return timer;
}

void cras_tm_cancel_timer(struct cras_tm* tm, struct cras_timer* t) {
  auto it = std::find(tm->timers.begin(), tm->timers.end(), t);
  if (it == tm->timers.end()) {
    return;
  }
  tm->timers.erase(it);
  free(t);
}

void cras_tm_call_first_callback(struct cras_tm* tm) {
  struct cras_timer* timer = tm->timers.front();
  timer->cb(timer, timer->cb_data);
  cras_tm_cancel_timer(tm, timer);
}

// Fake implementation of cras_system_state.

struct cras_tm* cras_system_state_get_tm() {
  return &fake_tm;
}

// Fake implementation of cras_server_metrics.

static int cras_server_metrics_dlc_counter = 0;

int cras_server_metrics_dlc_install_retried_times_on_success(
    enum CrasDlcId dlc_id,
    int num_retry_times) {
  ++cras_server_metrics_dlc_counter;
  return 0;
}

// Fake implementation of cras_dlc.

static bool cras_dlc_install_ret[NumCrasDlc] = {};
static bool cras_dlc_is_available_ret[NumCrasDlc] = {};

bool cras_dlc_install(enum CrasDlcId id) {
  return cras_dlc_install_ret[id];
}

bool cras_dlc_is_available(enum CrasDlcId id) {
  return cras_dlc_is_available_ret[id];
}

char* cras_dlc_get_root_path(enum CrasDlcId id) {
  static char cras_dlc_get_root_path_ret[] = "";
  return cras_dlc_get_root_path_ret;
}

void cras_dlc_get_id_string(char* ret, uintptr_t ret_len, enum CrasDlcId id) {
  ret[0] = '\0';
}

void ResetCrasDlc() {
  for (int i = 0; i < NumCrasDlc; ++i) {
    cras_dlc_install_ret[i] = false;
    cras_dlc_is_available_ret[i] = false;
  }
}
}

namespace {

class DlcManagerTest : public testing::Test {
 protected:
  virtual void SetUp() {
    tm_ = cras_tm_init();
    ResetCrasDlc();
    cras_server_metrics_dlc_counter = 0;
  }
  virtual void TearDown() { cras_tm_deinit(tm_); }

 private:
  struct cras_tm* tm_ = nullptr;
};

TEST_F(DlcManagerTest, TestIfDlcIsAvailable) {
  cras_dlc_manager_init();
  struct cras_tm* tm = cras_system_state_get_tm();
  ASSERT_EQ(tm->timers.size(), NumCrasDlc);

  for (int i = 0; i < NumCrasDlc; ++i) {
    cras_dlc_is_available_ret[i] = true;
  }

  for (int i = 0; i < NumCrasDlc; ++i) {
    cras_tm_call_first_callback(tm);
  }

  // no new timer should be added.
  EXPECT_EQ(tm->timers.size(), 0);
  // each DLC installation, either success or not, would send 1 UMA
  EXPECT_EQ(cras_server_metrics_dlc_counter, NumCrasDlc);
  EXPECT_TRUE(cras_dlc_manager_is_null());
}

TEST_F(DlcManagerTest, TestDlcIsUnAvailableAndKeepsRetrying) {
  cras_dlc_manager_init();
  struct cras_tm* tm = cras_system_state_get_tm();
  ASSERT_EQ(tm->timers.size(), NumCrasDlc);

  // Test for a few rounds
  for (int i = 0; i < NumCrasDlc * 5; ++i) {
    cras_tm_call_first_callback(tm);
    // When dlc is unavailable, a new timer will be added.
    ASSERT_EQ(tm->timers.size(), NumCrasDlc);
  }

  EXPECT_EQ(tm->timers.size(), NumCrasDlc);
  // All retries failed, no metrics are sent
  EXPECT_EQ(cras_server_metrics_dlc_counter, 0);
  EXPECT_TRUE(!cras_dlc_manager_is_null());
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  openlog(NULL, LOG_PERROR, LOG_USER);
  return RUN_ALL_TESTS();
}
