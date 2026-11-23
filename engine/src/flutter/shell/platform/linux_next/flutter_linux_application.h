// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_APPLICATION_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_APPLICATION_H_

#include <memory>
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux_next/event_loop.h"
#include "flutter/shell/platform/linux_next/wayland/wayland_display.h"

namespace flutter {

// The platform resources shared by all engines in a process.
//
// The application must be created, run and destroyed on the same thread,
// which becomes the platform thread for all engines. It must outlive all
// engines.
class FlutterLinuxApplication {
 public:
  // Creates an application connected to the display server. Returns nullptr
  // on failure. Sets |result| to the result.
  static std::unique_ptr<FlutterLinuxApplication> Create(
      FlutterLinuxResult* result);

  ~FlutterLinuxApplication();

  EventLoop* event_loop() { return &event_loop_; }

  WaylandDisplay* display() { return display_.get(); }

  // Processes events until Quit is called.
  void Run();

  // Causes Run to return. May be called from any thread.
  void Quit();

  // The ID used to match the application to its desktop file.
  const std::string& application_id() const { return application_id_; }
  void SetApplicationId(const std::string& application_id) {
    application_id_ = application_id;
  }

 private:
  FlutterLinuxApplication();

  EventLoop event_loop_;

  // Must be destroyed before |event_loop_|.
  std::unique_ptr<WaylandDisplay> display_;

  std::string application_id_;

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxApplication);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_APPLICATION_H_
