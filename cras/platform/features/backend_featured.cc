/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/platform/features/backend_featured.hh"

#include <array>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/thread_pool.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/threading/thread.h>
#include <bitset>
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
#include "cras/server/main_message.h"

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
  int Start(std::unique_ptr<FeatureLibraryAdapter> adapter) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_checker_);
    CHECK(!started_);
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
    DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_checker_);
    std::shared_lock lock(feature_status_mux_);
    return feature_status_[id];
  }

 private:
  using FeatureStatus = std::bitset<NUM_FEATURES>;

  // Update the feature status.
  // Thread safe.
  void Update(FeatureStatus& payload) {
    CHECK(thread_->task_runner()->BelongsToCurrentThread());
    bool notification_needed = false;
    {
      std::unique_lock lock(feature_status_mux_);
      notification_needed = feature_status_ != payload;
      feature_status_ = payload;
    }
    if (notification_needed) {
      struct cras_main_message msg = {
          .length = sizeof(msg),
          .type = CRAS_MAIN_FEATURE_CHANGED,
      };
      cras_main_message_send(&msg);
    }
  }

  // Returns the default states of the features.
  // Called from both main and worker thread.
  static FeatureStatus DefaultState() {
    FeatureStatus status;
#define DEFINE_FEATURE(name, default_enabled) status[name] = default_enabled;
#include "cras/platform/features/features.inc"
#undef DEFINE_FEATURE
    return status;
  }

  // Callback for GetParamsAndEnabled().
  void GetParamsCallback(feature::PlatformFeatures::ParamsResult result) {
    CHECK(thread_->task_runner()->BelongsToCurrentThread());
    auto update = DefaultState();
    for (const auto& [name, entry] : result) {
      enum cras_feature_id id = cras_feature_get_by_name(name.c_str());
      if (id == CrOSLateBootUnknown) {
        continue;
      }
      update[id] = entry.enabled;
    }

    Update(update);
    std::string bits = update.to_string();
    // Reverse so the first feature defined in the enum is printed first.
    std::reverse(bits.begin(), bits.end());
    syslog(LOG_INFO, "features/backend_featured updated: %s (LSB first)",
           bits.c_str());
  }

  // Trigger fetching features.
  void Fetch() {
    CHECK(thread_->task_runner()->BelongsToCurrentThread());
    adapter_->Get()->GetParamsAndEnabled(
        feature_ptrs_,
        base::BindOnce(&Worker::GetParamsCallback, weak_factory_.GetWeakPtr()));
  }

  // Callback when ListenForRefetchNeeded is attached.
  void Ready(std::promise<int> rc, bool attached) {
    CHECK(thread_->task_runner()->BelongsToCurrentThread());
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
  void SpawnTasks(std::promise<int> rc) {
    CHECK(thread_->task_runner()->BelongsToCurrentThread());
    scoped_refptr<base::SingleThreadTaskRunner> task_runner =
        thread_->task_runner();

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

  SEQUENCE_CHECKER(main_sequence_checker_);

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
