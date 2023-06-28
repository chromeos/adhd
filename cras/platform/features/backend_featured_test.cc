// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <featured/fake_platform_features.h>
#include <unistd.h>

#include "cras/platform/features/backend_featured.hh"
#include "cras/platform/features/features.h"
#include "gtest/gtest.h"

// Library adapter of the fake feature_library.
class FakeFeatureLibraryAdapterImpl : public FeatureLibraryAdapter {
 public:
  bool Initialize(scoped_refptr<dbus::Bus> bus) override {
    fake_lib_ = std::make_unique<feature::FakePlatformFeatures>(bus);
    // Also set lib_ so FeatureLibraryAdapter::Get() can acquire it.
    lib_ = fake_lib_.get();
    return true;
  }

  void Shutdown() override {
    fake_lib_.reset(nullptr);
  }

  std::unique_ptr<feature::FakePlatformFeatures> fake_lib_;
};

TEST(FeaturesBackendFeatured, InitShutdown) {
  // The real instance can be initialized once.
  // At least check that it does not leak memory.
  ASSERT_EQ(cras_features_init(), -ECONNRESET);
  // The second init should fail.
  ASSERT_EQ(cras_features_init(), -EEXIST);
  cras_features_deinit();

  // Try to construct and destruct the fake version multiple times.
  ASSERT_EQ(
      backend_featured_init(std::make_unique<FakeFeatureLibraryAdapterImpl>()),
      0);
  cras_features_deinit();
  ASSERT_EQ(
      backend_featured_init(std::make_unique<FakeFeatureLibraryAdapterImpl>()),
      0);
  cras_features_deinit();
  ASSERT_EQ(
      backend_featured_init(std::make_unique<FakeFeatureLibraryAdapterImpl>()),
      0);
  cras_features_deinit();
  // Should be safe to double destruct.
  cras_features_deinit();
}

TEST(FeaturesBackendFeatured, IsEnabled) {
  auto adapter = std::make_unique<FakeFeatureLibraryAdapterImpl>();
  FakeFeatureLibraryAdapterImpl* adapter_unowned = adapter.get();
  ASSERT_EQ(backend_featured_init(std::move(adapter)), 0);
  // Once backend_featured_init returns fake_lib_ should be initialized.
  auto fake_lib = adapter_unowned->fake_lib_.get();

  // Initial state.
  EXPECT_TRUE(cras_feature_enabled(CrOSLateBootEnabledByDefault));
  EXPECT_FALSE(cras_feature_enabled(CrOSLateBootDisabledByDefault));

  // Invert enabled status.
  fake_lib->SetEnabled("CrOSLateBootEnabledByDefault", false);
  fake_lib->SetEnabled("CrOSLateBootDisabledByDefault", true);
  fake_lib->TriggerRefetchSignal();
  while (cras_feature_enabled(CrOSLateBootEnabledByDefault) ||
         !cras_feature_enabled(CrOSLateBootDisabledByDefault)) {
    usleep(1000);
  }
  EXPECT_FALSE(cras_feature_enabled(CrOSLateBootEnabledByDefault));
  EXPECT_TRUE(cras_feature_enabled(CrOSLateBootDisabledByDefault));

  // Clear enabled status.
  fake_lib->ClearEnabled("CrOSLateBootEnabledByDefault");
  fake_lib->ClearEnabled("CrOSLateBootDisabledByDefault");
  fake_lib->TriggerRefetchSignal();
  while (!cras_feature_enabled(CrOSLateBootEnabledByDefault) ||
         cras_feature_enabled(CrOSLateBootDisabledByDefault)) {
    usleep(1000);
  }
  EXPECT_TRUE(cras_feature_enabled(CrOSLateBootEnabledByDefault));
  EXPECT_FALSE(cras_feature_enabled(CrOSLateBootDisabledByDefault));

  cras_features_deinit();
}
