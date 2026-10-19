// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/gestures.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

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

  test('create is deferred until a size is known', () async {
    final LinuxViewController controller = PlatformViewsService.initLinuxView(
      id: 7,
      viewType: 'webview',
      layoutDirection: TextDirection.ltr,
      creationParams: 'hello',
      creationParamsCodec: const StringCodec(),
    );
    expect(controller.awaitingCreation, isTrue);
    expect(calls, isEmpty);

    final createdIds = <int>[];
    controller.addOnPlatformViewCreatedListener(createdIds.add);
    await controller.create(size: const Size(100, 50));

    expect(controller.awaitingCreation, isFalse);
    expect(createdIds, <int>[7]);
    expect(calls, hasLength(1));
    expect(calls[0].method, 'create');
    final args = calls[0].arguments as Map<Object?, Object?>;
    expect(args['id'], 7);
    expect(args['viewType'], 'webview');
    expect(args['width'], 100.0);
    expect(args['height'], 50.0);
    expect(args['params'], isA<Uint8List>());
    expect(String.fromCharCodes(args['params']! as Uint8List), 'hello');
  });

  test('dispose before create sends nothing', () async {
    final LinuxViewController controller = PlatformViewsService.initLinuxView(
      id: 1,
      viewType: 'webview',
      layoutDirection: TextDirection.ltr,
    );
    await controller.dispose();
    expect(calls, isEmpty);
  });

  test('dispose and clearFocus send the view ID', () async {
    final LinuxViewController controller = PlatformViewsService.initLinuxView(
      id: 3,
      viewType: 'webview',
      layoutDirection: TextDirection.ltr,
    );
    await controller.create(size: const Size(10, 10));
    await controller.clearFocus();
    await controller.dispose();
    await controller.dispose();
    expect(calls.map((MethodCall c) => c.method), <String>['create', 'clearFocus', 'dispose']);
    expect(calls[1].arguments, 3);
    expect(calls[2].arguments, 3);
  });

  test('pointer events use local coordinates', () async {
    final LinuxViewController controller = PlatformViewsService.initLinuxView(
      id: 4,
      viewType: 'webview',
      layoutDirection: TextDirection.ltr,
    );
    await controller.create(size: const Size(10, 10));
    calls.clear();

    await controller.dispatchPointerEvent(
      const PointerDownEvent(
        timeStamp: Duration(milliseconds: 5),
        kind: PointerDeviceKind.stylus,
        device: 2,
        position: Offset(110, 220),
      ).transformed(Matrix4.translationValues(-100, -200, 0)),
    );
    await controller.dispatchPointerEvent(
      const PointerScrollEvent(position: Offset(1, 2), scrollDelta: Offset(3, 4)),
    );

    expect(calls, hasLength(2));
    expect(calls[0].method, 'pointer');
    expect(calls[0].arguments, <String, Object?>{
      'id': 4,
      'kind': 'down',
      'deviceKind': 'stylus',
      'device': 2,
      'buttons': kPrimaryButton,
      'x': 10.0,
      'y': 20.0,
      'scrollDeltaX': 0.0,
      'scrollDeltaY': 0.0,
      'timestamp': 5000,
    });
    final scroll = calls[1].arguments as Map<Object?, Object?>;
    expect(scroll['kind'], 'scroll');
    expect(scroll['deviceKind'], 'mouse');
    expect(scroll['scrollDeltaX'], 3.0);
    expect(scroll['scrollDeltaY'], 4.0);
  });
}
