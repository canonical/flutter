// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/public/flutter_linux_abi.h"

#include <filesystem>

#include "flutter/fml/paths.h"
#include "flutter/shell/platform/linux_next/flutter_linux_application.h"
#include "flutter/shell/platform/linux_next/flutter_linux_engine.h"

namespace {

flutter::FlutterLinuxApplication* ToApplication(
    FlutterLinuxApplication* application) {
  return reinterpret_cast<flutter::FlutterLinuxApplication*>(application);
}

flutter::FlutterLinuxEngine* ToEngine(FlutterLinuxEngine* engine) {
  return reinterpret_cast<flutter::FlutterLinuxEngine*>(engine);
}

// Resolves |path| relative to the executable's directory.
std::filesystem::path ResolvePath(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return {};
  }
  std::filesystem::path result(path);
  if (result.is_relative()) {
    auto [ok, executable_directory] = fml::paths::GetExecutableDirectoryPath();
    if (ok) {
      result = std::filesystem::path(executable_directory) / result;
    }
  }
  return result;
}

}  // namespace

FlutterLinuxApplication* flutter_linux_application_new(void) {
  return reinterpret_cast<FlutterLinuxApplication*>(
      new flutter::FlutterLinuxApplication());
}

void flutter_linux_application_free(FlutterLinuxApplication* application) {
  delete ToApplication(application);
}

void flutter_linux_application_run(FlutterLinuxApplication* application) {
  ToApplication(application)->Run();
}

void flutter_linux_application_quit(FlutterLinuxApplication* application) {
  ToApplication(application)->Quit();
}

void flutter_linux_application_set_application_id(
    FlutterLinuxApplication* application,
    const char* application_id) {
  ToApplication(application)->SetApplicationId(application_id);
}

FlutterLinuxEngine* flutter_linux_engine_new(
    FlutterLinuxApplication* application,
    const FlutterLinuxEngineProperties* properties) {
  flutter::FlutterLinuxEngine::Properties engine_properties;
  engine_properties.assets_path = ResolvePath(properties->assets_path);
  engine_properties.icu_data_path = ResolvePath(properties->icu_data_path);
  engine_properties.aot_library_path =
      ResolvePath(properties->aot_library_path);
  if (properties->dart_entrypoint != nullptr) {
    engine_properties.dart_entrypoint = properties->dart_entrypoint;
  }
  for (int i = 0; i < properties->dart_entrypoint_argc; i++) {
    engine_properties.dart_entrypoint_arguments.push_back(
        properties->dart_entrypoint_argv[i]);
  }

  return reinterpret_cast<FlutterLinuxEngine*>(new flutter::FlutterLinuxEngine(
      ToApplication(application), std::move(engine_properties)));
}

void flutter_linux_engine_free(FlutterLinuxEngine* engine) {
  delete ToEngine(engine);
}

bool flutter_linux_engine_run(FlutterLinuxEngine* engine) {
  return ToEngine(engine)->Run();
}

bool flutter_linux_engine_is_running(FlutterLinuxEngine* engine) {
  return ToEngine(engine)->running();
}
