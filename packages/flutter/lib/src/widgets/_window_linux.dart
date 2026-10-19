// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Do not import this file in production applications or packages published
// to pub.dev. Flutter will make breaking changes to this file, even in patch
// versions.
//
// All APIs in this file must be private or must:
//
// 1. Have the `@internal` attribute.
// 2. Throw an `UnsupportedError` if `isWindowingEnabled`
//    is `false`.
//
// See: https://github.com/flutter/flutter/issues/30701.

import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:ui' show Display, FlutterView;

import 'package:flutter/foundation.dart';
import 'package:flutter/rendering.dart';

import '../foundation/_features.dart';
import '_window.dart';
import '_window_positioner.dart';
import 'binding.dart';

const String _kWindowingDisabledErrorMessage = '''
Windowing APIs are not enabled.

Windowing APIs are currently experimental. Do not use windowing APIs in
production applications or plugins published to pub.dev.

To try experimental windowing APIs:
1. Switch to Flutter's main release channel.
2. Turn on the windowing feature flag.

See: https://github.com/flutter/flutter/issues/30701.
''';

// Values of WindowArchetype in the engine.
const int _kArchetypeRegular = 0;
const int _kArchetypeDialog = 1;
const int _kArchetypeTooltip = 2;
const int _kArchetypePopup = 3;
const int _kArchetypeSatellite = 4;

// Values of WindowEvent in the engine.
const int _kEventChanged = 0;
const int _kEventCloseRequested = 1;
const int _kEventDestroyed = 2;

FlutterView _flutterViewForId(int viewId) {
  final FlutterView? view = WidgetsBinding.instance.platformDispatcher.view(id: viewId);
  if (view == null) {
    throw StateError('No FlutterView with viewId $viewId was found on the platform dispatcher.');
  }
  return view;
}

int get _engineId => WidgetsBinding.instance.platformDispatcher.engineId!;

/// [WindowingOwner] implementation for Linux.
///
/// If [Platform.isLinux] is false, then the constructor will throw an
/// [UnsupportedError].
///
/// {@macro flutter.widgets.windowing.experimental}
///
/// See also:
///
///  * [WindowingOwner], the abstract class that manages native windows.
@internal
class WindowingOwnerLinux extends WindowingOwner {
  /// Creates a new [WindowingOwnerLinux] instance.
  ///
  /// If [Platform.isLinux] is false, then this constructor will throw an
  /// [UnsupportedError].
  ///
  /// {@macro flutter.widgets.windowing.experimental}
  ///
  /// See also:
  ///
  ///  * [WindowingOwner], the abstract class that manages native windows.
  @internal
  WindowingOwnerLinux() {
    if (!isWindowingEnabled) {
      throw UnsupportedError(_kWindowingDisabledErrorMessage);
    }

    if (!Platform.isLinux) {
      throw UnsupportedError('Only available on the Linux platform');
    }

    assert(
      WidgetsBinding.instance.platformDispatcher.engineId != null,
      'WindowingOwnerLinux must be created after the engine has been initialized.',
    );

    // Events are sent on the platform thread, which runs this isolate.
    _onEvent = ffi.NativeCallable<_WindowEventNative>.isolateLocal(_handleEvent);
    final ffi.Pointer<_WindowingInitRequest> request = _allocator<_WindowingInitRequest>();
    try {
      request.ref.onEvent = _onEvent.nativeFunction;
      _initialize(_engineId, request);
    } finally {
      _allocator.free(request);
    }
  }

  late final ffi.NativeCallable<_WindowEventNative> _onEvent;
  final List<_WindowLinux> _windows = <_WindowLinux>[];

  void _addWindow(_WindowLinux window) => _windows.add(window);

  void _removeWindow(_WindowLinux window) => _windows.remove(window);

  void _handleEvent(int viewId, int event) {
    // Windows may be added or removed while handling an event.
    for (final window in List<_WindowLinux>.of(_windows)) {
      window._handleEvent(viewId, event);
    }
  }

