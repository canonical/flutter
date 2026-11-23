// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/flutter_linux_application.h"

namespace flutter {

std::unique_ptr<FlutterLinuxApplication> FlutterLinuxApplication::Create(
    FlutterLinuxResult* result) {
  std::unique_ptr<FlutterLinuxApplication> application(
      new FlutterLinuxApplication());
  application->display_ =
      WaylandDisplay::Create(&application->event_loop_, result);
  if (application->display_ == nullptr) {
    return nullptr;
  }

  // Nothing can be shown without the display server.
  application->display_->SetErrorCallback(
      [application = application.get()]() { application->Quit(); });

  return application;
}

FlutterLinuxApplication::FlutterLinuxApplication() = default;

FlutterLinuxApplication::~FlutterLinuxApplication() = default;

void FlutterLinuxApplication::Run() {
  event_loop_.Run();
}

void FlutterLinuxApplication::Quit() {
  event_loop_.Quit();
}

}  // namespace flutter
