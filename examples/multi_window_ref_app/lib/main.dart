// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:multi_window_ref_app/app/window_controller_render.dart';
import 'package:multi_window_ref_app/app/window_manager_model.dart';
import 'package:multi_window_ref_app/app/window_settings.dart';
import 'app/main_window.dart';

void main() {
  final windowManagerModel = WindowManagerModel();
  final windowSettings = WindowSettings();

  final RegularWindowController mainWindowController = RegularWindowController(
    contentSize: WindowSizing(
      preferredSize: const Size(800, 600),
      constraints: const BoxConstraints(minWidth: 640, minHeight: 480),
    ),
    title: "Multi-Window Reference Application",
  );
  windowManagerModel.add(KeyedWindowController(
      isMainWindow: true, key: UniqueKey(), controller: mainWindowController));

  runWidget(ListenableBuilder(
      listenable: windowManagerModel,
      builder: (BuildContext context, Widget? _) {
        final List<Widget> childViews = <Widget>[];
        for (final KeyedWindowController controller
            in windowManagerModel.windows) {
          if (controller.parent == null) {
            if (controller.isMainWindow) {
              childViews.add(RegularWindow(
                controller: mainWindowController,
                child: MaterialApp(
                    home: MainWindow(
                        windowManagerModel: windowManagerModel,
                        settings: windowSettings,
                        mainController: mainWindowController)),
              ));
            } else {
              childViews.add(WindowControllerRender(
                controller: controller.controller,
                key: controller.key,
                windowSettings: windowSettings,
                windowManagerModel: windowManagerModel,
                onDestroyed: () => windowManagerModel.remove(controller.key),
                onError: () => windowManagerModel.remove(controller.key),
              ));
            }
          }
        }

        return ViewCollection(views: childViews);
      }));
}