  @internal
  @override
  WindowController createWindowController({
    Size? size,
    BoxConstraints? constraints,
    required bool resizable,
    String? title,
    required WindowControllerDelegate delegate,
  }) {
    return WindowControllerLinux(
      owner: this,
      delegate: delegate,
      size: size,
      constraints: constraints,
      title: title,
      resizable: resizable,
    );
  }

  @internal
  @override
  DialogWindowController createDialogWindowController({
    required DialogWindowControllerDelegate delegate,
    Size? size,
    BoxConstraints? constraints,
    required bool resizable,
    BaseWindowController? parent,
    String? title,
  }) {
    return DialogWindowControllerLinux(
      owner: this,
      delegate: delegate,
      size: size,
      constraints: constraints,
      title: title,
      parent: parent,
      resizable: resizable,
    );
  }

  @internal
  @override
  TooltipWindowController createTooltipWindowController({
    required TooltipWindowControllerDelegate delegate,
    required BoxConstraints constraints,
    required Rect anchorRect,
    required WindowPositioner positioner,
    required BaseWindowController parent,
  }) {
    return TooltipWindowControllerLinux(
      owner: this,
      delegate: delegate,
      constraints: constraints,
      anchorRect: anchorRect,
      positioner: positioner,
      parent: parent,
    );
  }

  @internal
  @override
  PopupWindowController createPopupWindowController({
    required PopupWindowControllerDelegate delegate,
    required BoxConstraints constraints,
    required Rect anchorRect,
    required WindowPositioner positioner,
    required BaseWindowController parent,
  }) {
    return PopupWindowControllerLinux(
      owner: this,
      delegate: delegate,
      constraints: constraints,
      anchorRect: anchorRect,
      positioner: positioner,
      parent: parent,
    );
  }

  @internal
  @override
  SatelliteWindowController createSatelliteWindowController({
    required SatelliteWindowControllerDelegate delegate,
    required BaseWindowController parent,
    required WindowPositioner initialPositioner,
    Rect? initialAnchorRect,
    Size? size,
    BoxConstraints? constraints,
    required bool resizable,
    String? title,
  }) {
    return SatelliteWindowControllerLinux(
      owner: this,
      delegate: delegate,
      parent: parent,
      size: size,
      constraints: constraints,
      resizable: resizable,
      title: title,
    );
  }
}

