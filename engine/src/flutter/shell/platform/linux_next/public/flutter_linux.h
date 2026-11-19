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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "flutter_linux_abi.h"

namespace flutter {

// The platform resources shared by all engines in a process.
class FlutterDesktopApplication {
 public:
  // Creates an application. Returns nullptr on failure.
  static std::shared_ptr<FlutterDesktopApplication> create() {
    FlutterLinuxApplication* application = flutter_linux_application_new();
    if (application == nullptr) {
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

  // Starts running Dart code. Returns false on failure.
  bool run() { return flutter_linux_engine_run(engine_); }

  // Returns true if the engine is running.
  bool is_running() const { return flutter_linux_engine_is_running(engine_); }

  // The application this engine runs in.
  const std::shared_ptr<FlutterDesktopApplication>& application() const {
    return application_;
  }

 private:
  FlutterDesktopEngine(std::shared_ptr<FlutterDesktopApplication> application,
                       FlutterLinuxEngine* engine)
      : application_(std::move(application)), engine_(engine) {}

  // Keeps the application alive while the engine exists.
  std::shared_ptr<FlutterDesktopApplication> application_;
  FlutterLinuxEngine* engine_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_H_
