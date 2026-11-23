// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_RESULT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_RESULT_H_

// Part of the binary interface in flutter_linux_abi.h. Separate so it can be
// used inside the library without the export annotations.

// The result of a function that can fail. Details of failures are logged.
typedef enum {
  kFlutterLinuxResultSuccess = 0,
  // No display server is available.
  kFlutterLinuxResultNoDisplay,
  // The display server doesn't support features that are required.
  kFlutterLinuxResultUnsupportedDisplay,
  // The function can't be called in the current state, such as creating the
  // implicit view after the engine has started running.
  kFlutterLinuxResultInvalidState,
  // Any other failure.
  kFlutterLinuxResultFailed,
} FlutterLinuxResult;

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_RESULT_H_
