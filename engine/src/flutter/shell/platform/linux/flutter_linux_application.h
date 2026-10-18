// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_APPLICATION_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_APPLICATION_H_

#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/gpu_device.h"
#include "flutter/shell/platform/linux/wayland_display.h"

namespace flutter {

// The platform resources shared by all engines in a process: the event loop,
// the connection to the Wayland compositor and the GPU.
//
// The application must be created, run and destroyed on the same thread,
// which becomes the platform thread for all engines. It must outlive all
// engines.
class DecorationRenderer;

class FlutterLinuxApplication {
 public:
  // Connects to the Wayland compositor and opens the GPU it uses. Returns
  // nullptr if the compositor or GPU are unavailable.
  static std::unique_ptr<FlutterLinuxApplication> Create();

  ~FlutterLinuxApplication();

  EventLoop* event_loop() const { return event_loop_.get(); }
  WaylandDisplay* display() const { return display_.get(); }
  GpuDevice* gpu() const { return gpu_.get(); }

  // Draws window decorations, created on first use. Returns nullptr if
  // decorations can't be drawn. Must be called on the platform thread.
  DecorationRenderer* decoration_renderer();

  // Processes events until Quit is called or the connection to the
  // compositor is lost.
  void Run();

  // Causes Run to return. May be called from any thread.
  void Quit();

 private:
  FlutterLinuxApplication() = default;

  bool Initialize();

  std::unique_ptr<EventLoop> event_loop_;
  std::unique_ptr<WaylandDisplay> display_;
  std::unique_ptr<GpuDevice> gpu_;
  std::unique_ptr<DecorationRenderer> decoration_renderer_;
  bool decoration_renderer_created_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxApplication);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_APPLICATION_H_
