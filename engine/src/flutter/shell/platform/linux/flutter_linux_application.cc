// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/flutter_linux_application.h"

namespace flutter {

std::unique_ptr<FlutterLinuxApplication> FlutterLinuxApplication::Create() {
  std::unique_ptr<FlutterLinuxApplication> application(
      new FlutterLinuxApplication());
  if (!application->Initialize()) {
    return nullptr;
  }
  return application;
}

FlutterLinuxApplication::~FlutterLinuxApplication() {
  gpu_.reset();
  if (display_) {
    event_loop_->RemoveSource(display_.get());
    display_.reset();
  }
  event_loop_.reset();
}

bool FlutterLinuxApplication::Initialize() {
  event_loop_ = std::make_unique<EventLoop>();

  display_ = WaylandDisplay::Create();
  if (!display_) {
    return false;
  }
  display_->SetErrorCallback([this]() { Quit(); });
  event_loop_->AddSource(display_.get());

  gpu_ = GpuDevice::Create(display_->main_device());
  return gpu_ != nullptr;
}

void FlutterLinuxApplication::Run() {
  event_loop_->Run();
}

void FlutterLinuxApplication::Quit() {
  event_loop_->Quit();
}

}  // namespace flutter
