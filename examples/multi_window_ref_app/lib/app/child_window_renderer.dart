// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'window_controller_render.dart';
import 'window_manager_model.dart';
import 'window_settings.dart';

class ChildWindowRenderer extends StatelessWidget {
  const ChildWindowRenderer({
    required this.windowManagerModel,
    required this.windowSettings,
    required this.controller,
    super.key,
  });

  final WindowManagerModel windowManagerModel;
  final WindowSettings windowSettings;
  final WindowController controller;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: windowManagerModel,
      builder: (BuildContext context, Widget? _) {
        final childViews = windowManagerModel.windows
            .where((child) => child.parent == controller)
            .map(
              (child) => WindowControllerRender(
                controller: child.controller,
                key: child.key,
                windowSettings: windowSettings,
                windowManagerModel: windowManagerModel,
                onDestroyed: () => windowManagerModel.remove(child.key),
                onError: () => windowManagerModel.remove(child.key),
              ),
            )
            .toList();

        return ViewCollection(views: childViews);
      },
    );
  }
}
