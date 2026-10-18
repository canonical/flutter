// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/public/flutter_linux.h"

#include <filesystem>
#include <memory>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/path_utils.h"
#include "flutter/shell/platform/linux/flutter_linux_engine.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"

namespace {

// The state behind a FlutterDesktopViewControllerRef.
struct ViewController {
  std::unique_ptr<flutter::FlutterLinuxEngine> engine;
  std::unique_ptr<flutter::FlutterLinuxView> view;
};

flutter::FlutterLinuxEngine* EngineFromHandle(FlutterDesktopEngineRef ref) {
  return reinterpret_cast<flutter::FlutterLinuxEngine*>(ref);
}

FlutterDesktopEngineRef HandleForEngine(flutter::FlutterLinuxEngine* engine) {
  return reinterpret_cast<FlutterDesktopEngineRef>(engine);
}

ViewController* ControllerFromHandle(FlutterDesktopViewControllerRef ref) {
  return reinterpret_cast<ViewController*>(ref);
}

flutter::FlutterLinuxView* ViewFromHandle(FlutterDesktopViewRef ref) {
  return reinterpret_cast<flutter::FlutterLinuxView*>(ref);
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

FlutterDesktopEngineRef FlutterDesktopEngineCreate(
    const FlutterDesktopEngineProperties* engine_properties) {
  flutter::FlutterLinuxEngine::Properties properties;
  properties.assets_path = ResolvePath(engine_properties->assets_path);
  properties.icu_data_path = ResolvePath(engine_properties->icu_data_path);
  properties.aot_library_path =
      ResolvePath(engine_properties->aot_library_path);
  if (engine_properties->dart_entrypoint != nullptr) {
    properties.dart_entrypoint = engine_properties->dart_entrypoint;
  }
  for (int i = 0; i < engine_properties->dart_entrypoint_argc; i++) {
    properties.dart_entrypoint_arguments.push_back(
        engine_properties->dart_entrypoint_argv[i]);
  }

  return HandleForEngine(
      flutter::FlutterLinuxEngine::Create(std::move(properties)).release());
}

bool FlutterDesktopEngineDestroy(FlutterDesktopEngineRef engine_ref) {
  flutter::FlutterLinuxEngine* engine = EngineFromHandle(engine_ref);
  bool result = engine->Stop();
  delete engine;
  return result;
}

bool FlutterDesktopEngineRun(FlutterDesktopEngineRef engine,
                             const char* entry_point) {
  return EngineFromHandle(engine)->Run(entry_point != nullptr ? entry_point
                                                              : "");
}

void FlutterDesktopEngineRunEventLoop(FlutterDesktopEngineRef engine) {
  EngineFromHandle(engine)->RunEventLoop();
}

void FlutterDesktopEngineQuitEventLoop(FlutterDesktopEngineRef engine) {
  EngineFromHandle(engine)->QuitEventLoop();
}

FlutterDesktopViewControllerRef FlutterDesktopViewControllerCreate(
    int width,
    int height,
    FlutterDesktopEngineRef engine_ref) {
  auto controller = std::make_unique<ViewController>();
  controller->engine.reset(EngineFromHandle(engine_ref));

  controller->view = controller->engine->CreateView(width, height);
  if (!controller->view) {
    return nullptr;
  }

  if (!controller->engine->running() && !controller->engine->Run("")) {
    controller->engine->RemoveView(controller->view.get());
    return nullptr;
  }

  return reinterpret_cast<FlutterDesktopViewControllerRef>(
      controller.release());
}

void FlutterDesktopViewControllerDestroy(
    FlutterDesktopViewControllerRef controller_ref) {
  ViewController* controller = ControllerFromHandle(controller_ref);

  // Stop rendering before the view is destroyed.
  controller->engine->Stop();
  controller->engine->RemoveView(controller->view.get());
  controller->view.reset();
  controller->engine.reset();
  delete controller;
}

FlutterDesktopViewId FlutterDesktopViewControllerGetViewId(
    FlutterDesktopViewControllerRef controller) {
  return ControllerFromHandle(controller)->view->view_id();
}

FlutterDesktopEngineRef FlutterDesktopViewControllerGetEngine(
    FlutterDesktopViewControllerRef controller) {
  return HandleForEngine(ControllerFromHandle(controller)->engine.get());
}

FlutterDesktopViewRef FlutterDesktopViewControllerGetView(
    FlutterDesktopViewControllerRef controller) {
  return reinterpret_cast<FlutterDesktopViewRef>(
      ControllerFromHandle(controller)->view.get());
}

void FlutterDesktopViewSetTitle(FlutterDesktopViewRef view, const char* title) {
  ViewFromHandle(view)->window()->SetTitle(title);
}

void FlutterDesktopViewSetApplicationId(FlutterDesktopViewRef view,
                                        const char* application_id) {
  ViewFromHandle(view)->window()->SetApplicationId(application_id);
}
