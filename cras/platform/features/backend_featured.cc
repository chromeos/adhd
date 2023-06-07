/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/platform/features/backend_featured.h"

#include <array>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/thread_pool.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/threading/thread.h>
#include <chrono>
#include <dbus/bus.h>
#include <errno.h>
#include <featured/feature_library.h>
#include <functional>
#include <future>
#include <shared_mutex>
#include <stdbool.h>
#include <syslog.h>
#include <time.h>

#include "cras/platform/features/features_impl.h"

namespace {

using FeatureArray = std::array<struct VariationsFeature, NUM_FEATURES>;

FeatureArray makeVariationsFeatureArray() {
  return FeatureArray{{
#define DEFINE_FEATURE(name, default_enabled)          \
  {#name, default_enabled ? FEATURE_ENABLED_BY_DEFAULT \
                          : FEATURE_DISABLED_BY_DEFAULT},
#include "cras/platform/features/features.inc"
#undef DEFINE_FEATURE
  }};
}

std::vector<const VariationsFeature*> makeVariationsFeaturePtrVector(
    const FeatureArray& features) {
  std::vector<const VariationsFeature*> out;
  out.reserve(features.size());
  for (const VariationsFeature& feature : features) {
    out.push_back(&feature);
  }
  return out;
}

class Worker {
 public:
  Worker()
      : features_(makeVariationsFeatureArray()),
        feature_ptrs_(makeVariationsFeaturePtrVector(features_)),
        feature_status_(DefaultState()) {}
  // Disallow copy and move.
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;
  Worker(Worker&&) = delete;
  Worker& operator=(Worker&&) = delete;

