// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_H_

// The C++ API for embedding Flutter in a Linux application.
//
// This is header-only so it is compiled with the application's C++ standard
// library. It is a thin layer over flutter_linux_abi.h.
//
// All objects must be created, used and destroyed on the same thread, which
// becomes the platform thread. The exception is
// FlutterDesktopEngine::quit_event_loop, which may be called from any thread.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "flutter_linux_abi.h"

namespace flutter {

// Properties for configuring a Flutter engine.
struct FlutterDesktopEngineProperties {
  // The path to the flutter_assets folder for the application to be run.
  // Relative paths are relative to the directory containing the executable.
  std::string assets_path;

  // The path to the icudtl.dat file for the version of Flutter being used.
  // Relative paths are relative to the directory containing the executable.
  std::string icu_data_path;

  // The path to the AOT library for the application. This is ignored for
  // non-AOT (debug) builds. Relative paths are relative to the directory
  // containing the executable.
  std::string aot_library_path;

  // The name of the top-level Dart entrypoint function. If empty, 'main' is
  // used. Custom entrypoints must be in the same library as main() and be
  // decorated with `@pragma('vm:entry-point')`.
  std::string dart_entrypoint;

  // Arguments passed to the Dart entrypoint.
  std::vector<std::string> dart_entrypoint_arguments;
};

// An instance of the Flutter engine.
class FlutterDesktopEngine {
 public:
  // Creates an engine and connects to the Wayland compositor. Returns nullptr
  // if no compositor or GPU is available.
  static std::shared_ptr<FlutterDesktopEngine> create(
      const FlutterDesktopEngineProperties& properties) {
    std::vector<const char*> argv;
    argv.reserve(properties.dart_entrypoint_arguments.size());
    for (const std::string& argument : properties.dart_entrypoint_arguments) {
      argv.push_back(argument.c_str());
    }

    FlutterLinuxEngineProperties abi_properties = {};
    abi_properties.assets_path = properties.assets_path.c_str();
    abi_properties.icu_data_path = properties.icu_data_path.c_str();
    abi_properties.aot_library_path = properties.aot_library_path.c_str();
    abi_properties.dart_entrypoint = properties.dart_entrypoint.c_str();
    abi_properties.dart_entrypoint_argc = static_cast<int>(argv.size());
    abi_properties.dart_entrypoint_argv = argv.data();

    FlutterLinuxEngine* engine = flutter_linux_engine_new(&abi_properties);
    if (engine == nullptr) {
      return nullptr;
    }
    return std::shared_ptr<FlutterDesktopEngine>(
        new FlutterDesktopEngine(engine));
  }

  ~FlutterDesktopEngine() { flutter_linux_engine_free(engine_); }

  FlutterDesktopEngine(const FlutterDesktopEngine&) = delete;
  FlutterDesktopEngine& operator=(const FlutterDesktopEngine&) = delete;

  // Starts running Dart code. Views can be created before or after this is
  // called. Returns false on failure.
  bool run() { return flutter_linux_engine_run(engine_); }

  // Returns true if the engine is running.
  bool is_running() const { return flutter_linux_engine_is_running(engine_); }

  // Processes events until quit_event_loop is called or the user closes the
  // first view.
  void run_event_loop() { flutter_linux_engine_run_event_loop(engine_); }

  // Causes run_event_loop to return. May be called from any thread.
  void quit_event_loop() { flutter_linux_engine_quit_event_loop(engine_); }

 private:
  friend class FlutterDesktopView;

  explicit FlutterDesktopEngine(FlutterLinuxEngine* engine) : engine_(engine) {}

  FlutterLinuxEngine* engine_;
};

// A view showing Flutter content in a top-level window.
class FlutterDesktopView {
 public:
  // Creates a view of |engine| in a new window of the given logical size. The
  // compositor may choose a different size. Returns nullptr on failure.
  //
  // Only a single view is currently supported.
  static std::shared_ptr<FlutterDesktopView>
  create(std::shared_ptr<FlutterDesktopEngine> engine, int width, int height) {
    FlutterLinuxView* view =
        flutter_linux_view_new(engine->engine_, width, height);
    if (view == nullptr) {
      return nullptr;
    }
    return std::shared_ptr<FlutterDesktopView>(
        new FlutterDesktopView(std::move(engine), view));
  }

  ~FlutterDesktopView() { flutter_linux_view_free(view_); }

  FlutterDesktopView(const FlutterDesktopView&) = delete;
  FlutterDesktopView& operator=(const FlutterDesktopView&) = delete;

  // The ID Flutter uses for this view.
  int64_t id() const { return flutter_linux_view_get_id(view_); }

  // The engine this view shows.
  const std::shared_ptr<FlutterDesktopEngine>& engine() const {
    return engine_;
  }

  // Sets the window title.
  void set_title(const std::string& title) {
    flutter_linux_view_set_title(view_, title.c_str());
  }

  // Sets the application ID of the window. The compositor uses this to group
  // windows and find the application's .desktop file, so it should match the
  // name of that file.
  void set_application_id(const std::string& application_id) {
    flutter_linux_view_set_application_id(view_, application_id.c_str());
  }

 private:
  FlutterDesktopView(std::shared_ptr<FlutterDesktopEngine> engine,
                     FlutterLinuxView* view)
      : engine_(std::move(engine)), view_(view) {}

  // Keeps the engine alive while the view exists.
  std::shared_ptr<FlutterDesktopEngine> engine_;
  FlutterLinuxView* view_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_H_
