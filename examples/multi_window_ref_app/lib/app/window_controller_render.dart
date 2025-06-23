// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'dialog_window_content.dart';
import 'regular_window_content.dart';
import 'window_manager_model.dart';
import 'window_settings.dart';

class WindowControllerRender extends StatelessWidget {
  const WindowControllerRender({
    required this.controller,
    required this.onDestroyed,
    required this.onError,
    required this.windowSettings,
    required this.windowManagerModel,
    required super.key,
  });

  final WindowController controller;
  final VoidCallback onDestroyed;
  final VoidCallback onError;
  final WindowSettings windowSettings;
  final WindowManagerModel windowManagerModel;

  @override
  Widget build(BuildContext context) {
    switch (controller.type) {
      case WindowArchetype.regular:
        return RegularWindow(
          key: key,
          controller: controller as RegularWindowController,
          child: RegularWindowContent(
              window: controller as RegularWindowController,
              windowSettings: windowSettings,
              windowManagerModel: windowManagerModel),
        );
      case WindowArchetype.dialog:
        final dialogController = controller as DialogWindowController;
        return DialogWindow(
          key: key,
          controller: dialogController,
          child: DialogWindowContent(
            controller: dialogController,
            windowSettings: windowSettings,
            windowManagerModel: windowManagerModel,
          ),
        );
    }
  }
}
