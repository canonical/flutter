// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets(
      'Widgets running in an initialWindow that was created by MultiWindowApp in runWidget can find their WindowContext',
      (WidgetTester tester) async {
    WindowContext? windowContext;

    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.windowing, (MethodCall call) async {
      final Map<Object?, Object?> args =
          call.arguments as Map<Object?, Object?>;
      switch (call.method) {
        case 'createRegularWindow':
          {
            final int width = args['width']! as int;
            final int height = args['height']! as int;

            return {
              'viewId': tester.view.viewId,
              'archetype': WindowArchetype.regular.index,
              'width': width,
              'height': height,
              'parentViewId': null
            };
          }
        default:
          throw Exception('Unsupported method call: ${call.method}');
      }
    });

    await tester.pumpWidget(wrapWithView: false, Builder(
      builder: (BuildContext context) {
        return MultiWindowApp(
          initialWindows: <Future<Window> Function(BuildContext)>[
            (BuildContext context) => createRegularWindow(
                context: context,
                size: const Size(800, 600),
                builder: (BuildContext context) {
                  return Builder(builder: (BuildContext context) {
                    windowContext = WindowContext.of(context);
                    return Container();
                  });
                })
          ],
        );
      },
    ));

    await tester.pump();
    expect(windowContext, isNotNull);
  });
}
