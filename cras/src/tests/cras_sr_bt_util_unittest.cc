/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>

extern "C" {
#include "cras_server_metrics.h"
#include "cras_sr_bt_util.h"
}

extern "C" {
static bool cras_system_get_force_sr_bt_enabled_return_value = false;
static bool get_hfp_mic_sr_feature_enabled_return_value = false;
static bool cras_dlc_sr_bt_is_available_return_value = false;
static enum CRAS_METRICS_HFP_MIC_SR_STATUS
    cras_server_metrics_hfp_mic_sr_called_status =
        CRAS_METRICS_HFP_MIC_SR_ENABLE_SUCCESS;

bool cras_system_get_force_sr_bt_enabled() {
  return cras_system_get_force_sr_bt_enabled_return_value;
}

bool get_hfp_mic_sr_feature_enabled() {
  return get_hfp_mic_sr_feature_enabled_return_value;
}

bool cras_dlc_sr_bt_is_available() {
  return cras_dlc_sr_bt_is_available_return_value;
}

int cras_server_metrics_hfp_mic_sr_status(
    struct cras_iodev* iodev,
    enum CRAS_METRICS_HFP_MIC_SR_STATUS status) {
  cras_server_metrics_hfp_mic_sr_called_status = status;
  return 0;
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

struct SendUMALogTestParam {
  enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS status;
  bool is_enabled;
  enum CRAS_METRICS_HFP_MIC_SR_STATUS expected_status;
};

class SendUMALogTest : public testing::TestWithParam<SendUMALogTestParam> {
 protected:
  virtual void SetUp() { ResetFakeState(); }
};

TEST_P(SendUMALogTest, TestExpectedStatus) {
  cras_sr_bt_send_uma_log(nullptr, GetParam().status, GetParam().is_enabled);

  EXPECT_EQ(cras_server_metrics_hfp_mic_sr_called_status,
            GetParam().expected_status);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    SendUMALogTest,
    testing::Values(
        SendUMALogTestParam(
            {.status = CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK,
             .is_enabled = false,
             .expected_status = CRAS_METRICS_HFP_MIC_SR_ENABLE_FAILED}),
        SendUMALogTestParam(
            {.status = CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK,
             .is_enabled = true,
             .expected_status = CRAS_METRICS_HFP_MIC_SR_ENABLE_SUCCESS}),
        SendUMALogTestParam(
            {.status = CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_UNSUPPORTED,
             .expected_status = CRAS_METRICS_HFP_MIC_SR_FEATURE_UNSUPPORTED}),
        SendUMALogTestParam(
            {.status = CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_DISABLED,
             .expected_status = CRAS_METRICS_HFP_MIC_SR_FEATURE_DISABLED}),
        SendUMALogTestParam(
            {.status = CRAS_SR_BT_CAN_BE_ENABLED_STATUS_DLC_UNAVAILABLE,
             .expected_status = CRAS_METRICS_HFP_MIC_SR_DLC_UNAVAILABLE})));

}  // namespace
