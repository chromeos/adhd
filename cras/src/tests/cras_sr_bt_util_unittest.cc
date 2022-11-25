/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>

extern "C" {
#include "cras_sr_bt_util.h"
}

extern "C" {
static bool cras_system_get_force_sr_bt_enabled_return_value = false;
static bool get_hfp_mic_sr_feature_enabled_return_value = false;
static bool cras_dlc_sr_bt_is_available_return_value = false;

bool cras_system_get_force_sr_bt_enabled() {
  return cras_system_get_force_sr_bt_enabled_return_value;
}

bool get_hfp_mic_sr_feature_enabled() {
  return get_hfp_mic_sr_feature_enabled_return_value;
}

bool cras_dlc_sr_bt_is_available() {
  return cras_dlc_sr_bt_is_available_return_value;
}

void ResetFakeState() {
  cras_system_get_force_sr_bt_enabled_return_value = false;
  get_hfp_mic_sr_feature_enabled_return_value = false;
  cras_dlc_sr_bt_is_available_return_value = false;
}
}
namespace {

struct SrBtUtilTestParam {
  bool cras_system_get_force_sr_bt_enabled_return_value = false;
  bool get_hfp_mic_sr_feature_enabled_return_value = false;
  bool cras_dlc_sr_bt_is_available_return_value = false;
  enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS expected_status;
};

class SrBtUtilTest : public testing::TestWithParam<SrBtUtilTestParam> {
 protected:
  virtual void SetUp() { ResetFakeState(); }
};

TEST_P(SrBtUtilTest, TestExpectedtatus) {
  cras_system_get_force_sr_bt_enabled_return_value =
      GetParam().cras_system_get_force_sr_bt_enabled_return_value;
  get_hfp_mic_sr_feature_enabled_return_value =
      GetParam().get_hfp_mic_sr_feature_enabled_return_value;
  cras_dlc_sr_bt_is_available_return_value =
      GetParam().cras_dlc_sr_bt_is_available_return_value;
  EXPECT_EQ(cras_sr_bt_can_be_enabled(), GetParam().expected_status);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    SrBtUtilTest,
    testing::Values(
        SrBtUtilTestParam(
            {.expected_status =
                 CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_DISABLED}),
        SrBtUtilTestParam(
            {.get_hfp_mic_sr_feature_enabled_return_value = true,
             .expected_status =
                 CRAS_SR_BT_CAN_BE_ENABLED_STATUS_DLC_UNAVAILABLE}),
        SrBtUtilTestParam(
            {.get_hfp_mic_sr_feature_enabled_return_value = true,
             .cras_dlc_sr_bt_is_available_return_value = true,
             .expected_status = CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK}),
        SrBtUtilTestParam(
            {.cras_system_get_force_sr_bt_enabled_return_value = true,
             .expected_status =
                 CRAS_SR_BT_CAN_BE_ENABLED_STATUS_DLC_UNAVAILABLE}),
        SrBtUtilTestParam(
            {.cras_system_get_force_sr_bt_enabled_return_value = true,
             .cras_dlc_sr_bt_is_available_return_value = true,
             .expected_status = CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK})));

}  // namespace
