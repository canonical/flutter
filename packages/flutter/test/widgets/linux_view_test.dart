// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late List<MethodCall> calls;

  setUp(() {
    calls = <MethodCall>[];
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger.setMockMethodCallHandler(
      SystemChannels.platform_views,
      (MethodCall call) async {
        calls.add(call);
        return null;
      },
    );
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger.setMockMethodCallHandler(
      SystemChannels.platform_views,
      null,
    );
  });

  testWidgets('LinuxView creates, composites and disposes a platform view', (
    WidgetTester tester,
  ) async {
    final int expectedId = platformViewsRegistry.getNextPlatformViewId() + 1;
    final createdIds = <int>[];
    await tester.pumpWidget(
      Center(
        child: SizedBox(
          width: 200,
          height: 100,
          child: LinuxView(viewType: 'webview', onPlatformViewCreated: createdIds.add),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(calls.map((MethodCall c) => c.method), <String>['create']);
    final args = calls[0].arguments as Map<Object?, Object?>;
    expect(args['id'], expectedId);
    expect(args['viewType'], 'webview');
    expect(args['width'], 200.0);
    expect(args['height'], 100.0);
    expect(createdIds, <int>[expectedId]);

    final PlatformViewLayer layer = tester.layers.whereType<PlatformViewLayer>().single;
    expect(layer.viewId, expectedId);
    expect(layer.rect.size, const Size(200, 100));

    await tester.pumpWidget(const SizedBox());
    expect(calls.last.method, 'dispose');
    expect(calls.last.arguments, expectedId);
  });

  testWidgets('LinuxView forwards pointer events', (WidgetTester tester) async {
    await tester.pumpWidget(
      const Align(
        alignment: Alignment.topLeft,
        child: Padding(
          padding: EdgeInsets.only(left: 50, top: 60),
          child: SizedBox(width: 200, height: 100, child: LinuxView(viewType: 'webview')),
        ),
      ),
    );
    await tester.pumpAndSettle();
    calls.clear();

    await tester.tapAt(const Offset(60, 75));
    await tester.pumpAndSettle();

    final List<Map<Object?, Object?>> events = calls
        .where((MethodCall c) => c.method == 'pointer')
        .map((MethodCall c) => c.arguments as Map<Object?, Object?>)
        .toList();
    expect(events.map((Map<Object?, Object?> e) => e['kind']), <String>['down', 'up']);
    expect(events[0]['x'], 10.0);
    expect(events[0]['y'], 15.0);
  });
}