/// Functionality shared by all window controllers on Linux.
mixin _WindowLinux {
  // Provided by [BaseWindowController].
  FlutterView get rootView;
  set rootView(FlutterView view);
  void notifyListeners();

  late final WindowingOwnerLinux _owner;
  bool _destroyed = false;

  /// Creates the native window and its view.
  void _create(
    WindowingOwnerLinux owner, {
    required int archetype,
    BaseWindowController? parent,
    Size? size,
    BoxConstraints? constraints,
    String? title,
    bool resizable = true,
    Rect? anchorRect,
    WindowPositioner? positioner,
  }) {
    if (!isWindowingEnabled) {
      throw UnsupportedError(_kWindowingDisabledErrorMessage);
    }

    _owner = owner;
    final ffi.Pointer<_WindowCreationRequest> request = _allocator<_WindowCreationRequest>();
    final ffi.Pointer<ffi.Uint8> nativeTitle = _toNativeString(title);
    try {
      request.ref
        ..archetype = archetype
        ..parentViewId = parent?.rootView.viewId ?? -1
        ..hasSize = size != null
        ..width = size?.width ?? 0
        ..height = size?.height ?? 0
        ..title = nativeTitle
        ..resizable = resizable;
      request.ref.constraints.from(constraints);
      if (anchorRect != null && positioner != null) {
        request.ref.positioner.from(anchorRect, positioner);
      }
      final int viewId = _createWindow(_engineId, request);
      if (viewId < 0) {
        throw Exception('Linux failed to create a window with a valid view id.');
      }
      rootView = _flutterViewForId(viewId);
    } finally {
      _allocator.free(nativeTitle);
      _allocator.free(request);
    }
    owner._addWindow(this);
  }

  void _ensureNotDestroyed() {
    if (_destroyed) {
      throw StateError('Window has been destroyed.');
    }
  }

  _WindowStateValues _getState() {
    _ensureNotDestroyed();
    final ffi.Pointer<_WindowState> state = _allocator<_WindowState>();
    try {
      if (!_getWindowState(_engineId, rootView.viewId, state)) {
        throw StateError('Window has been destroyed.');
      }
      return _WindowStateValues(
        contentSize: Size(state.ref.width, state.ref.height),
        activated: state.ref.activated,
        maximized: state.ref.maximized,
        fullscreen: state.ref.fullscreen,
        offset: Offset(state.ref.offsetX, state.ref.offsetY),
      );
    } finally {
      _allocator.free(state);
    }
  }

  @internal
  bool get isDestroyed => _destroyed;

  @internal
  Size get contentSize => _getState().contentSize;

  @internal
  void destroy() {
    if (_destroyed) {
      return;
    }
    // The engine sends a destroyed event for this window and its children.
    _destroyWindow(_engineId, rootView.viewId);
    if (!_destroyed) {
      _destroyed = true;
      notifyListeners();
    }
  }

  void _setSize(Size size) {
    _ensureNotDestroyed();
    // Listeners are notified when the window is resized.
    _setWindowSize(_engineId, rootView.viewId, size.width, size.height);
  }

  void _setConstraints(BoxConstraints constraints) {
    _ensureNotDestroyed();
    final ffi.Pointer<_WindowConstraints> nativeConstraints = _allocator<_WindowConstraints>();
    try {
      nativeConstraints.ref.from(constraints);
      _setWindowConstraints(_engineId, rootView.viewId, nativeConstraints);
    } finally {
      _allocator.free(nativeConstraints);
    }
    notifyListeners();
  }

  void _setTitle(String title) {
    _ensureNotDestroyed();
    final ffi.Pointer<ffi.Uint8> nativeTitle = _toNativeString(title);
    try {
      _setWindowTitle(_engineId, rootView.viewId, nativeTitle);
    } finally {
      _allocator.free(nativeTitle);
    }
    notifyListeners();
  }

  void _activate() {
    _ensureNotDestroyed();
    _activateWindow(_engineId, rootView.viewId);
  }

  void _handleEvent(int viewId, int event) {
    if (viewId != rootView.viewId) {
      return;
    }

    switch (event) {
      case _kEventChanged:
        if (!_destroyed) {
          notifyListeners();
        }
      case _kEventCloseRequested:
        _onCloseRequested();
      case _kEventDestroyed:
        final bool wasAlreadyDestroyed = _destroyed;
        _destroyed = true;
        if (!wasAlreadyDestroyed) {
          notifyListeners();
        }
        _owner._removeWindow(this);
        _onDestroyed();
    }
  }

  void _onCloseRequested() {}

  void _onDestroyed();
}

/// Closes tooltips and popups when the window they belong to loses focus.
mixin _TransientWindowLinux on _WindowLinux {
  BaseWindowController get parent;

  /// Whether this window should also close when its parent changes size.
  bool get _closeOnParentResize;

  late BaseWindowController _rootParent;
  late bool _rootParentActivated;
  late Size _parentSize;

  void _watchParent() {
    BaseWindowController root = parent;
    while (root is PopupWindowController || root is TooltipWindowController) {
      root = root is PopupWindowController ? root.parent : (root as TooltipWindowController).parent;
    }
    _rootParent = root;
    _rootParentActivated = _isActivated(root);
    _parentSize = parent.contentSize;
  }

  static bool _isActivated(BaseWindowController controller) {
    return switch (controller) {
      WindowController() => controller.isActivated,
      DialogWindowController() => controller.isActivated,
      SatelliteWindowController() => controller.isActivated,
      _ => false,
    };
  }

  @override
  void _handleEvent(int viewId, int event) {
    if (!_destroyed && event == _kEventChanged) {
      var close = false;
      if (viewId == _rootParent.rootView.viewId && !_rootParent.isDestroyed) {
        final bool activated = _isActivated(_rootParent);
        close = _rootParentActivated && !activated;
        _rootParentActivated = activated;
      }
      if (_closeOnParentResize && viewId == parent.rootView.viewId && !parent.isDestroyed) {
        final Size size = parent.contentSize;
        close = close || size != _parentSize;
        _parentSize = size;
      }
      if (close) {
        // Avoid destroying the window while the engine is sending events.
        scheduleMicrotask(destroy);
      }
    }
    super._handleEvent(viewId, event);
  }

  void _updatePosition(Rect anchorRect, WindowPositioner positioner) {
    _ensureNotDestroyed();
    final ffi.Pointer<_WindowPositionerRequest> request = _allocator<_WindowPositionerRequest>();
    try {
      request.ref.from(anchorRect, positioner);
      _updateWindowPosition(_engineId, rootView.viewId, request);
    } finally {
      _allocator.free(request);
    }
  }
}

