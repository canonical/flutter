import 'dart:io';

import 'package:flutter/src/widgets/window_linux.dart';
import 'package:flutter/src/widgets/window_win32.dart';

import 'window.dart';
import 'window_macos.dart';

/// Creates a default [WindowingOwner] for current platform.
/// Only supported on desktop platforms.
WindowingOwner? createDefaultOwner() {
  if (Platform.isMacOS) {
    return WindowingOwnerMacOS();
  } else if (Platform.isWindows) {
    return WindowingOwnerWin32();
  } else if (Platform.isLinux) {
    return WindowingOwnerLinux();
  } else {
    return null;
  }
}
