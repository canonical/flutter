// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

class SettingsValueNotifier<T> extends ChangeNotifier {
  SettingsValueNotifier({required T value}) : _value = value;

  T _value;
  T get value => _value;
  set value(T v) {
    _value = v;
    notifyListeners();
  }
}

class WindowSettings {
  WindowSettings({
    Size regularSize = const Size(400, 300),
    Size dialogSize = const Size(300, 250),
  })  : _regularSize = SettingsValueNotifier(value: regularSize),
        _dialogSize = SettingsValueNotifier(value: dialogSize);

  final SettingsValueNotifier<Size> _regularSize;
  SettingsValueNotifier<Size> get regularSizeNotifier => _regularSize;
  set regularSize(Size value) {
    _regularSize.value = value;
  }

  final SettingsValueNotifier<Size> _dialogSize;
  SettingsValueNotifier<Size> get dialogSizeNotifier => _dialogSize;
  set dialogSize(Size value) {
    _dialogSize.value = value;
  }
}
