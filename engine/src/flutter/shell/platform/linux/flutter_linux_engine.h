// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_ENGINE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_ENGINE_H_

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/compositor_opengl.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/wayland_display.h"

namespace flutter {

class FlutterLinuxView;

// The view ID of the view that exists for the lifetime of the engine.
constexpr FlutterViewId kImplicitViewId = 0;

// An instance of a Flutter engine, and the platform resources it uses.
//
// The engine must be created, run and destroyed on the same thread, which
// becomes the platform thread.
class FlutterLinuxEngine {
 public:
  struct Properties {
    std::filesystem::path assets_path;
    std::filesystem::path icu_data_path;
    std::filesystem::path aot_library_path;
    std::string dart_entrypoint;
    std::vector<std::string> dart_entrypoint_arguments;
  };

  // Creates an engine and connects to the display server. Returns nullptr if
  // the display server or GPU are unavailable.
  static std::unique_ptr<FlutterLinuxEngine> Create(Properties properties);

  ~FlutterLinuxEngine();

  // Starts running the engine with the given entrypoint. If empty, the
  // entrypoint in the properties is used, or 'main' if that is also empty.
  bool Run(std::string_view entrypoint);

  // Returns true if the engine is running.
  bool running() const { return engine_ != nullptr; }

  // Stops the engine. Returns true if the engine was stopped or was not
  // running.
  bool Stop();

  // Processes events until QuitEventLoop is called.
  void RunEventLoop();

  // Causes RunEventLoop to return. May be called from any thread.
  void QuitEventLoop();

  WaylandDisplay* display() const { return display_.get(); }

  // Creates the view shown in a new window of the given logical size.
  //
  // Only the implicit view is currently supported, so this can only be
  // called once.
  std::unique_ptr<FlutterLinuxView> CreateView(int width, int height);

  // Detaches |view| from the engine before it is destroyed.
  void RemoveView(FlutterLinuxView* view);

  // Informs the engine of the size of a view.
  void SendWindowMetrics(const FlutterWindowMetricsEvent& event);

  // Called when the user requests a view be closed.
  void OnViewClose(FlutterLinuxView* view);

 private:
  explicit FlutterLinuxEngine(Properties properties);

  bool Initialize();

  // Returns true if Impeller has been enabled with a command line switch.
  bool IsImpellerEnabled() const;

  // Posts a Flutter task to be run on the platform thread.
  void PostFlutterTask(FlutterTask task, uint64_t target_time_nanos);

  // Responds to a message from the framework.
  void HandlePlatformMessage(const FlutterPlatformMessage* message);

  FlutterRendererConfig GetRendererConfig();

  Properties properties_;
  std::vector<std::string> switches_;

  FlutterEngineProcTable embedder_api_ = {};
  FLUTTER_API_SYMBOL(FlutterEngine) engine_ = nullptr;
  FlutterEngineAOTData aot_data_ = nullptr;

  std::unique_ptr<EventLoop> event_loop_;
  std::unique_ptr<WaylandDisplay> display_;
  std::unique_ptr<EGLManager> egl_;
  std::unique_ptr<CompositorOpenGL> compositor_;

  // The views shown by this engine. Owned by their view controllers.
  std::map<FlutterViewId, FlutterLinuxView*> views_;

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxEngine);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_ENGINE_H_