/// Implementation of [WindowController] for the Linux platform.
///
/// {@macro flutter.widgets.windowing.experimental}
///
/// See also:
///
///  * [WindowController], the base class for regular windows.
class WindowControllerLinux extends WindowController with _WindowLinux {
  /// Creates a new regular window controller for Linux.
  ///
  /// When this constructor completes the native window has been created and
  /// has a view associated with it.
  ///
  /// {@macro flutter.widgets.windowing.experimental}
  ///
  /// See also:
  ///
  ///  * [WindowController], the base class for regular windows.
  @internal
  WindowControllerLinux({
    required WindowingOwnerLinux owner,
    required this._delegate,
    Size? size,
    BoxConstraints? constraints,
    String? title,
    required bool resizable,
  }) : _title = title ?? '',
       super.empty() {
    _create(
      owner,
      archetype: _kArchetypeRegular,
      size: size,
      constraints: constraints,
      title: title,
      resizable: resizable,
    );
  }

  final WindowControllerDelegate _delegate;
  String _title;

  @override
  @internal
  String get title {
    _ensureNotDestroyed();
    return _title;
  }

  @override
  @internal
  bool get isActivated => _getState().activated;

  @override
  @internal
  bool get isMaximized => _getState().maximized;

  // Wayland does not report when a window is minimized.
  @override
  @internal
  bool get isMinimized {
    _ensureNotDestroyed();
    return false;
  }

  @override
  @internal
  bool get isFullscreen => _getState().fullscreen;

  @override
  @internal
  void setSize(Size size) => _setSize(size);

  @override
  @internal
  void setConstraints(BoxConstraints constraints) => _setConstraints(constraints);

  @override
  @internal
  void setTitle(String title) {
    _title = title;
    _setTitle(title);
  }

  @override
  @internal
  void activate() => _activate();

  @override
  @internal
  void setMaximized(bool maximized) {
    _ensureNotDestroyed();
    _setWindowMaximized(_engineId, rootView.viewId, maximized);
  }

  // Wayland windows can be minimized, but not restored by the application.
  @override
  @internal
  void setMinimized(bool minimized) {
    _ensureNotDestroyed();
    if (minimized) {
      _setWindowMinimized(_engineId, rootView.viewId);
    } else {
      _activate();
    }
  }

  // The compositor chooses the display.
  @override
  @internal
  void setFullscreen(bool fullscreen, {Display? display}) {
    _ensureNotDestroyed();
    _setWindowFullscreen(_engineId, rootView.viewId, fullscreen);
  }

  @override
  void _onCloseRequested() => _delegate.onWindowCloseRequested(this);

  @override
  void _onDestroyed() => _delegate.onWindowDestroyed();
}

