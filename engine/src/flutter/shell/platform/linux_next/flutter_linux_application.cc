// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/flutter_linux_application.h"

namespace flutter {

FlutterLinuxApplication::FlutterLinuxApplication() = default;

FlutterLinuxApplication::~FlutterLinuxApplication() = default;

void FlutterLinuxApplication::Run() {
  event_loop_.Run();
}

void FlutterLinuxApplication::Quit() {
  event_loop_.Quit();
}

}  // namespace flutter
