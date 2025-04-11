import 'dart:io';

import 'window.dart';
import 'window_macos.dart';
import 'window_win32.dart';

/// Creates a default [WindowingOwner] for current platform.
/// Only supported on desktop platforms.
WindowingOwner? createDefaultOwner() {
  if (Platform.isMacOS) {
    return WindowingOwnerMacOS();
  } else if (Platform.isWindows) {
    return WindowingOwnerWin32();
  } else {
    return null;
  }
}
