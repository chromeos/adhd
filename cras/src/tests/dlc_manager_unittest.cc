/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>
#include <syslog.h>
#include <vector>

extern "C" {
#include "cras/src/server/cras_dlc_manager_test_only.h"
#include "cras/src/server/rust/include/cras_dlc.h"
}

extern "C" {

// Fake implementation of cras_tm.

struct cras_timer {
  void (*cb)(struct cras_timer* t, void* data);
  void* cb_data;
};

struct cras_tm {
  std::vector<struct cras_timer*> timers;
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

// Fake implementation of cras_system_state.

struct cras_tm* cras_system_state_get_tm() {
  return &fake_tm;
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

const char* cras_dlc_get_root_path(enum CrasDlcId id) {
  static const char* cras_dlc_get_root_path_ret = "";
  return cras_dlc_get_root_path_ret;
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
    struct cras_timer* timer = tm->timers[i];
    timer->cb(timer, timer->cb_data);
  }

  // no new timer should be added.
  EXPECT_EQ(tm->timers.size(), NumCrasDlc);
  EXPECT_TRUE(cras_dlc_manager_is_null());
}

TEST_F(DlcManagerTest, TestDlcIsUnAvailableAndReachesMaxRetry) {
  cras_dlc_manager_init();
  struct cras_tm* tm = cras_system_state_get_tm();

  // The first try pluses the `MAX_RETRY_COUNT`, which is 10.
  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(tm->timers.size(), NumCrasDlc);
    for (int num_cur_timers = NumCrasDlc; num_cur_timers > 0;
         --num_cur_timers) {
      struct cras_timer* timer = tm->timers[0];
      timer->cb(timer, timer->cb_data);
      // When dlc is unavailable, new timer will be added.
      ASSERT_EQ(tm->timers.size(), NumCrasDlc + 1);
      cras_tm_cancel_timer(tm, timer);
    }
  }
  for (int num_cur_timers = NumCrasDlc; num_cur_timers > 0; --num_cur_timers) {
    // When `MAX_RETRY_COUNT` is reached, no more timer is added.
    ASSERT_EQ(tm->timers.size(), num_cur_timers);
    struct cras_timer* timer = tm->timers[0];
    timer->cb(timer, timer->cb_data);
    cras_tm_cancel_timer(tm, timer);
  }

  // no timer remains.
  EXPECT_EQ(tm->timers.size(), 0);
  EXPECT_TRUE(cras_dlc_manager_is_null());
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  openlog(NULL, LOG_PERROR, LOG_USER);
  return RUN_ALL_TESTS();
}