/// Implementation of [DialogWindowController] for the Linux platform.
///
/// {@macro flutter.widgets.windowing.experimental}
///
/// See also:
///
///  * [DialogWindowController], the base class for dialog windows.
class DialogWindowControllerLinux extends DialogWindowController with _WindowLinux {
  /// Creates a new dialog window controller for Linux.
  ///
  /// When this constructor completes the native window has been created and
  /// has a view associated with it.
  ///
  /// {@macro flutter.widgets.windowing.experimental}
  ///
  /// See also:
  ///
  ///  * [DialogWindowController], the base class for dialog windows.
  @internal
  DialogWindowControllerLinux({
    required WindowingOwnerLinux owner,
    required this._delegate,
    Size? size,
    BoxConstraints? constraints,
    String? title,
    BaseWindowController? parent,
    required bool resizable,
  }) : _parent = parent,
       _title = title ?? '',
       super.empty() {
    _create(
      owner,
      archetype: _kArchetypeDialog,
      parent: parent,
      size: size,
      constraints: constraints,
      title: title,
      resizable: resizable,
    );
  }

  final DialogWindowControllerDelegate _delegate;
  final BaseWindowController? _parent;
  String _title;

  @override
  @internal
  BaseWindowController? get parent => _parent;

  @override
  @internal
  String get title {
    _ensureNotDestroyed();
    return _title;
  }

  @override
  @internal
  bool get isActivated => _getState().activated;

  // Wayland does not report when a window is minimized.
  @override
  @internal
  bool get isMinimized {
    _ensureNotDestroyed();
    return false;
  }

  @override
  @internal
  void setSize(Size size) => _setSize(size);

  @override
  @internal
  void setConstraints(BoxConstraints constraints) => _setConstraints(constraints);

  @override
  @internal
  void setTitle(String title) {
    _title = title;
    _setTitle(title);
  }

  @override
  @internal
  void activate() => _activate();

  // Modal dialogs are minimized with their parent.
  @override
  @internal
  void setMinimized(bool minimized) {
    if (parent != null) {
      return;
    }

    _ensureNotDestroyed();
    if (minimized) {
      _setWindowMinimized(_engineId, rootView.viewId);
    } else {
      _activate();
    }
  }

  @override
  void _onCloseRequested() => _delegate.onWindowCloseRequested(this);

  @override
  void _onDestroyed() => _delegate.onWindowDestroyed();
}

/// Implementation of [TooltipWindowController] for the Linux platform.
///
/// {@macro flutter.widgets.windowing.experimental}
///
/// See also:
///
///  * [TooltipWindowController], the base class for tooltip windows.
class TooltipWindowControllerLinux extends TooltipWindowController
    with _WindowLinux, _TransientWindowLinux {
  /// Creates a new tooltip window controller for Linux.
  ///
  /// When this constructor completes, the native window has been created and
  /// has a view associated with it.
  ///
  /// {@macro flutter.widgets.windowing.experimental}
  ///
  /// See also:
  ///
  ///  * [TooltipWindowController], the base class for tooltip windows.
  @internal
  TooltipWindowControllerLinux({
    required WindowingOwnerLinux owner,
    required this._delegate,
    required BoxConstraints constraints,
    required BaseWindowController parent,
    required Rect anchorRect,
    required WindowPositioner positioner,
  }) : _parent = parent,
       _anchorRect = anchorRect,
       _positioner = positioner,
       super.empty() {
    _create(
      owner,
      archetype: _kArchetypeTooltip,
      parent: parent,
      constraints: constraints,
      anchorRect: anchorRect,
      positioner: positioner,
    );
    _watchParent();
  }

  final TooltipWindowControllerDelegate _delegate;
  final BaseWindowController _parent;
  Rect _anchorRect;
  WindowPositioner _positioner;

  @override
  @internal
  BaseWindowController get parent => _parent;

  @override
  bool get _closeOnParentResize => true;

  @override
  @internal
  void setConstraints(BoxConstraints constraints) => _setConstraints(constraints);

  @override
  @internal
  void updatePosition({Rect? anchorRect, WindowPositioner? positioner}) {
    _anchorRect = anchorRect ?? _anchorRect;
    _positioner = positioner ?? _positioner;
    _updatePosition(_anchorRect, _positioner);
  }

  @override
  void _onDestroyed() => _delegate.onWindowDestroyed();
}

