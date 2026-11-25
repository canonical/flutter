// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_H_

// The C++ API for Flutter Linux applications.
//
// This is header-only so it is compiled with the application's C++ standard
// library, and uses the C interface in flutter_linux_abi.h to call into
// libflutter_linux.so.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "flutter_linux_abi.h"

namespace flutter {

// The result of a function that can fail. Details of failures are logged.
enum class FlutterDesktopResult {
  kSuccess,
  // No display server is available.
  kNoDisplay,
  // The display server doesn't support features that are required.
  kUnsupportedDisplay,
  // The function can't be called in the current state, such as creating the
  // implicit view after the engine has started running.
  kInvalidState,
  // Any other failure.
  kFailed,
};

// Returns a description of |result| suitable for showing to the user.
inline const char* to_string(FlutterDesktopResult result) {
  switch (result) {
    case FlutterDesktopResult::kSuccess:
      return "Success";
    case FlutterDesktopResult::kNoDisplay:
      return "No display server available";
    case FlutterDesktopResult::kUnsupportedDisplay:
      return "Display server not supported";
    case FlutterDesktopResult::kInvalidState:
      return "Invalid state";
    case FlutterDesktopResult::kFailed:
      break;
  }
  return "Failed";
}

namespace internal {

// Converts |result| from the binary interface, and sets |out| to it if not
// nullptr. Returns true if |result| is success.
inline bool set_result(FlutterLinuxResult result, FlutterDesktopResult* out) {
  FlutterDesktopResult value;
  switch (result) {
    case kFlutterLinuxResultSuccess:
      value = FlutterDesktopResult::kSuccess;
      break;
    case kFlutterLinuxResultNoDisplay:
      value = FlutterDesktopResult::kNoDisplay;
      break;
    case kFlutterLinuxResultUnsupportedDisplay:
      value = FlutterDesktopResult::kUnsupportedDisplay;
      break;
    case kFlutterLinuxResultInvalidState:
      value = FlutterDesktopResult::kInvalidState;
      break;
    default:
      value = FlutterDesktopResult::kFailed;
      break;
  }
  if (out != nullptr) {
    *out = value;
  }
  return value == FlutterDesktopResult::kSuccess;
}

}  // namespace internal

// The platform resources shared by all engines in a process.
class FlutterDesktopApplication {
 public:
  // Creates an application. Returns nullptr on failure. If |result| is not
  // nullptr, it is set to the result.
  static std::shared_ptr<FlutterDesktopApplication> create(
      FlutterDesktopResult* result = nullptr) {
    FlutterLinuxApplication* application = nullptr;
    if (!internal::set_result(flutter_linux_application_new(&application),
                              result)) {
      return nullptr;
    }
    return std::shared_ptr<FlutterDesktopApplication>(
        new FlutterDesktopApplication(application));
  }

  ~FlutterDesktopApplication() { flutter_linux_application_free(application_); }

  FlutterDesktopApplication(const FlutterDesktopApplication&) = delete;
  FlutterDesktopApplication& operator=(const FlutterDesktopApplication&) =
      delete;

  // Processes events for all engines until quit is called.
  void run() { flutter_linux_application_run(application_); }

  // Causes run to return. May be called from any thread.
  void quit() { flutter_linux_application_quit(application_); }

  // Sets the ID used to match the application to its desktop file.
  void set_application_id(const std::string& application_id) {
    flutter_linux_application_set_application_id(application_,
                                                 application_id.c_str());
  }

 private:
  friend class FlutterDesktopEngine;

  explicit FlutterDesktopApplication(FlutterLinuxApplication* application)
      : application_(application) {}

  FlutterLinuxApplication* application_;
};

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
  // Creates an engine in |application|. Returns nullptr on failure.
  static std::shared_ptr<FlutterDesktopEngine> create(
      std::shared_ptr<FlutterDesktopApplication> application,
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

    FlutterLinuxEngine* engine =
        flutter_linux_engine_new(application->application_, &abi_properties);
    if (engine == nullptr) {
      return nullptr;
    }
    return std::shared_ptr<FlutterDesktopEngine>(
        new FlutterDesktopEngine(std::move(application), engine));
  }

  ~FlutterDesktopEngine() { flutter_linux_engine_free(engine_); }

  FlutterDesktopEngine(const FlutterDesktopEngine&) = delete;
  FlutterDesktopEngine& operator=(const FlutterDesktopEngine&) = delete;

  // Starts running Dart code. Returns false on failure. If |result| is not
  // nullptr, it is set to the result.
  bool run(FlutterDesktopResult* result = nullptr) {
    return internal::set_result(flutter_linux_engine_run(engine_), result);
  }

  // Returns true if the engine is running.
  bool is_running() const { return flutter_linux_engine_is_running(engine_); }

  // The application this engine runs in.
  const std::shared_ptr<FlutterDesktopApplication>& application() const {
    return application_;
  }

 private:
  friend class FlutterDesktopView;

  FlutterDesktopEngine(std::shared_ptr<FlutterDesktopApplication> application,
                       FlutterLinuxEngine* engine)
      : application_(std::move(application)), engine_(engine) {}

  // Keeps the application alive while the engine exists.
  std::shared_ptr<FlutterDesktopApplication> application_;
  FlutterLinuxEngine* engine_;
};

// A window that shows the output of an engine.
class FlutterDesktopView {
 public:
  // Creates the implicit view of |engine|, which most apps show their
  // content in, in a window of |width| x |height| logical pixels. Must be
  // called before the engine runs, and only once per engine. Returns nullptr
  // on failure. If |result| is not nullptr, it is set to the result.
  static std::shared_ptr<FlutterDesktopView> create_implicit(
      std::shared_ptr<FlutterDesktopEngine> engine,
      int width,
      int height,
      FlutterDesktopResult* result = nullptr) {
    FlutterLinuxView* view = nullptr;
    if (!internal::set_result(flutter_linux_view_new_implicit(
                                  engine->engine_, width, height, &view),
                              result)) {
      return nullptr;
    }
    return std::shared_ptr<FlutterDesktopView>(
        new FlutterDesktopView(std::move(engine), view));
  }

  // Creates a view of |engine| other than the implicit view, in a window of
  // |width| x |height| logical pixels. Returns nullptr on failure. If
  // |result| is not nullptr, it is set to the result.
  static std::shared_ptr<FlutterDesktopView> create(
      std::shared_ptr<FlutterDesktopEngine> engine,
      int width,
      int height,
      FlutterDesktopResult* result = nullptr) {
    FlutterLinuxView* view = nullptr;
    if (!internal::set_result(
            flutter_linux_view_new(engine->engine_, width, height, &view),
            result)) {
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

  // Sets the title of the window.
  void set_title(const std::string& title) {
    flutter_linux_view_set_title(view_, title.c_str());
  }

  // The engine this view shows.
  const std::shared_ptr<FlutterDesktopEngine>& engine() const {
    return engine_;
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

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_H_
