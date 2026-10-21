// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

const int _kArms = 4;

// Particles are drawn in batches that share a size and colour.
class _Batch {
  _Batch(this.color, this.size, int capacity) : points = Float32List(capacity * 2);

  final Color color;
  final double size;
  final Float32List points;
  int count = 0;
}

/// A spiral galaxy of particles, animated by [time] in seconds.
class GalaxyPainter extends CustomPainter {
  GalaxyPainter({required this.time, required this.particleCount, required int maxParticles})
    : super(repaint: time) {
    _ensureParticles(maxParticles);
  }

  final ValueListenable<double> time;
  final int particleCount;

  // Particle parameters are shared between painters so they are only
  // generated once.
  static Float32List _radius = Float32List(0);
  static Float32List _angle = Float32List(0);
  static Float32List _wobble = Float32List(0);
  static Uint8List _batchIndex = Uint8List(0);
  static List<_Batch> _batches = <_Batch>[];
  static Float32List _stars = Float32List(0);

  static void _ensureParticles(int count) {
    if (_radius.length >= count) {
      return;
    }
    final random = math.Random(42);
    _radius = Float32List(count);
    _angle = Float32List(count);
    _wobble = Float32List(count);
    _batchIndex = Uint8List(count);

    const palette = <Color>[
      Color(0xFFFFF4D6), // Core, warm white.
      Color(0xFFFFB86B), // Inner, amber.
      Color(0xFFFF5FA2), // Middle, pink.
      Color(0xFFB06BFF), // Outer, violet.
      Color(0xFF4FC3FF), // Edge, blue.
    ];
    const sizes = <double>[1.2, 2.2];
    _batches = <_Batch>[
      for (final Color color in palette)
        for (final double size in sizes) _Batch(color, size, count),
    ];

    for (var i = 0; i < count; i++) {
      // More particles near the centre.
      final double r = math.pow(random.nextDouble(), 1.6).toDouble();
      final int arm = random.nextInt(_kArms);
      final double spread =
          (random.nextDouble() - 0.5) * (0.35 + 0.9 * (1 - r)) * random.nextDouble();
      _radius[i] = r;
      _angle[i] = arm * 2 * math.pi / _kArms + r * 5.0 + spread * 2.2;
      _wobble[i] = random.nextDouble() * 2 * math.pi;
      final int band = (r * palette.length + (random.nextDouble() - 0.5) * 0.8).floor().clamp(
        0,
        palette.length - 1,
      );
      final size = random.nextDouble() < 0.85 ? 0 : 1;
      _batchIndex[i] = band * sizes.length + size;
    }

    _stars = Float32List(600 * 2);
    for (var i = 0; i < _stars.length; i++) {
      _stars[i] = random.nextDouble();
    }
  }

  @override
  void paint(Canvas canvas, Size size) {
    final double t = time.value;
    final center = Offset(size.width / 2, size.height * 0.54);
    final double scale = math.min(size.width * 0.44, size.height * 0.58);

    // Background, with slowly drifting nebulae.
    canvas.drawRect(Offset.zero & size, Paint()..color = const Color(0xFF05040F));
    _drawNebula(
      canvas,
      center + Offset(math.cos(t * 0.05) * scale * 0.6, math.sin(t * 0.07) * scale * 0.3),
      scale * 1.3,
      const Color(0x553A1C8C),
    );
    _drawNebula(
      canvas,
      center + Offset(math.cos(t * 0.04 + 2) * scale * 0.7, math.sin(t * 0.06 + 1) * scale * 0.4),
      scale * 1.1,
      const Color(0x40A0306A),
    );

    // Distant stars.
    final stars = Float32List(_stars.length);
    for (var i = 0; i < _stars.length; i += 2) {
      stars[i] = _stars[i] * size.width;
      stars[i + 1] = _stars[i + 1] * size.height;
    }
    canvas.drawRawPoints(
      ui.PointMode.points,
      stars,
      Paint()
        ..color = const Color(0x99FFFFFF)
        ..strokeWidth = 1.2
        ..strokeCap = StrokeCap.round,
    );

    // Galaxy, tilted and rotating as a whole.
    final double rotation = t * 0.12;
    const tilt = 0.55;
    const cosTilt = 0.906; // Rotation of the galaxy on screen.
    const sinTilt = -0.423;
    for (final _Batch batch in _batches) {
      batch.count = 0;
    }
    for (var i = 0; i < particleCount; i++) {
      final double r = _radius[i];
      final double wobble = _wobble[i];
      final double a = _angle[i] + rotation * (1.6 - r) + 0.05 * math.sin(t * 1.3 + wobble);
      final double rr = r * scale * (1 + 0.03 * math.sin(t * 0.9 + wobble * 3));
      final double x = rr * math.cos(a);
      final double y = rr * math.sin(a) * tilt;
      final _Batch batch = _batches[_batchIndex[i]];
      final int j = batch.count * 2;
      batch.points[j] = center.dx + x * cosTilt - y * sinTilt;
      batch.points[j + 1] = center.dy + x * sinTilt + y * cosTilt;
      batch.count++;
    }

    final particlePaint = Paint()
      ..blendMode = BlendMode.plus
      ..strokeCap = StrokeCap.round;
    for (final _Batch batch in _batches) {
      if (batch.count == 0) {
        continue;
      }
      particlePaint
        ..color = batch.color.withValues(alpha: 0.75)
        ..strokeWidth = batch.size;
      canvas.drawRawPoints(
        ui.PointMode.points,
        Float32List.sublistView(batch.points, 0, batch.count * 2),
        particlePaint,
      );
    }

    // Glowing core, pulsing gently.
    final double pulse = 1 + 0.08 * math.sin(t * 2);
    _drawNebula(canvas, center, scale * 0.28 * pulse, const Color(0xCCFFE2B0));
    _drawNebula(canvas, center, scale * 0.08 * pulse, const Color(0xFFFFFFFF));
  }

  void _drawNebula(Canvas canvas, Offset center, double radius, Color color) {
    canvas.drawCircle(
      center,
      radius,
      Paint()
        ..blendMode = BlendMode.plus
        ..shader = ui.Gradient.radial(center, radius, <Color>[color, color.withValues(alpha: 0)]),
    );
  }

  @override
  bool shouldRepaint(GalaxyPainter oldDelegate) =>
      oldDelegate.particleCount != particleCount || oldDelegate.time != time;
}
