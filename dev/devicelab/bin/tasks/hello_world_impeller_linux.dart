// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_devicelab/framework/devices.dart';
import 'package:flutter_devicelab/framework/framework.dart';
import 'package:flutter_devicelab/framework/task_result.dart';
import 'package:flutter_devicelab/framework/utils.dart';
import 'package:path/path.dart' as path;

// Printed by the engine with the backend in use, e.g. "(OpenGLES)".
const String _impellerBackendMessage = 'Using the Impeller rendering backend (';

/// Runs the app with [options] and returns whether the Impeller backend
/// message was seen, or a failure message.
Future<(bool, String?)> _runApp(String step, List<String> options) async {
  final Process process = await startFlutter('run', options: <String>[...options, '-d', 'linux']);

  final completer = Completer<void>();
  var sawImpellerBackendMessage = false;
  var sawVMServiceMessage = false;

  final StreamSubscription<String> subscription = process.stdout
      .transform(utf8.decoder)
      .transform(const LineSplitter())
      .listen((String line) {
        print('[STDOUT $step]: $line');
        if (line.contains(_impellerBackendMessage)) {
          sawImpellerBackendMessage = true;
        }
        if (line.contains('The Dart VM service is listening on') ||
            line.contains('A Dart VM Service')) {
          sawVMServiceMessage = true;
          if (!completer.isCompleted) {
            completer.complete();
          }
        }
      });

  await Future.any(<Future<void>>[
    completer.future,
    Future<void>.delayed(const Duration(minutes: 2)),
  ]);

  process.stdin.writeln('q');
  final int exitCode = await process.exitCode;
  await subscription.cancel();

  if (exitCode != 0) {
    return (false, 'Flutter process $step exited with non-zero exit code: $exitCode');
  } else if (!sawVMServiceMessage) {
    return (false, 'Did not see VM Service message (Step $step)');
  }
  return (sawImpellerBackendMessage, null);
}

Future<TaskResult> run() async {
  deviceOperatingSystem = DeviceOperatingSystem.linux;

  final Directory appDir = dir(path.join(flutterDirectory.path, 'examples/hello_world'));

  try {
    return await inDirectory(appDir, () async {
      await flutter('packages', options: <String>['get']);

      // Step 1: Impeller is disabled by default.
      {
        final (bool sawImpeller, String? failure) = await _runApp('1', <String>[]);
        if (failure != null) {
          return TaskResult.failure(failure);
        } else if (sawImpeller) {
          return TaskResult.failure(
            'Saw "$_impellerBackendMessage" in output but Impeller should be disabled (Step 1)',
          );
        }
      }

      // Step 2: Impeller is enabled with the command line flag.
      {
        final (bool sawImpeller, String? failure) = await _runApp('2', <String>[
          '--enable-impeller',
        ]);
        if (failure != null) {
          return TaskResult.failure(failure);
        } else if (!sawImpeller) {
          return TaskResult.failure('Did not see "$_impellerBackendMessage" in output (Step 2)');
        }
      }

      return TaskResult.success(null);
    });
  } catch (e) {
    return TaskResult.failure('Test failed with exception: $e');
  }
}

Future<void> main() async {
  await task(run);
}