/// Implementation of [PopupWindowController] for the Linux platform.
///
/// {@macro flutter.widgets.windowing.experimental}
///
/// See also:
///
///  * [PopupWindowController], the base class for popup windows.
class PopupWindowControllerLinux extends PopupWindowController
    with _WindowLinux, _TransientWindowLinux {
  /// Creates a new popup window controller for Linux.
  ///
  /// When this constructor completes, the native window has been created and
  /// has a view associated with it.
  ///
  /// {@macro flutter.widgets.windowing.experimental}
  ///
  /// See also:
  ///
  ///  * [PopupWindowController], the base class for popup windows.
  @internal
  PopupWindowControllerLinux({
    required WindowingOwnerLinux owner,
    required this._delegate,
    required BoxConstraints constraints,
    required BaseWindowController parent,
    required Rect anchorRect,
    required WindowPositioner positioner,
  }) : _parent = parent,
       _anchorRect = anchorRect,
       _positioner = positioner,
       super.empty() {
    _create(
      owner,
      archetype: _kArchetypePopup,
      parent: parent,
      constraints: constraints,
      anchorRect: anchorRect,
      positioner: positioner,
    );
    _watchParent();
  }

  final PopupWindowControllerDelegate _delegate;
  final BaseWindowController _parent;
  Rect _anchorRect;
  WindowPositioner _positioner;

  @override
  @internal
  BaseWindowController get parent => _parent;

  @override
  bool get _closeOnParentResize => false;

  @override
  @internal
  void setConstraints(BoxConstraints constraints) => _setConstraints(constraints);

  @override
  @internal
  void updatePosition({Rect? anchorRect, WindowPositioner? positioner}) {
    _anchorRect = anchorRect ?? _anchorRect;
    _positioner = positioner ?? _positioner;
    _updatePosition(_anchorRect, _positioner);
  }

  @override
  @internal
  Offset get offsetFromParent => _getState().offset;

  @override
  void _onDestroyed() => _delegate.onWindowDestroyed();
}

/// Implementation of [SatelliteWindowController] for the Linux platform.
///
/// Wayland does not allow applications to position top-level windows, so the
/// compositor chooses where satellite windows are placed.
///
/// {@macro flutter.widgets.windowing.experimental}
///
/// See also:
///
///  * [SatelliteWindowController], the base class for satellite windows.
class SatelliteWindowControllerLinux extends SatelliteWindowController with _WindowLinux {
  /// Creates a new satellite window controller for Linux.
  ///
  /// When this constructor completes the native window has been created and
  /// has a view associated with it.
  ///
  /// {@macro flutter.widgets.windowing.experimental}
  ///
  /// See also:
  ///
  ///  * [SatelliteWindowController], the base class for satellite windows.
  @internal
  SatelliteWindowControllerLinux({
    required WindowingOwnerLinux owner,
    required this._delegate,
    required BaseWindowController parent,
    required bool resizable,
    Size? size,
    BoxConstraints? constraints,
    String? title,
  }) : _parent = parent,
       _title = title ?? '',
       super.empty() {
    _create(
      owner,
      archetype: _kArchetypeSatellite,
      parent: parent,
      size: size,
      constraints: constraints,
      title: title,
      resizable: resizable,
    );
  }

  final SatelliteWindowControllerDelegate _delegate;
  BaseWindowController _parent;
  String _title;

  @override
  @internal
  BaseWindowController get parent => _parent;

  @override
  @internal
  String get title {
    _ensureNotDestroyed();
    return _title;
  }

  @override
  @internal
  bool get isActivated => _getState().activated;

  @override
  @internal
  void setParent(BaseWindowController parent) {
    _ensureNotDestroyed();
    assert(!identical(parent, this), 'A satellite cannot specify itself as a parent');
    if (identical(parent, _parent)) {
      return;
    }

    _parent = parent;
    _setWindowParent(_engineId, rootView.viewId, parent.rootView.viewId);
    notifyListeners();
  }

  @override
  @internal
  void setSize(Size size) => _setSize(size);

  @override
  @internal
  void setConstraints(BoxConstraints constraints) => _setConstraints(constraints);

  @override
  @internal
  void setTitle(String title) {
    _title = title;
    _setTitle(title);
  }

  @override
  @internal
  void activate() => _activate();

