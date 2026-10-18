// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/public/flutter_linux_abi.h"

#include <filesystem>
#include <memory>

#include "flutter/shell/platform/common/path_utils.h"
#include "flutter/shell/platform/linux/flutter_linux_application.h"
#include "flutter/shell/platform/linux/flutter_linux_engine.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"

namespace {

flutter::FlutterLinuxApplication* ToApplication(
    FlutterLinuxApplication* application) {
  return reinterpret_cast<flutter::FlutterLinuxApplication*>(application);
}

flutter::FlutterLinuxEngine* ToEngine(FlutterLinuxEngine* engine) {
  return reinterpret_cast<flutter::FlutterLinuxEngine*>(engine);
}

flutter::FlutterLinuxView* ToView(FlutterLinuxView* view) {
  return reinterpret_cast<flutter::FlutterLinuxView*>(view);
}

// Resolves |path| relative to the executable's directory.
std::filesystem::path ResolvePath(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return {};
  }
  std::filesystem::path result(path);
  if (result.is_relative()) {
    result = flutter::GetExecutableDirectory() / result;
  }
  return result;
}

}  // namespace

FlutterLinuxApplication* flutter_linux_application_new(void) {
  return reinterpret_cast<FlutterLinuxApplication*>(
      flutter::FlutterLinuxApplication::Create().release());
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

  return reinterpret_cast<FlutterLinuxEngine*>(
      flutter::FlutterLinuxEngine::Create(ToApplication(application),
                                          std::move(engine_properties))
          .release());
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

FlutterLinuxView* flutter_linux_view_new(FlutterLinuxEngine* engine,
                                         int width,
                                         int height) {
  return reinterpret_cast<FlutterLinuxView*>(
      ToEngine(engine)->CreateView(width, height).release());
}

void flutter_linux_view_free(FlutterLinuxView* view_handle) {
  flutter::FlutterLinuxView* view = ToView(view_handle);
  // Ensure the raster thread is no longer presenting to the view before it is
  // destroyed.
  view->engine()->RemoveView(view);
  delete view;
}

int64_t flutter_linux_view_get_id(FlutterLinuxView* view) {
  return ToView(view)->view_id();
}

void flutter_linux_view_set_title(FlutterLinuxView* view, const char* title) {
  ToView(view)->window()->SetTitle(title);
}

void flutter_linux_view_set_application_id(FlutterLinuxView* view,
                                           const char* application_id) {
  ToView(view)->window()->SetApplicationId(application_id);
}

void flutter_linux_view_set_close_callback(
    FlutterLinuxView* view,
    FlutterLinuxViewCloseCallback callback,
    void* user_data) {
  if (callback == nullptr) {
    ToView(view)->SetCloseCallback(nullptr);
    return;
  }
  ToView(view)->SetCloseCallback(
      [callback, user_data]() { callback(user_data); });
}
