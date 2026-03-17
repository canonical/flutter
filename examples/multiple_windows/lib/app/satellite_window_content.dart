// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// A satellite window that displays a toolkit panel with drawing tools,
/// color swatches, sliders, and toggles. None of the controls are backed
/// by real data. They are purely for demonstration purposes.
class SatelliteWindowContent extends StatefulWidget {
  const SatelliteWindowContent({super.key});

  @override
  State<SatelliteWindowContent> createState() => _SatelliteWindowContentState();
}

class _SatelliteWindowContentState extends State<SatelliteWindowContent> {
  int _selectedToolIndex = 0;
  int _selectedColorIndex = 2;
  double _brushSize = 12.0;
  double _opacity = 0.85;
  bool _antiAlias = true;
  bool _pressureSensitivity = false;

  static const List<_ToolInfo> _tools = [
    _ToolInfo(Icons.edit, 'Pencil'),
    _ToolInfo(Icons.brush, 'Brush'),
    _ToolInfo(Icons.format_paint, 'Fill'),
    _ToolInfo(Icons.auto_fix_high, 'Eraser'),
    _ToolInfo(Icons.crop_square, 'Rectangle'),
    _ToolInfo(Icons.circle_outlined, 'Ellipse'),
    _ToolInfo(Icons.timeline, 'Line'),
    _ToolInfo(Icons.colorize, 'Eyedropper'),
  ];

  static const List<Color> _palette = [
    Color(0xFF000000),
    Color(0xFFFFFFFF),
    Color(0xFFE53935),
    Color(0xFFFB8C00),
    Color(0xFFFDD835),
    Color(0xFF43A047),
    Color(0xFF1E88E5),
    Color(0xFF8E24AA),
    Color(0xFF6D4C41),
    Color(0xFF546E7A),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFFF5F5F5),
      body: Padding(
        padding: const EdgeInsets.all(12.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Header
            const Text(
              'Tools',
              style: TextStyle(
                color: Color(0xFF333333),
                fontSize: 13,
                fontWeight: FontWeight.w600,
                letterSpacing: 0.5,
              ),
            ),
            const SizedBox(height: 8),

            // Tool grid
            _buildToolGrid(),
            const SizedBox(height: 16),

            const _SectionDivider(),

            // Color palette
            const Text(
              'Color',
              style: TextStyle(
                color: Color(0xFF333333),
                fontSize: 13,
                fontWeight: FontWeight.w600,
                letterSpacing: 0.5,
              ),
            ),
            const SizedBox(height: 8),
            _buildColorPalette(),
            const SizedBox(height: 16),

            const _SectionDivider(),

            // Brush size slider
            _buildSliderRow(
              label: 'Size',
              value: _brushSize,
              min: 1,
              max: 64,
              displayValue: '${_brushSize.round()} px',
              onChanged: (v) => setState(() => _brushSize = v),
            ),
            const SizedBox(height: 8),

            // Opacity slider
            _buildSliderRow(
              label: 'Opacity',
              value: _opacity,
              min: 0,
              max: 1,
              displayValue: '${(_opacity * 100).round()}%',
              onChanged: (v) => setState(() => _opacity = v),
            ),
            const SizedBox(height: 12),

            const _SectionDivider(),

            // Toggles
            _buildToggleRow(
              label: 'Anti-alias',
              value: _antiAlias,
              onChanged: (v) => setState(() => _antiAlias = v ?? false),
            ),
            _buildToggleRow(
              label: 'Pressure sensitivity',
              value: _pressureSensitivity,
              onChanged: (v) =>
                  setState(() => _pressureSensitivity = v ?? false),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildToolGrid() {
    return Wrap(
      spacing: 4,
      runSpacing: 4,
      children: List.generate(_tools.length, (index) {
        final tool = _tools[index];
        final isSelected = index == _selectedToolIndex;
        return Tooltip(
          message: tool.name,
          child: GestureDetector(
            onTap: () => setState(() => _selectedToolIndex = index),
            child: Container(
              width: 36,
              height: 36,
              decoration: BoxDecoration(
                color: isSelected
                    ? const Color(0xFF5090D0)
                    : const Color(0xFFE8E8E8),
                borderRadius: BorderRadius.circular(6),
                border: Border.all(
                  color: isSelected
                      ? const Color(0xFF3A70B0)
                      : const Color(0xFFCCCCCC),
                ),
              ),
              child: Icon(
                tool.icon,
                color: isSelected
                    ? const Color(0xFFFFFFFF)
                    : const Color(0xFF555555),
                size: 18,
              ),
            ),
          ),
        );
      }),
    );
  }

  Widget _buildColorPalette() {
    return Wrap(
      spacing: 6,
      runSpacing: 6,
      children: List.generate(_palette.length, (index) {
        final color = _palette[index];
        final isSelected = index == _selectedColorIndex;
        return GestureDetector(
          onTap: () => setState(() => _selectedColorIndex = index),
          child: Container(
            width: 24,
            height: 24,
            decoration: BoxDecoration(
              color: color,
              borderRadius: BorderRadius.circular(4),
              border: Border.all(
                color: isSelected
                    ? const Color(0xFF333333)
                    : const Color(0xFFCCCCCC),
                width: isSelected ? 2 : 1,
              ),
            ),
          ),
        );
      }),
    );
  }

  Widget _buildSliderRow({
    required String label,
    required double value,
    required double min,
    required double max,
    required String displayValue,
    required ValueChanged<double> onChanged,
  }) {
    return Row(
      children: [
        SizedBox(
          width: 52,
          child: Text(
            label,
            style: const TextStyle(color: Color(0xFF666666), fontSize: 12),
          ),
        ),
        Expanded(
          child: SliderTheme(
            data: SliderThemeData(
              activeTrackColor: const Color(0xFF5090D0),
              inactiveTrackColor: const Color(0xFFCCCCCC),
              thumbColor: const Color(0xFF5090D0),
              overlayColor: const Color(0x225090D0),
              trackHeight: 3,
              thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 6),
            ),
            child: Slider(
              value: value,
              min: min,
              max: max,
              onChanged: onChanged,
            ),
          ),
        ),
        SizedBox(
          width: 44,
          child: Text(
            displayValue,
            textAlign: TextAlign.right,
            style: const TextStyle(color: Color(0xFF777777), fontSize: 11),
          ),
        ),
      ],
    );
  }

  Widget _buildToggleRow({
    required String label,
    required bool value,
    required ValueChanged<bool?> onChanged,
  }) {
    return Row(
      children: [
        SizedBox(
          width: 20,
          height: 28,
          child: Checkbox(
            value: value,
            onChanged: onChanged,
            activeColor: const Color(0xFF5090D0),
            checkColor: const Color(0xFFFFFFFF),
            side: const BorderSide(color: Color(0xFFAAAAAA)),
            materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
          ),
        ),
        const SizedBox(width: 8),
        Text(
          label,
          style: const TextStyle(color: Color(0xFF666666), fontSize: 12),
        ),
      ],
    );
  }
}

class _ToolInfo {
  const _ToolInfo(this.icon, this.name);
  final IconData icon;
  final String name;
}

class _SectionDivider extends StatelessWidget {
  const _SectionDivider();

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Divider(color: const Color(0xFFDDDDDD), height: 1),
    );
  }
}
