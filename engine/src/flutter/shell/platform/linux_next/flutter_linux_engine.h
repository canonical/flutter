// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_ENGINE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_ENGINE_H_

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux_next/flutter_linux_application.h"

namespace flutter {

// An instance of the Flutter engine, running on the platform thread of an
// application.
class FlutterLinuxEngine {
 public:
  struct Properties {
    std::filesystem::path assets_path;
    std::filesystem::path icu_data_path;
    std::filesystem::path aot_library_path;
    std::string dart_entrypoint;
    std::vector<std::string> dart_entrypoint_arguments;
  };

  // Creates an engine in |application|, which must outlive the engine.
  FlutterLinuxEngine(FlutterLinuxApplication* application,
                     Properties properties);
  ~FlutterLinuxEngine();

  // Starts running Dart code. Returns false on failure.
  bool Run();

  // Returns true if the engine is running.
  bool running() const { return engine_ != nullptr; }

 private:
  // Schedules |task| to run on the platform thread.
  void PostFlutterTask(FlutterTask task, uint64_t target_time_nanos);

  // Handles a message sent from the framework.
  void HandlePlatformMessage(const FlutterPlatformMessage* message);

  FlutterLinuxApplication* application_;
  Properties properties_;
  FlutterEngineProcTable embedder_api_ = {};
  FlutterEngineAOTData aot_data_ = nullptr;
  FLUTTER_API_SYMBOL(FlutterEngine) engine_ = nullptr;

  // Tasks scheduled on the event loop hold a weak reference to this, so they
  // don't run after the engine is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxEngine);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_ENGINE_H_
