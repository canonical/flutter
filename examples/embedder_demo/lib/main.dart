// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Shows an animated scene along with the resource usage of the app and the
// compositor, to compare Linux embedders.
//
// The scene is either a galaxy, which stresses rendering, or a typical app
// made of common widgets. It can be changed with the buttons, the G and A keys,
// or with --scene=galaxy|app.
//
// The workload can be changed with the buttons, the 1, 2 and 3 keys, or with
// --mode=idle|light|heavy.
//
// Q quits.

import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

import 'app_scene.dart';
import 'galaxy.dart';
import 'resource_sampler.dart';

enum Workload {
  idle('Idle', 0, 6000),
  light('Light', 1, 12000),
  heavy('Heavy', 2, 60000);

  const Workload(this.label, this.key, this.particles);

  final String label;
  final int key;
  final int particles;
}

enum Scene {
  galaxy('Galaxy'),
  app('App');

  const Scene(this.label);

  final String label;
}

const int _kHistoryLength = 60;
const Color _kBackgroundColor = Color(0xFF05040F);
const Color _kPanelColor = Color(0xD0101020);
const Color _kTextColor = Color(0xFFE8E8F0);
const Color _kDimTextColor = Color(0xFF9A9AB0);

void main(List<String> args) {
  String? option(String name, String environment) =>
      args
          .where((String a) => a.startsWith('--$name='))
          .map((String a) => a.substring(name.length + 3))
          .firstOrNull ??
      Platform.environment[environment];
  final String? mode = option('mode', 'DEMO_MODE');
  final Workload workload = Workload.values.firstWhere(
    (Workload w) => w.name == mode,
    orElse: () => Workload.light,
  );
  final String? sceneName = option('scene', 'DEMO_SCENE');
  final Scene scene = Scene.values.firstWhere(
    (Scene s) => s.name == sceneName,
    orElse: () => Scene.galaxy,
  );
  runApp(EmbedderDemo(initialScene: scene, initialWorkload: workload));
}

class EmbedderDemo extends StatefulWidget {
  const EmbedderDemo({super.key, required this.initialScene, required this.initialWorkload});

  final Scene initialScene;
  final Workload initialWorkload;

  @override
  State<EmbedderDemo> createState() => _EmbedderDemoState();
}

class _EmbedderDemoState extends State<EmbedderDemo> with SingleTickerProviderStateMixin {
  late Scene _scene = widget.initialScene;
  late Workload _workload = widget.initialWorkload;
  late final Ticker _ticker = createTicker(_onTick);
  final ValueNotifier<double> _time = ValueNotifier<double>(0);
  Duration _timeOffset = Duration.zero;
  Duration _lastElapsed = Duration.zero;

  @override
  void initState() {
    super.initState();
    _updateTicker();
  }

  void _onTick(Duration elapsed) {
    _lastElapsed = elapsed;
    _time.value = (_timeOffset + elapsed).inMicroseconds / 1e6;
  }

  void _updateTicker() {
    if (_scene != Scene.galaxy || _workload == Workload.idle) {
      if (_ticker.isActive) {
        _timeOffset += _lastElapsed;
        _lastElapsed = Duration.zero;
        _ticker.stop();
      }
    } else if (!_ticker.isActive) {
      _ticker.start();
    }
  }

  void _setWorkload(Workload workload) {
    setState(() {
      _workload = workload;
      _updateTicker();
    });
  }

  void _setScene(Scene scene) {
    setState(() {
      _scene = scene;
      _updateTicker();
    });
  }

