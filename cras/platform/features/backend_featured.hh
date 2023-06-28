// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_PLATFORM_FEATURES_BACKEND_FEATURED_HH_
#define CRAS_PLATFORM_FEATURES_BACKEND_FEATURED_HH_

#include <dbus/bus.h>
#include <featured/feature_library.h>
#include <functional>
#include <memory>

#include "cras/platform/features/features.h"

// Wrapper to make PlatformFeatures and FakePlatformFeatures behave the same.
class FeatureLibraryAdapter {
 public:
  FeatureLibraryAdapter() = default;
  virtual ~FeatureLibraryAdapter() = default;

  // Disallow copy and move.
  FeatureLibraryAdapter(const FeatureLibraryAdapter&) = delete;
  FeatureLibraryAdapter& operator=(const FeatureLibraryAdapter&) = delete;
  FeatureLibraryAdapter(FeatureLibraryAdapter&&) = delete;
  FeatureLibraryAdapter& operator=(FeatureLibraryAdapter&&) = delete;

  // Initialize the library instance.
  // Should set the lib_ attribute.
  virtual bool Initialize(scoped_refptr<dbus::Bus> bus) = 0;

  // Shutdown and destruct the library instance.
  // Must be called on the same thread as Initialize().
  virtual void Shutdown() = 0;

  // Get the library instance.
  feature::PlatformFeaturesInterface* Get() { return lib_; }

 protected:
  feature::PlatformFeaturesInterface* lib_;
};

int backend_featured_init(std::unique_ptr<FeatureLibraryAdapter> adapter);

#endif  // CRAS_PLATFORM_FEATURES_BACKEND_FEATURED_HH_
