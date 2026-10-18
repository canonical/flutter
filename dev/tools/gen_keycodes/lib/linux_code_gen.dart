// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:path/path.dart' as path;

import 'base_code_gen.dart';
import 'constants.dart';
import 'logical_key_data.dart';
import 'physical_key_data.dart';
import 'utils.dart';

/// Generates the key mapping for the Linux embedder, based on the information
/// in the key data structure given to it.
class LinuxCodeGenerator extends PlatformCodeGenerator {
  LinuxCodeGenerator(super.keyData, super.logicalData, this._layoutGoals);

  /// This generates the map of XKB scan codes to Flutter physical keys.
  String get _xkbScanCodeMap {
    final lines = OutputLines<int>('Linux XKB keycode map');
    for (final PhysicalKeyEntry entry in keyData.entries) {
      if (entry.xKbScanCode != null) {
        lines.add(
          entry.xKbScanCode!,
          '    {${toHex(entry.xKbScanCode)}, ${toHex(entry.usbHidCode)}},  // ${entry.constantName}',
        );
      }
    }
    return lines.sortedJoin().trimRight();
  }

  /// This generates the map of XKB keysyms to Flutter logical keys.
  ///
  /// GTK keyvals have the same values as XKB keysyms.
  String get _keysymCodeMap {
    final lines = OutputLines<int>('Linux keysym map');
    for (final LogicalKeyEntry entry in logicalData.entries) {
      zipStrict(entry.gtkValues, entry.gtkNames, (int value, String name) {
        lines.add(value, '    {${toHex(value)}, ${toHex(entry.value, digits: 11)}},  // $name');
      });
    }
    return lines.sortedJoin().trimRight();
  }

  final Map<String, bool> _layoutGoals;
  String get _layoutGoalsString {
    final lines = OutputLines<int>('Linux layout goals');
    _layoutGoals.forEach((String name, bool mandatory) {
      final PhysicalKeyEntry physicalEntry = keyData.entryByName(name);
      final LogicalKeyEntry logicalEntry = logicalData.entryByName(name);
      final line =
          '{'
          '${toHex(physicalEntry.xKbScanCode, digits: 2)}, '
          '${toHex(logicalEntry.value, digits: 2)}, '
          '${mandatory ? 'true' : 'false'}'
          '},';
      lines.add(
        logicalEntry.value,
        '    ${line.padRight(29)}'
        '// ${logicalEntry.name}',
      );
    });
    return lines.sortedJoin().trimRight();
  }

  /// This generates the mask values for the part of a key code that defines its plane.
  String get _maskConstants {
    return <String>[
      'const uint64_t kValueMask = ${toHex(kValueMask.value, digits: 11)};',
      'const uint64_t kUnicodePlane = ${toHex(kUnicodePlane.value, digits: 11)};',
      // Keys without a Flutter logical key use the plane reserved for GTK,
      // since their values are keysyms, which GTK also uses.
      'const uint64_t kLinuxPlane = ${toHex(kGtkPlane.value, digits: 11)};',
    ].join('\n');
  }

  @override
  String get templatePath => path.join(dataRoot, 'linux_key_mapping_cc.tmpl');

  @override
  String outputPath(String platform) => path.join(
    PlatformCodeGenerator.engineRoot,
    'shell',
    'platform',
    'linux',
    'key_mapping.g.cc',
  );

  @override
  Map<String, String> mappings() {
    return <String, String>{
      'XKB_SCAN_CODE_MAP': _xkbScanCodeMap,
      'KEYSYM_CODE_MAP': _keysymCodeMap,
      'MASK_CONSTANTS': _maskConstants,
      'LAYOUT_GOALS': _layoutGoalsString,
    };
  }
}
