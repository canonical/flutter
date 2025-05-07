// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ffi' hide Size;
import 'dart:ui' show FlutterView;
import 'package:ffi/ffi.dart' as ffi;
import 'package:flutter/foundation.dart';
import 'package:flutter/rendering.dart';

import 'binding.dart';
import 'window.dart';

/// Windowing owner implementation for Linux.
class WindowingOwnerLinux extends WindowingOwner {
  /// Creates a new [WindowingOwnerLinux] instance.
  WindowingOwnerLinux() {}

  @override
  RegularWindowController createRegularWindowController({
    required WindowSizing contentSize,
    required RegularWindowControllerDelegate delegate,
  }) {
    final RegularWindowControllerLinux res = RegularWindowControllerLinux(
      owner: this,
      delegate: delegate,
      contentSize: contentSize,
    );
    _activeControllers.add(res);
    return res;
  }

  @override
  bool hasTopLevelWindows() {
    return _activeControllers.isNotEmpty;
  }

  final List<WindowController> _activeControllers = <WindowController>[];
}

/// The Linux implementation of the regular window controller.
class RegularWindowControllerLinux extends RegularWindowController {
  /// Creates a new regular window controller for Linux. When this constructor
  /// completes the FlutterView is created and framework is aware of it.
  RegularWindowControllerLinux({
    required WindowingOwnerLinux owner,
    required RegularWindowControllerDelegate delegate,
    required WindowSizing contentSize,
  }) : _owner = owner,
       _delegate = delegate,
       super.empty() {
    _onDelete = NativeCallable<Void Function()>.isolateLocal(_handleOnDelete);
    final Pointer<_WindowCreationRequest> request = ffi.calloc<_WindowCreationRequest>()
      ..ref.contentSize.set(contentSize)
      ..ref.onDelete = _onDelete.nativeFunction;
    final int viewId = _createWindow(PlatformDispatcher.instance.engineId!, request);
    ffi.calloc.free(request);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);
  }

  final WindowingOwnerLinux _owner;
  final RegularWindowControllerDelegate _delegate;
  late final NativeCallable<Void Function()> _onDelete;

  @override
  Size get contentSize {
    return Size(0, 0);
  }

  @override
  WindowState get state {
    return WindowState.restored;
  }

  @override
  void setState(WindowState state) {}

  @override
  void setTitle(String title) {
    final Pointer<ffi.Utf8> titlePointer = title.toNativeUtf8();
    _setWindowTitle(PlatformDispatcher.instance.engineId!, rootView.viewId, titlePointer);
    ffi.calloc.free(titlePointer);
  }

  @override
  void setContentSize(WindowSizing size) {
    final Pointer<_Sizing> sizing = ffi.calloc<_Sizing>();
    sizing.ref.set(size);
    _setWindowContentSize(PlatformDispatcher.instance.engineId!, rootView.viewId, sizing);
    ffi.calloc.free(sizing);
  }

  @override
  void destroy() {
    _owner._activeControllers.remove(this);
    _destroyWindow(PlatformDispatcher.instance.engineId!, rootView.viewId);
  }

  void _handleOnDelete() {
    _delegate.onWindowCloseRequested(this);
  }

  @Native<Int64 Function(Int64, Pointer<_WindowCreationRequest>)>(
    symbol: 'FlutterCreateRegularWindow',
  )
  external static int _createWindow(int engineId, Pointer<_WindowCreationRequest> request);

  @Native<Void Function(Int64, Int64, Pointer<ffi.Utf8>)>(symbol: 'FlutterSetWindowTitle')
  external static void _setWindowTitle(int engineId, int viewId, Pointer<ffi.Utf8> title);

  @Native<Void Function(Int64, Int64, Pointer<_Sizing>)>(symbol: 'FlutterSetWindowContentSize')
  external static void _setWindowContentSize(int engineId, int viewId, Pointer<_Sizing> size);

  @Native<Void Function(Int64, Int64)>(symbol: 'FlutterDestroyWindow')
  external static void _destroyWindow(int engineId, int viewId);
}

final class _Sizing extends Struct {
  @Bool()
  external bool hasSize;

  @Double()
  external double width;

  @Double()
  external double height;

  @Bool()
  external bool hasConstraints;

  @Double()
  external double minWidth;

  @Double()
  external double minHeight;

  @Double()
  external double maxWidth;

  @Double()
  external double maxHeight;

  void set(WindowSizing sizing) {
    final Size? size = sizing.size;
    if (size != null) {
      hasSize = true;
      width = size.width;
      height = size.height;
    } else {
      hasSize = false;
    }

    final BoxConstraints? constraints = sizing.constraints;
    if (constraints != null) {
      hasConstraints = true;
      minWidth = constraints.minWidth;
      minHeight = constraints.minHeight;
      maxWidth = constraints.maxWidth;
      maxHeight = constraints.maxHeight;
    } else {
      hasConstraints = false;
    }
  }
}

final class _WindowCreationRequest extends Struct {
  external _Sizing contentSize;

  external Pointer<NativeFunction<Void Function()>> onDelete;
}