  @override
  void _onCloseRequested() => _delegate.onWindowCloseRequested(this);

  @override
  void _onDestroyed() => _delegate.onWindowDestroyed();
}

class _WindowStateValues {
  const _WindowStateValues({
    required this.contentSize,
    required this.activated,
    required this.maximized,
    required this.fullscreen,
    required this.offset,
  });

  final Size contentSize;
  final bool activated;
  final bool maximized;
  final bool fullscreen;
  final Offset offset;
}

ffi.Pointer<ffi.Uint8> _toNativeString(String? value) {
  if (value == null) {
    return ffi.nullptr;
  }
  final List<int> units = utf8.encode(value);
  final ffi.Pointer<ffi.Uint8> result = _allocator<ffi.Uint8>(units.length + 1);
  result.asTypedList(units.length + 1)
    ..setAll(0, units)
    ..[units.length] = 0;
  return result;
}

final _CallocAllocator _allocator = _CallocAllocator();

@ffi.Native<ffi.Pointer<ffi.Void> Function(ffi.Size, ffi.Size)>(symbol: 'calloc')
external ffi.Pointer<ffi.Void> _calloc(int count, int size);

@ffi.Native<ffi.Void Function(ffi.Pointer<ffi.Void>)>(symbol: 'free')
external void _free(ffi.Pointer<ffi.Void> pointer);

final class _CallocAllocator implements ffi.Allocator {
  /// Allocates [byteCount] bytes of zero-initialized memory on the native
  /// heap.
  @override
  ffi.Pointer<T> allocate<T extends ffi.NativeType>(int byteCount, {int? alignment}) {
    final ffi.Pointer<T> result = _calloc(1, byteCount).cast();
    if (result.address == 0) {
      throw ArgumentError('Could not allocate $byteCount bytes.');
    }
    return result;
  }

  @override
  // ignore: always_specify_types
  void free(ffi.Pointer pointer) {
    _free(pointer.cast());
  }
}

typedef _WindowEventNative = ffi.Void Function(ffi.Int64 viewId, ffi.Int32 event);

final class _WindowingInitRequest extends ffi.Struct {
  external ffi.Pointer<ffi.NativeFunction<_WindowEventNative>> onEvent;
}

final class _WindowConstraints extends ffi.Struct {
  @ffi.Bool()
  external bool hasConstraints;

  @ffi.Double()
  external double minWidth;

  @ffi.Double()
  external double minHeight;

  @ffi.Double()
  external double maxWidth;

  @ffi.Double()
  external double maxHeight;

  void from(BoxConstraints? constraints) {
    hasConstraints = constraints != null;
    minWidth = constraints?.minWidth ?? 0;
    minHeight = constraints?.minHeight ?? 0;
    maxWidth = constraints?.maxWidth ?? double.infinity;
    maxHeight = constraints?.maxHeight ?? double.infinity;
  }
}

final class _WindowPositionerRequest extends ffi.Struct {
  @ffi.Double()
  external double anchorX;

  @ffi.Double()
  external double anchorY;

  @ffi.Double()
  external double anchorWidth;

  @ffi.Double()
  external double anchorHeight;

  @ffi.Int32()
  external int parentAnchor;

  @ffi.Int32()
  external int childAnchor;

  @ffi.Double()
  external double offsetX;

  @ffi.Double()
  external double offsetY;

  @ffi.Uint32()
  external int constraintAdjustment;

  void from(Rect anchorRect, WindowPositioner positioner) {
    anchorX = anchorRect.left;
    anchorY = anchorRect.top;
    anchorWidth = anchorRect.width;
    anchorHeight = anchorRect.height;
    parentAnchor = positioner.parentAnchor.index;
    childAnchor = positioner.childAnchor.index;
    offsetX = positioner.offset.dx;
    offsetY = positioner.offset.dy;
    final WindowPositionerConstraintAdjustment adjustment = positioner.constraintAdjustment;
    constraintAdjustment =
        (adjustment.slideX ? 1 : 0) |
        (adjustment.slideY ? 2 : 0) |
        (adjustment.flipX ? 4 : 0) |
        (adjustment.flipY ? 8 : 0) |
        (adjustment.resizeX ? 16 : 0) |
        (adjustment.resizeY ? 32 : 0);
  }
}

