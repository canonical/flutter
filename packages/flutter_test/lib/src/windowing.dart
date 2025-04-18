import 'package:flutter/widgets.dart';

/// WindowingOwner used in Flutter Tester.
class TestWindowingOwner extends WindowingOwner {
  @override
  RegularWindowController createRegularWindowController({
    required WindowSizing contentSize,
    required RegularWindowControllerDelegate delegate,
  }) {
    throw UnsupportedError('Current platform does not support windowing.\n');
  }

  @override
  bool hasTopLevelWindows() {
    return false;
  }
}