  @override
  void dispose() {
    _ticker.dispose();
    _time.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Directionality(
      textDirection: TextDirection.ltr,
      child: DefaultTextStyle(
        style: const TextStyle(color: _kTextColor, fontSize: 14),
        child: CallbackShortcuts(
          bindings: <ShortcutActivator, VoidCallback>{
            const SingleActivator(LogicalKeyboardKey.digit1): () => _setWorkload(Workload.idle),
            const SingleActivator(LogicalKeyboardKey.digit2): () => _setWorkload(Workload.light),
            const SingleActivator(LogicalKeyboardKey.digit3): () => _setWorkload(Workload.heavy),
            const SingleActivator(LogicalKeyboardKey.keyG): () => _setScene(Scene.galaxy),
            const SingleActivator(LogicalKeyboardKey.keyA): () => _setScene(Scene.app),
            const SingleActivator(LogicalKeyboardKey.keyQ): () =>
                ServicesBinding.instance.exitApplication(ui.AppExitType.required),
          },
          child: Focus(
            autofocus: true,
            child: Stack(
              fit: StackFit.expand,
              children: <Widget>[
                if (_scene == Scene.galaxy) ...<Widget>[
                  RepaintBoundary(
                    child: CustomPaint(
                      painter: GalaxyPainter(
                        time: _time,
                        particleCount: _workload.particles,
                        maxParticles: Workload.heavy.particles,
                      ),
                    ),
                  ),
                  const Positioned(left: 24, top: 20, child: _Title()),
                ] else ...<Widget>[
                  const ColoredBox(color: _kBackgroundColor),
                  // Beside the stats panel and above the selectors.
                  Positioned(
                    left: 20,
                    top: 20,
                    right: 380,
                    bottom: 84,
                    child: ClipRRect(
                      borderRadius: BorderRadius.circular(16),
                      child: AppScene(
                        updates: _workload != Workload.idle,
                        scrolling: _workload == Workload.heavy,
                      ),
                    ),
                  ),
                ],
                const Positioned(right: 20, top: 20, child: RepaintBoundary(child: StatsPanel())),
                Positioned(
                  left: 0,
                  right: 0,
                  bottom: 24,
                  child: Center(
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: <Widget>[
                        _Selector<Scene>(
                          values: Scene.values,
                          selected: _scene,
                          onSelected: _setScene,
                          width: 100,
                          label: (Scene scene) => '${scene.label[0]}  ${scene.label}',
                          detail: (Scene scene) => scene == Scene.galaxy ? 'rendering' : 'widgets',
                        ),
                        const SizedBox(width: 12),
                        _Selector<Workload>(
                          values: Workload.values,
                          selected: _workload,
                          onSelected: _setWorkload,
                          width: 150,
                          label: (Workload workload) => '${workload.key + 1}  ${workload.label}',
                          detail: (Workload workload) => _workloadDetail(_scene, workload),
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

String _embedderName(String library) {
  if (library.startsWith('libflutter_linux_gtk')) {
    return 'GTK embedder';
  } else if (library.startsWith('libflutter_linux')) {
    return 'New Linux embedder';
  }
  return 'Unknown embedder';
}

Color _embedderColor(String library) {
  if (library.startsWith('libflutter_linux_gtk')) {
    return const Color(0xFFFF9F43);
  } else if (library.startsWith('libflutter_linux')) {
    return const Color(0xFF2EE6A8);
  }
  return _kDimTextColor;
}

final String _embedderLibrary = ResourceSampler.embedderLibrary();

class _Title extends StatelessWidget {
  const _Title();

  @override
  Widget build(BuildContext context) {
    final Color accent = _embedderColor(_embedderLibrary);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        const Text(
          'Flutter on Linux',
          style: TextStyle(fontSize: 34, fontWeight: FontWeight.w300, letterSpacing: 1),
        ),
        const SizedBox(height: 6),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 5),
          decoration: BoxDecoration(
            color: accent.withValues(alpha: 0.18),
            border: Border.all(color: accent, width: 1.5),
            borderRadius: BorderRadius.circular(20),
          ),
          child: Text(
            _embedderName(_embedderLibrary),
            style: TextStyle(color: accent, fontSize: 18, fontWeight: FontWeight.w600),
          ),
        ),
      ],
    );
  }
}

String _workloadDetail(Scene scene, Workload workload) {
  switch (scene) {
    case Scene.galaxy:
      return workload == Workload.idle ? 'paused' : '${_formatCount(workload.particles)} stars';
    case Scene.app:
      switch (workload) {
        case Workload.idle:
          return 'static';
        case Workload.light:
          return 'small updates';
        case Workload.heavy:
          return 'scrolling, pages';
      }
  }
}

class _Selector<T> extends StatelessWidget {
  const _Selector({
    required this.values,
    required this.selected,
    required this.onSelected,
    required this.width,
    required this.label,
    required this.detail,
  });

  final List<T> values;
  final T selected;
  final ValueChanged<T> onSelected;
  final double width;
  final String Function(T) label;
  final String Function(T) detail;

  @override
  Widget build(BuildContext context) {
    final Color accent = _embedderColor(_embedderLibrary);
    return Container(
      padding: const EdgeInsets.all(4),
      decoration: BoxDecoration(
        color: _kPanelColor,
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: const Color(0x30FFFFFF)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          for (final T value in values)
            MouseRegion(
              cursor: SystemMouseCursors.click,
              child: GestureDetector(
                onTap: () => onSelected(value),
                child: Container(
                  width: width,
                  padding: const EdgeInsets.symmetric(vertical: 8),
                  decoration: BoxDecoration(
                    color: value == selected ? accent.withValues(alpha: 0.25) : null,
                    borderRadius: BorderRadius.circular(20),
                  ),
                  child: Column(
                    children: <Widget>[
                      Text(
                        label(value),
                        style: TextStyle(
                          fontSize: 16,
                          fontWeight: value == selected ? FontWeight.w600 : FontWeight.normal,
                          color: value == selected ? accent : _kTextColor,
                        ),
                      ),
                      Text(
                        detail(value),
                        style: const TextStyle(fontSize: 11, color: _kDimTextColor),
                      ),
                    ],
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }
}

String _formatCount(int n) =>
    n >= 1000 ? '${n ~/ 1000},${(n % 1000).toString().padLeft(3, '0')}' : '$n';

/// Shows the resource usage of the app and the compositor, updated each second.
class StatsPanel extends StatefulWidget {
  const StatsPanel({super.key});

  @override
  State<StatsPanel> createState() => _StatsPanelState();
}

class _StatsPanelState extends State<StatsPanel> {
  final ResourceSampler _sampler = ResourceSampler();
  late final Timer _timer;
  ResourceSample? _sample;
  int? _startupMs;
  final List<double> _cpuHistory = <double>[];
  final List<double> _gpuHistory = <double>[];
  final List<double> _memoryHistory = <double>[];
  final List<double> _compositorCpuHistory = <double>[];

  // Frame statistics since the last sample.
  int _frames = 0;
  int _rasterMicroseconds = 0;
  int _fps = 0;
  double _rasterMs = 0;

  @override
  void initState() {
    super.initState();
    SchedulerBinding.instance.addTimingsCallback(_onTimings);
    _sampler.sample();
    _timer = Timer.periodic(const Duration(seconds: 1), (_) => _takeSample());
  }

  @override
  void dispose() {
    _timer.cancel();
    SchedulerBinding.instance.removeTimingsCallback(_onTimings);
    super.dispose();
  }

  void _onTimings(List<FrameTiming> timings) {
    _startupMs ??= _sampler.millisecondsSinceStart();
    for (final timing in timings) {
      _frames++;
      _rasterMicroseconds += timing.rasterDuration.inMicroseconds;
    }
  }

  void _takeSample() {
    final ResourceSample? sample = _sampler.sample();
    if (sample == null) {
      return;
    }
    void record(List<double> history, double? value) {
      history.add(value ?? 0);
      if (history.length > _kHistoryLength) {
        history.removeAt(0);
      }
    }

    setState(() {
      _sample = sample;
      record(_cpuHistory, sample.cpuPercent);
      record(_gpuHistory, sample.gpuPercent);
      record(_memoryHistory, sample.rssMiB);
      record(_compositorCpuHistory, sample.compositorCpuPercent);
      _fps = _frames;
      _rasterMs = _frames > 0 ? _rasterMicroseconds / _frames / 1000 : 0;
      _frames = 0;
      _rasterMicroseconds = 0;
    });
  }

  @override
  Widget build(BuildContext context) {
    final ResourceSample? s = _sample;
    final Color accent = _embedderColor(_embedderLibrary);
    final String compositor = _sampler.compositorName ?? 'Compositor';
    final ui.Size physicalSize = View.of(context).physicalSize;

    String percent(double? v) => v == null ? 'n/a' : '${v.toStringAsFixed(1)} %';
    String mib(double? v) => v == null ? 'n/a' : '${v.toStringAsFixed(0)} MiB';

    return Container(
      width: 340,
      padding: const EdgeInsets.fromLTRB(18, 14, 18, 14),
      decoration: BoxDecoration(
        color: _kPanelColor,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: accent.withValues(alpha: 0.6)),
      ),
      child: DefaultTextStyle.merge(
        style: const TextStyle(fontFeatures: <FontFeature>[FontFeature.tabularFigures()]),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          mainAxisSize: MainAxisSize.min,
          children: <Widget>[
            Text(_embedderLibrary, style: const TextStyle(fontSize: 12, color: _kDimTextColor)),
            const SizedBox(height: 8),
            const _SectionHeader('App'),
            _StatRow(
              label: 'Frame rate',
              value: '$_fps fps',
              detail: _fps > 0 ? 'raster ${_rasterMs.toStringAsFixed(1)} ms' : null,
            ),
            _StatRow(
              label: 'CPU',
              value: percent(s?.cpuPercent),
              history: _cpuHistory,
              color: accent,
            ),
            _StatRow(
              label: 'GPU',
              value: percent(s?.gpuPercent),
              history: s?.gpuPercent == null ? null : _gpuHistory,
              color: const Color(0xFF7FA8FF),
            ),
            _StatRow(
              label: 'Memory',
              value: mib(s?.rssMiB),
              detail: s == null ? null : 'PSS ${mib(s.pssMiB)}',
              history: _memoryHistory,
              color: const Color(0xFFFF7FBF),
              minimumMaximum: 50,
            ),
            _StatRow(label: 'GPU memory', value: mib(s?.gpuMemoryMiB)),
            _StatRow(label: 'Threads', value: s == null ? '' : '${s.threads}'),
            _StatRow(
              label: 'Startup',
              value: _startupMs == null ? '' : '$_startupMs ms',
              detail: 'to first frame',
            ),
            const SizedBox(height: 8),
            _SectionHeader(compositor),
            _StatRow(
              label: 'CPU',
              value: percent(s?.compositorCpuPercent),
              history: s?.compositorCpuPercent == null ? null : _compositorCpuHistory,
              color: const Color(0xFFFFD166),
            ),
            _StatRow(label: 'GPU', value: percent(s?.compositorGpuPercent)),
            const SizedBox(height: 8),
            Text(
              '${physicalSize.width.toInt()}×${physicalSize.height.toInt()} px · '
              'CPU 100 % = one core',
              style: const TextStyle(fontSize: 11, color: _kDimTextColor),
            ),
          ],
        ),
      ),
    );
  }
}

class _SectionHeader extends StatelessWidget {
  const _SectionHeader(this.title);

  final String title;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Text(
        title.toUpperCase(),
        style: const TextStyle(
          fontSize: 11,
          letterSpacing: 1.5,
          color: _kDimTextColor,
          fontWeight: FontWeight.w600,
        ),
      ),
    );
  }
}

class _StatRow extends StatelessWidget {
  const _StatRow({
    required this.label,
    required this.value,
    this.detail,
    this.history,
    this.color = _kTextColor,
    this.minimumMaximum = 10,
  });

  final String label;
  final String value;
  final String? detail;
  final List<double>? history;
  final Color color;
  final double minimumMaximum;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Row(
        children: <Widget>[
          SizedBox(
            width: 92,
            child: Text(label, style: const TextStyle(color: _kDimTextColor)),
          ),
          SizedBox(
            width: 84,
            child: Text(
              value,
              textAlign: TextAlign.right,
              style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: history != null
                ? SizedBox(
                    height: 22,
                    child: CustomPaint(
                      painter: _SparklinePainter(List<double>.of(history!), color, minimumMaximum),
                    ),
                  )
                : Text(detail ?? '', style: const TextStyle(fontSize: 12, color: _kDimTextColor)),
          ),
        ],
      ),
    );
  }
}

class _SparklinePainter extends CustomPainter {
  _SparklinePainter(this.values, this.color, this.minimumMaximum);

  final List<double> values;
  final Color color;
  final double minimumMaximum;

  @override
  void paint(Canvas canvas, Size size) {
    if (values.length < 2) {
      return;
    }
    double maximum = minimumMaximum;
    for (final double v in values) {
      if (v > maximum) {
        maximum = v;
      }
    }
    final double step = size.width / (_kHistoryLength - 1);
    final double start = size.width - step * (values.length - 1);
    final line = Path();
    for (var i = 0; i < values.length; i++) {
      final p = Offset(start + i * step, size.height * (1 - values[i] / (maximum * 1.1)));
      if (i == 0) {
        line.moveTo(p.dx, p.dy);
      } else {
        line.lineTo(p.dx, p.dy);
      }
    }
    final fill = Path.from(line)
      ..lineTo(size.width, size.height)
      ..lineTo(start, size.height)
      ..close();
    canvas.drawPath(fill, Paint()..color = color.withValues(alpha: 0.2));
    canvas.drawPath(
      line,
      Paint()
        ..color = color
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(_SparklinePainter oldDelegate) => true;
}
