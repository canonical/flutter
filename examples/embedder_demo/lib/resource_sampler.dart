// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

// Clock ticks per second used by /proc/<pid>/stat. This is 100 on all Linux
// architectures Flutter supports.
const int _kClockTicksPerSecond = 100;

// Compositors to report usage for, in order of preference.
const List<String> _kCompositorNames = <String>[
  'gnome-shell',
  'kwin_wayland',
  'mutter',
  'sway',
  'weston',
  'Hyprland',
  'labwc',
  'wayfire',
  'cosmic-comp',
];

/// One sample of resource usage, taken about once a second.
class ResourceSample {
  const ResourceSample({
    required this.cpuPercent,
    required this.rssMiB,
    required this.pssMiB,
    required this.threads,
    required this.gpuPercent,
    required this.gpuMemoryMiB,
    required this.compositorCpuPercent,
    required this.compositorGpuPercent,
  });

  /// CPU used by this process, where 100% is one core.
  final double cpuPercent;
  final double rssMiB;
  final double pssMiB;
  final int threads;

  /// GPU engine time used by this process, or null if not reported.
  final double? gpuPercent;

  /// GPU memory resident for this process, or null if not reported.
  final double? gpuMemoryMiB;

  /// CPU used by the compositor, where 100% is one core.
  final double? compositorCpuPercent;
  final double? compositorGpuPercent;
}

/// Reads the resource usage of a process from /proc.
class _ProcessReader {
  _ProcessReader(this.pid);

  final int pid;
  List<String>? _drmFdInfoPaths;
  int _samplesSinceDrmScan = 0;

  /// Total CPU time used in seconds.
  double? cpuSeconds() {
    try {
      final String stat = File('/proc/$pid/stat').readAsStringSync();
      final List<String> fields = stat.substring(stat.lastIndexOf(')') + 2).split(' ');
      return (int.parse(fields[11]) + int.parse(fields[12])) / _kClockTicksPerSecond;
    } on Object {
      return null;
    }
  }

  /// Process start time in seconds since boot.
  double? startTime() {
    try {
      final String stat = File('/proc/$pid/stat').readAsStringSync();
      final List<String> fields = stat.substring(stat.lastIndexOf(')') + 2).split(' ');
      return int.parse(fields[19]) / _kClockTicksPerSecond;
    } on Object {
      return null;
    }
  }

  // Finds the file descriptors that are DRM clients. These are rescanned
  // occasionally as the set rarely changes and the compositor has many.
  List<String> _drmFdInfos() {
    if (_drmFdInfoPaths == null || _samplesSinceDrmScan >= 10) {
      _samplesSinceDrmScan = 0;
      final paths = <String>[];
      final clientIds = <String>{};
      try {
        for (final FileSystemEntity entry in Directory('/proc/$pid/fdinfo').listSync()) {
          try {
            final String info = File(entry.path).readAsStringSync();
            final String? clientId = _parseFdInfo(info)['drm-client-id'];
            // Several file descriptors may refer to the same client.
            if (clientId != null && clientIds.add(clientId)) {
              paths.add(entry.path);
            }
          } on Object {
            // File descriptor closed while scanning.
          }
        }
      } on Object {
        // Process not readable.
      }
      _drmFdInfoPaths = paths;
    }
    _samplesSinceDrmScan++;
    return _drmFdInfoPaths!;
  }

  /// Returns the total GPU engine time in nanoseconds and resident GPU memory
  /// in MiB, or null if the driver does not report them.
  (int?, double?) gpu() {
    int? engineNs;
    double? memoryMiB;
    for (final String path in _drmFdInfos()) {
      final Map<String, String> info;
      try {
        info = _parseFdInfo(File(path).readAsStringSync());
      } on Object {
        _drmFdInfoPaths = null;
        continue;
      }
      for (final MapEntry<String, String> entry in info.entries) {
        if (entry.key.startsWith('drm-engine-') && !entry.key.startsWith('drm-engine-capacity-')) {
          engineNs = (engineNs ?? 0) + (int.tryParse(entry.value.split(' ').first) ?? 0);
        } else if (entry.key.startsWith('drm-resident-')) {
          memoryMiB = (memoryMiB ?? 0) + _parseMiB(entry.value);
        }
      }
    }
    return (engineNs, memoryMiB);
  }

  static Map<String, String> _parseFdInfo(String info) {
    final values = <String, String>{};
    for (final String line in info.split('\n')) {
      final int colon = line.indexOf(':');
      if (colon > 0) {
        values[line.substring(0, colon)] = line.substring(colon + 1).trim();
      }
    }
    return values;
  }

