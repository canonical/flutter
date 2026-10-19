// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/flutter_linux_application.h"

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/decoration_renderer.h"

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
  decoration_renderer_.reset();
  atspi_.reset();
  gpu_.reset();
  if (display_) {
    event_loop_->RemoveSource(display_.get());
    display_.reset();
  }
  event_loop_.reset();
}

bool FlutterLinuxApplication::Initialize() {
  event_loop_ = std::make_unique<EventLoop>();

  display_ = WaylandDisplay::Create(event_loop_.get());
  if (!display_) {
    return false;
  }
  display_->SetErrorCallback([this]() { Quit(); });
  event_loop_->AddSource(display_.get());

  gpu_ = GpuDevice::Create(display_->main_device());
  if (!gpu_) {
    return false;
  }

  atspi_ = std::make_unique<AtspiApplication>(event_loop_.get());
  atspi_->Start();
  return true;
}

DecorationRenderer* FlutterLinuxApplication::decoration_renderer() {
  if (!decoration_renderer_created_) {
    decoration_renderer_created_ = true;
    decoration_renderer_ =
        DecorationRenderer::Create(gpu_.get(), display_.get());
    if (!decoration_renderer_) {
      FML_LOG(ERROR) << "Failed to create window decoration renderer";
    }
  }
  return decoration_renderer_.get();
}

void FlutterLinuxApplication::Run() {
  event_loop_->Run();
}

void FlutterLinuxApplication::Quit() {
  event_loop_->Quit();
}

}  // namespace flutter
