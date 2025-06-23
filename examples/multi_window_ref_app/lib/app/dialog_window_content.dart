// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'child_window_renderer.dart';
import 'window_manager_model.dart';
import 'window_settings.dart';

class DialogWindowContent extends StatelessWidget {
  const DialogWindowContent(
      {super.key,
      required this.controller,
      required this.windowSettings,
      required this.windowManagerModel});

  final DialogWindowController controller;
  final WindowSettings windowSettings;
  final WindowManagerModel windowManagerModel;

  @override
  Widget build(BuildContext context) {
    final child = FocusScope(
      autofocus: true,
      child: Scaffold(
        appBar: AppBar(title: Text('${controller.type}')),
        body: SingleChildScrollView(
          child: Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                ListenableBuilder(
                  listenable: controller,
                  builder: (BuildContext context, Widget? _) {
                    if (controller.destroyed) {
                      return SizedBox.shrink();
                    }
                    return Text(
                      'View ID: ${controller.rootView.viewId}\n'
                      'Parent View ID: ${controller.parent?.viewId}\n'
                      'Size: ${(controller.contentSize.width).toStringAsFixed(1)}\u00D7${(controller.contentSize.height).toStringAsFixed(1)}\n'
                      'Device Pixel Ratio: ${MediaQuery.of(context).devicePixelRatio}',
                      textAlign: TextAlign.center,
                    );
                  },
                ),
                const SizedBox(height: 16.0),
                ElevatedButton(
                  onPressed: () {
                    controller.destroy();
                  },
                  child: const Text('Close'),
                ),
              ],
            ),
          ),
        ),
      ),
    );

    return ViewAnchor(
        view: ChildWindowRenderer(
            windowManagerModel: windowManagerModel,
            windowSettings: windowSettings,
            controller: controller),
        child: child);
  }
}