  // Destruct the worker.
  // Must be called from the main thread.
  ~Worker() {
    thread_->task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&FeatureLibraryAdapter::Shutdown,
                                  base::Unretained(adapter_.get())));
    thread_->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&base::WeakPtrFactory<Worker>::InvalidateWeakPtrs,
                       base::Unretained(&weak_factory_)));
    thread_->Stop();
  }

  // Start the worker thread and wait for it to be started.
  // Must be called from the main thread.
  int Start(std::unique_ptr<FeatureLibraryAdapter> adapter) {
    assert(!started_);
    started_ = true;

    adapter_ = std::move(adapter);

    std::promise<int> status;
    std::future<int> status_future = status.get_future();

    thread_ = std::make_unique<base::Thread>("feature_library_worker");
    if (!thread_->StartWithOptions(
            base::Thread::Options(base::MessagePumpType::IO, 0))) {
      syslog(LOG_ERR, "Cannot start feature_library_worker thread.");
      return -EIO;
    }

    scoped_refptr<base::SingleThreadTaskRunner> task_runner =
        thread_->task_runner();
    task_runner->PostTask(
        FROM_HERE, base::BindOnce(&Worker::SpawnTasks, base::Unretained(this),
                                  std::move(status)));

    if (status_future.wait_for(std::chrono::seconds(3)) !=
        std::future_status::ready) {
      syslog(LOG_ERR, "feature worker ready timeout.");
      return -ETIMEDOUT;
    }
    return status_future.get();
  }

  // Tells whether feature id is enabled.
  // Thread safe.
  bool IsEnabled(enum cras_feature_id id) {
    std::shared_lock lock(feature_status_mux_);
    return feature_status_[id];
  }

 private:
  using FeatureStatus = std::array<bool, NUM_FEATURES>;

  // Update the feature status.
  // Thread safe.
  void Update(FeatureStatus& payload) {
    std::unique_lock lock(feature_status_mux_);
    feature_status_ = payload;
  }

  // Returns the default states of the features.
  static FeatureStatus DefaultState() {
    FeatureStatus status;
#define DEFINE_FEATURE(name, default_enabled) status[name] = default_enabled;
#include "cras/platform/features/features.inc"
#undef DEFINE_FEATURE
    return status;
  }

  // Callback for GetParamsAndEnabled().
  void GetParamsCallback(feature::PlatformFeatures::ParamsResult result) {
    auto update = DefaultState();
    for (const auto& [name, entry] : result) {
      enum cras_feature_id id = cras_feature_get_by_name(name.c_str());
      if (id == CrOSLateBootUnknown) {
        continue;
      }
      update[id] = entry.enabled;
    }

    Update(update);
  }

  // Trigger fetching features.
  // Must be called on the worker thread.
  void Fetch() {
    adapter_->Get()->GetParamsAndEnabled(
        feature_ptrs_,
        base::BindOnce(&Worker::GetParamsCallback, weak_factory_.GetWeakPtr()));
  }

  // Callback when ListenForRefetchNeeded is attached.
  void Ready(std::promise<int> rc, bool attached) {
    if (!attached) {
      rc.set_value(-ECONNRESET);
      syslog(LOG_ERR, "Failed to attach ListenForRefetchNeeded");
      return;
    }

    // Fetch once after initialization.
    Fetch();
    rc.set_value(0);
  }

  // The entry point of the worker thread.
  // Returns the status via the promise rc.
  // Must be called on the worker thread.
  void SpawnTasks(std::promise<int> rc) {
    scoped_refptr<base::SingleThreadTaskRunner> task_runner =
        base::SingleThreadTaskRunner::GetCurrentDefault();

    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    options.dbus_task_runner = task_runner;
    scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

    if (!adapter_->Initialize(bus)) {
      syslog(LOG_ERR, "Failed to initialize PlatformFeatures instance.");
      rc.set_value(-ENODATA);
      return;
    }
    adapter_->Get()->ListenForRefetchNeeded(
        base::BindRepeating(&Worker::Fetch, weak_factory_.GetWeakPtr()),
        base::BindOnce(&Worker::Ready, weak_factory_.GetWeakPtr(),
                       std::move(rc)));
  }

 private:
  // main thread only.
  bool started_ = false;
  std::unique_ptr<base::Thread> thread_;

  // const, safe to share.
  const FeatureArray features_;
  const std::vector<const VariationsFeature*> feature_ptrs_;

  // Set in main thread, used by worker thread.
  std::unique_ptr<FeatureLibraryAdapter> adapter_;

  // shared by mutex.
  std::shared_mutex feature_status_mux_;
  FeatureStatus feature_status_;

  // Chrome's compiler toolchain enforces that any `WeakPtrFactory`
  // fields are declared last, to avoid destruction ordering issues.
  base::WeakPtrFactory<Worker> weak_factory_{this};
};

// Library adapter of the real feature_library.
class FeatureLibraryAdapterImpl : public FeatureLibraryAdapter {
 public:
  // Do nothing.
  ~FeatureLibraryAdapterImpl() override = default;

  bool Initialize(scoped_refptr<dbus::Bus> bus) override {
    bool status = feature::PlatformFeatures::Initialize(bus);
    if (!status) {
      syslog(LOG_ERR, "feature::PlatformFeatures::Initialize failed");
      return false;
    }
    lib_ = feature::PlatformFeatures::Get();
    if (lib_ == nullptr) {
      syslog(LOG_ERR, "feature::PlatformFeatures::Get returned nullptr");
      return false;
    }
    return true;
  }

  void Shutdown() override {
    // Do nothing.
  }
};

Worker* g_worker;

}  // namespace

int backend_featured_init(std::unique_ptr<FeatureLibraryAdapter> adapter) {
  if (g_worker != nullptr) {
    return -EEXIST;
  }

  g_worker = new Worker;
  return g_worker->Start(std::move(adapter));
}

extern "C" {
int cras_features_init() {
  return backend_featured_init(std::make_unique<FeatureLibraryAdapterImpl>());
}

void cras_features_deinit() {
  delete g_worker;
  g_worker = nullptr;
}

bool cras_features_backend_get_enabled(const struct cras_feature* feature) {
  if (g_worker == nullptr) {
    return feature->default_enabled;
  }
  return g_worker->IsEnabled(cras_feature_get_id(feature));
}
}
