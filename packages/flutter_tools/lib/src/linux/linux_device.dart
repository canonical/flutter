// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:unified_analytics/unified_analytics.dart';

import '../base/file_system.dart';
import '../base/os.dart';
import '../build_info.dart';
import '../context/tool_context.dart';
import '../desktop_device.dart';
import '../device.dart';
import '../features.dart';
import '../project.dart';
import 'application_package.dart';
import 'build_linux.dart';
import 'linux_workflow.dart';

/// A device that represents a desktop Linux target.
class LinuxDevice extends DesktopDevice {
  LinuxDevice({
    required this._analytics,
    required this._featureFlags,
    required super.toolContext,
    this.linuxNext = false,
  }) : super(
         linuxNext ? 'linux-next' : 'linux',
         platformType: PlatformType.linux,
         ephemeral: false,
       );

  final Analytics _analytics;
  final FeatureFlags _featureFlags;

  /// Whether this runs the `linux_next` project with the next Linux embedder.
  final bool linuxNext;

  @override
  Future<bool> isSupported() async => true;

  @override
  bool get supportsFlavors => true;

  @override
  String get name => linuxNext ? 'Linux Next' : 'Linux';

  @override
  late final Future<TargetPlatform> targetPlatform = () async {
    final HostPlatform hostPlatform = toolContext.os.hostPlatform;
    if (hostPlatform == HostPlatform.linux_x64) {
      return TargetPlatform.linux_x64;
    } else if (hostPlatform == HostPlatform.linux_riscv64) {
      return TargetPlatform.linux_riscv64;
    }
    return TargetPlatform.linux_arm64;
  }();

  @override
  Future<CpuArch> get cpuArch async => toolContext.os.hostPlatform.cpuArch;

  @override
  bool isSupportedForProject(FlutterProject flutterProject) {
    return _linuxProject(flutterProject).existsSync();
  }

  LinuxProject _linuxProject(FlutterProject flutterProject) =>
      linuxNext ? flutterProject.linuxNext : flutterProject.linux;

  @override
  Future<void> buildForDevice({
    String? mainPath,
    required BuildInfo buildInfo,
    bool usingCISystem = false,
  }) async {
    final ToolContext(:FileSystem fs, :FlutterProjectFactory projectFactory) = toolContext;
    await buildLinux(
      _linuxProject(projectFactory.fromDirectory(fs.currentDirectory)),
      buildInfo,
      analytics: _analytics,
      featureFlags: _featureFlags,
      target: mainPath,
      targetPlatform: await targetPlatform,
      toolContext: toolContext,
    );
  }

  @override
  String executablePathForDevice(covariant LinuxApp package, BuildInfo buildInfo) {
    // The package is created from the linux project, so use the same app built
    // from the linux_next project instead.
    if (linuxNext && package is BuildableLinuxApp) {
      package = BuildableLinuxApp(
        project: package.project.parent.linuxNext,
        toolContext: toolContext,
      );
    }
    return package.executable(buildInfo.mode, buildInfo.flavor);
  }
}

class LinuxDevices extends PollingDeviceDiscovery {
  LinuxDevices({
    required this._analytics,
    required this._featureFlags,
    required ToolContext super.toolContext,
  }) : _linuxWorkflow = LinuxWorkflow(platform: toolContext.platform, featureFlags: _featureFlags),
       super('linux devices');

  final Analytics _analytics;
  final FeatureFlags _featureFlags;
  final LinuxWorkflow _linuxWorkflow;

  @override
  bool get supportsPlatform => toolContext.platform.isLinux;

  @override
  bool get canListAnything => _linuxWorkflow.canListDevices;

  @override
  Future<List<Device>> pollingGetDevices({
    Duration? timeout,
    bool forWirelessDiscovery = false,
  }) async {
    if (!canListAnything) {
      return const <Device>[];
    }
    return <Device>[
      LinuxDevice(analytics: _analytics, featureFlags: _featureFlags, toolContext: toolContext),
      LinuxDevice(
        analytics: _analytics,
        featureFlags: _featureFlags,
        toolContext: toolContext,
        linuxNext: true,
      ),
    ];
  }

  @override
  Future<List<String>> getDiagnostics() async => const <String>[];

  @override
  List<String> get wellKnownIds => const <String>['linux', 'linux-next'];
}