final class _WindowCreationRequest extends ffi.Struct {
  @ffi.Int32()
  external int archetype;

  @ffi.Int64()
  external int parentViewId;

  @ffi.Bool()
  external bool hasSize;

  @ffi.Double()
  external double width;

  @ffi.Double()
  external double height;

  external _WindowConstraints constraints;

  external ffi.Pointer<ffi.Uint8> title;

  @ffi.Bool()
  external bool resizable;

  external _WindowPositionerRequest positioner;
}

final class _WindowState extends ffi.Struct {
  @ffi.Double()
  external double width;

  @ffi.Double()
  external double height;

  @ffi.Bool()
  external bool activated;

  @ffi.Bool()
  external bool maximized;

  @ffi.Bool()
  external bool fullscreen;

  @ffi.Double()
  external double offsetX;

  @ffi.Double()
  external double offsetY;
}

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Pointer<_WindowingInitRequest>)>(
  symbol: 'InternalFlutterLinux_WindowManager_Initialize',
)
external void _initialize(int engineId, ffi.Pointer<_WindowingInitRequest> request);

@ffi.Native<ffi.Int64 Function(ffi.Int64, ffi.Pointer<_WindowCreationRequest>)>(
  symbol: 'InternalFlutterLinux_WindowManager_CreateWindow',
)
external int _createWindow(int engineId, ffi.Pointer<_WindowCreationRequest> request);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64)>(
  symbol: 'InternalFlutterLinux_WindowManager_DestroyWindow',
)
external void _destroyWindow(int engineId, int viewId);

@ffi.Native<ffi.Bool Function(ffi.Int64, ffi.Int64, ffi.Pointer<_WindowState>)>(
  symbol: 'InternalFlutterLinux_WindowManager_GetWindowState',
)
external bool _getWindowState(int engineId, int viewId, ffi.Pointer<_WindowState> state);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64, ffi.Double, ffi.Double)>(
  symbol: 'InternalFlutterLinux_WindowManager_SetSize',
)
external void _setWindowSize(int engineId, int viewId, double width, double height);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64, ffi.Pointer<_WindowConstraints>)>(
  symbol: 'InternalFlutterLinux_WindowManager_SetConstraints',
)
external void _setWindowConstraints(
  int engineId,
  int viewId,
  ffi.Pointer<_WindowConstraints> constraints,
);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64, ffi.Pointer<ffi.Uint8>)>(
  symbol: 'InternalFlutterLinux_WindowManager_SetTitle',
)
external void _setWindowTitle(int engineId, int viewId, ffi.Pointer<ffi.Uint8> title);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64, ffi.Bool)>(
  symbol: 'InternalFlutterLinux_WindowManager_SetMaximized',
)
external void _setWindowMaximized(int engineId, int viewId, bool maximized);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64)>(
  symbol: 'InternalFlutterLinux_WindowManager_SetMinimized',
)
external void _setWindowMinimized(int engineId, int viewId);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64, ffi.Bool)>(
  symbol: 'InternalFlutterLinux_WindowManager_SetFullscreen',
)
external void _setWindowFullscreen(int engineId, int viewId, bool fullscreen);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64)>(
  symbol: 'InternalFlutterLinux_WindowManager_Activate',
)
external void _activateWindow(int engineId, int viewId);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64, ffi.Int64)>(
  symbol: 'InternalFlutterLinux_WindowManager_SetParent',
)
external void _setWindowParent(int engineId, int viewId, int parentViewId);

@ffi.Native<ffi.Void Function(ffi.Int64, ffi.Int64, ffi.Pointer<_WindowPositionerRequest>)>(
  symbol: 'InternalFlutterLinux_WindowManager_UpdatePosition',
)
external void _updateWindowPosition(
  int engineId,
  int viewId,
  ffi.Pointer<_WindowPositionerRequest> request,
);
