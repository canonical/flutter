// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_ENGINE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_ENGINE_H_

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux_next/compositor.h"
#include "flutter/shell/platform/linux_next/flutter_linux_application.h"
#include "flutter/shell/platform/linux_next/public/flutter_linux_result.h"

namespace flutter {

class FlutterLinuxView;

// The ID of the view the framework expects to exist from when it starts.
constexpr FlutterViewId kImplicitViewId = 0;

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

  // Starts running Dart code.
  FlutterLinuxResult Run();

  // Returns true if the engine is running.
  bool running() const { return engine_ != nullptr; }

  FlutterLinuxApplication* application() const { return application_; }

  // Creates the implicit view, in a window of |width| x |height| logical
  // pixels. The framework expects the implicit view to exist from when it
  // starts, so this must be called before the engine runs, and only once.
  // Returns nullptr on failure. Sets |result| to the result.
  //
  // The view must be removed with RemoveView before it is destroyed.
  std::unique_ptr<FlutterLinuxView>
  CreateImplicitView(int width, int height, FlutterLinuxResult* result);

  // Creates a view other than the implicit view, in a window of |width| x
  // |height| logical pixels. Returns nullptr on failure. Sets |result| to the
  // result.
  //
  // The view must be removed with RemoveView before it is destroyed.
  std::unique_ptr<FlutterLinuxView> CreateView(int width,
                                               int height,
                                               FlutterLinuxResult* result);

  // Stops showing the engine's output in |view| and removes it from the
  // engine.
  void RemoveView(FlutterLinuxView* view);

  // Tells the engine the size and scale of a view.
  void SendWindowMetrics(const FlutterWindowMetricsEvent& event);

  // Called when the user requests |view| be closed.
  void OnViewClose(FlutterLinuxView* view);

 private:
  // Creates a view with |view_id| and registers it with the engine.
  std::unique_ptr<FlutterLinuxView> CreateView(FlutterViewId view_id,
                                               int width,
                                               int height,
                                               FlutterLinuxResult* result);

  // Registers |view| with the engine.
  std::unique_ptr<FlutterLinuxView> AddView(
      std::unique_ptr<FlutterLinuxView> view);

  // Tells the running engine about a view other than the implicit view.
  void AddViewToEngine(FlutterLinuxView* view);

  // Schedules |task| to run on the platform thread.
  void PostFlutterTask(FlutterTask task, uint64_t target_time_nanos);

  // Handles a message sent from the framework.
  void HandlePlatformMessage(const FlutterPlatformMessage* message);

  FlutterLinuxApplication* application_;
  Properties properties_;
  FlutterEngineProcTable embedder_api_ = {};
  FlutterEngineAOTData aot_data_ = nullptr;
  FLUTTER_API_SYMBOL(FlutterEngine) engine_ = nullptr;
  std::unique_ptr<Compositor> compositor_;

  // The views of this engine, which are owned by their creators.
  std::map<FlutterViewId, FlutterLinuxView*> views_;
  FlutterViewId next_view_id_ = kImplicitViewId + 1;
  bool implicit_view_created_ = false;

  // Tasks scheduled on the event loop hold a weak reference to this, so they
  // don't run after the engine is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxEngine);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_ENGINE_H_