  static double _parseMiB(String value) {
    final List<String> parts = value.split(' ');
    final double n = double.tryParse(parts.first) ?? 0;
    return switch (parts.length > 1 ? parts[1] : '') {
      'KiB' => n / 1024,
      'MiB' => n,
      'GiB' => n * 1024,
      _ => n / (1024 * 1024),
    };
  }
}

/// Samples the resource usage of this process and the compositor.
class ResourceSampler {
  ResourceSampler() : _self = _ProcessReader(pid) {
    _compositor = _findCompositor();
  }

  final _ProcessReader _self;
  _ProcessReader? _compositor;

  /// Name of the compositor being measured, if one was found.
  String? compositorName;

  final Stopwatch _clock = Stopwatch()..start();
  double? _lastTime;
  double? _lastCpu;
  int? _lastGpuNs;
  double? _lastCompositorCpu;
  int? _lastCompositorGpuNs;

  _ProcessReader? _findCompositor() {
    final found = <String, int>{};
    try {
      for (final FileSystemEntity entry in Directory('/proc').listSync()) {
        final int? processId = int.tryParse(entry.path.substring(6));
        if (processId == null) {
          continue;
        }
        try {
          final String name = File('/proc/$processId/comm').readAsStringSync().trim();
          if (_kCompositorNames.contains(name)) {
            found.putIfAbsent(name, () => processId);
          }
        } on Object {
          // Process exited.
        }
      }
    } on Object {
      return null;
    }
    for (final String name in _kCompositorNames) {
      if (found.containsKey(name)) {
        compositorName = name;
        return _ProcessReader(found[name]!);
      }
    }
    return null;
  }

  /// Name of the embedder library loaded into this process.
  static String embedderLibrary() {
    try {
      for (final String line in File('/proc/self/maps').readAsLinesSync()) {
        final int index = line.indexOf('libflutter_');
        if (index >= 0) {
          return line.substring(index);
        }
      }
    } on Object {
      // Fall through.
    }
    return 'unknown';
  }

  /// Time between this process starting and now, in milliseconds.
  int? millisecondsSinceStart() {
    final double? start = _self.startTime();
    if (start == null) {
      return null;
    }
    try {
      final double uptime = double.parse(File('/proc/uptime').readAsStringSync().split(' ').first);
      return ((uptime - start) * 1000).round();
    } on Object {
      return null;
    }
  }

  /// Takes a sample. Usage rates are averaged since the previous sample, so
  /// the first sample only primes the rates.
  ResourceSample? sample() {
    final double now = _clock.elapsedMicroseconds / 1e6;
    final double? cpu = _self.cpuSeconds();
    final (int? gpuNs, double? gpuMemoryMiB) = _self.gpu();
    final double? compositorCpu = _compositor?.cpuSeconds();
    final int? compositorGpuNs = _compositor?.gpu().$1;

    double rssMiB = 0;
    double pssMiB = 0;
    var threads = 0;
    try {
      for (final String line in File('/proc/self/smaps_rollup').readAsLinesSync()) {
        if (line.startsWith('Rss:')) {
          rssMiB = _kiBField(line) / 1024;
        } else if (line.startsWith('Pss:')) {
          pssMiB = _kiBField(line) / 1024;
        }
      }
      for (final String line in File('/proc/self/status').readAsLinesSync()) {
        if (line.startsWith('Threads:')) {
          threads = int.parse(line.substring(8).trim());
        }
      }
    } on Object {
      // Leave as zero.
    }

    ResourceSample? result;
    final double? lastTime = _lastTime;
    if (lastTime != null && cpu != null && _lastCpu != null) {
      final double dt = now - lastTime;
      double? rate(num? value, num? last, double scale) =>
          value != null && last != null ? 100 * (value - last) * scale / dt : null;
      result = ResourceSample(
        cpuPercent: rate(cpu, _lastCpu, 1)!,
        rssMiB: rssMiB,
        pssMiB: pssMiB,
        threads: threads,
        gpuPercent: rate(gpuNs, _lastGpuNs, 1e-9),
        gpuMemoryMiB: gpuMemoryMiB,
        compositorCpuPercent: rate(compositorCpu, _lastCompositorCpu, 1),
        compositorGpuPercent: rate(compositorGpuNs, _lastCompositorGpuNs, 1e-9),
      );
    }
    _lastTime = now;
    _lastCpu = cpu;
    _lastGpuNs = gpuNs;
    _lastCompositorCpu = compositorCpu;
    _lastCompositorGpuNs = compositorGpuNs;
    return result;
  }

  static int _kiBField(String line) => int.parse(line.split(RegExp(r'\s+'))[1]);
}
