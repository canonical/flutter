// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_devicelab/framework/devices.dart';
import 'package:flutter_devicelab/framework/framework.dart';
import 'package:flutter_devicelab/framework/utils.dart';
import 'package:flutter_devicelab/tasks/integration_tests.dart';
import 'package:path/path.dart' as path;

Future<void> main() async {
  deviceOperatingSystem = DeviceOperatingSystem.linux;
  await task(() async {
    // Flutter on Linux requires a Wayland compositor, which CI machines don't
    // have running, so start a headless one.
    final compositor = WaylandCompositor();
    await compositor.start();

    try {
      return await createWindowingDriverTest(
        environment: <String, String>{'WAYLAND_DISPLAY': WaylandCompositor.socketName},
      )();
    } finally {
      compositor.kill();
    }
  });
}

class WaylandCompositor {
  static const socketName = 'wayland-flutter-windowing-test';

  Process? _process;

  Future<void> start() async {
    final String? runtimeDir = Platform.environment['XDG_RUNTIME_DIR'];
    if (runtimeDir == null) {
      throw Exception('XDG_RUNTIME_DIR must be set to run a Wayland compositor');
    }

    final Process process = _process = await startProcess('weston', const <String>[
      '--backend=headless',
      '--socket=$socketName',
      '--width=1280',
      '--height=720',
      '--idle-time=0',
    ]);
    process.stdout
        .transform<String>(const Utf8Decoder())
        .transform<String>(const LineSplitter())
        .listen((String line) => stdout.writeln('[weston stdout] $line'));
    process.stderr
        .transform<String>(const Utf8Decoder())
        .transform<String>(const LineSplitter())
        .listen((String line) => stderr.writeln('[weston stderr] $line'));

    // The compositor is ready when it has created its socket.
    final socket = File(path.join(runtimeDir, socketName));
    final stopwatch = Stopwatch()..start();
    while (!socket.existsSync()) {
      if (stopwatch.elapsed > const Duration(seconds: 10)) {
        throw Exception('Timed out waiting for weston to start');
      }
      await Future<void>.delayed(const Duration(milliseconds: 100));
    }
  }

  void kill() {
    _process?.kill();
    _process = null;
  }
}
