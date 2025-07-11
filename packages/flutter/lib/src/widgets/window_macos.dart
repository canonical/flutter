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
import 'window_positioner.dart';

/// The macOS implementation of the windowing API.
class WindowingOwnerMacOS extends WindowingOwner {
  @override
  RegularWindowController createRegularWindowController({
    required WindowSizing contentSize,
    required RegularWindowControllerDelegate delegate,
  }) {
    final RegularWindowControllerMacOS res = RegularWindowControllerMacOS(
      owner: this,
      delegate: delegate,
      contentSize: contentSize,
    );
    _activeControllers.add(res);
    return res;
  }

  @override
  DialogWindowController createDialogWindowController({
    required WindowSizing contentSize,
    required DialogWindowControllerDelegate delegate,
    FlutterView? parent,
  }) {
    final DialogWindowControllerMacOS res = DialogWindowControllerMacOS(
      owner: this,
      delegate: delegate,
      contentSize: contentSize,
      parent: parent,
    );
    _activeControllers.add(res);
    return res;
  }

  @override
  TooltipWindowController createTooltipWindowController({
    required FlutterView parent,
    required BoxConstraints contentSizeConstraints,
    required TooltipWindowControllerDelegate delegate,
    required Rect anchorRect,
    required WindowPositioner positioner,
  }) {
    final TooltipWindowControllerMacOS res = TooltipWindowControllerMacOS(
      owner: this,
      delegate: delegate,
      contentSizeConstraints: contentSizeConstraints,
      parent: parent,
      positioner: positioner,
      anchorRect: anchorRect,
    );
    return res;
  }

  @override
  bool hasTopLevelWindows() {
    return _activeControllers.isNotEmpty;
  }

  final List<WindowController> _activeControllers = <WindowController>[];

  /// Returns the window handle for the given [view], or null is the window
  /// handle is not available.
  /// The window handle is a pointer to NSWindow instance.
  static Pointer<Void> getWindowHandle(FlutterView view) {
    return _getWindowHandle(PlatformDispatcher.instance.engineId!, view.viewId);
  }

  @Native<Pointer<Void> Function(Int64, Int64)>(symbol: 'InternalFlutter_Window_GetHandle')
  external static Pointer<Void> _getWindowHandle(int engineId, int viewId);
}

mixin _WindowControllerMixin on WindowController {
  void _initController(WindowingOwnerMacOS owner) {
    _onShouldClose = NativeCallable<Void Function()>.isolateLocal(_handleOnShouldClose);
    _onWillClose = NativeCallable<Void Function()>.isolateLocal(_handleOnWillClose);
    _onResize = NativeCallable<Void Function()>.isolateLocal(_handleOnResize);
    _onGetWindowPosition = NativeCallable<
      Pointer<_Rect> Function(
        Pointer<_Size> childSize,
        Pointer<_Rect> parentRect,
        Pointer<_Rect> outputRect,
      )
    >.isolateLocal(_handleOnGetWindowPosition);
    _owner = owner;
    _owner._activeControllers.add(this);
  }

  void _handleOnShouldClose();

  void _handleOnResize();

  @mustCallSuper
  void _handleOnWillClose() {
    _onWillClose.close();
    _onShouldClose.close();
    _onResize.close();
    _onGetWindowPosition.close();
    _destroyed = true;
    _owner._activeControllers.remove(this);
  }

  @mustCallSuper
  Pointer<_Rect> _handleOnGetWindowPosition(
    Pointer<_Size> childSize,
    Pointer<_Rect> parentRect,
    Pointer<_Rect> outputRect,
  ) {
    return Pointer<_Rect>.fromAddress(0);
  }

  void _ensureNotDestroyed() {
    if (_destroyed) {
      throw StateError('Window has been destroyed.');
    }
  }

  /// Returns window handle for the current window.
  /// The handle is a pointer to NSWindow instance.
  Pointer<Void> getWindowHandle() {
    _ensureNotDestroyed();
    return WindowingOwnerMacOS.getWindowHandle(rootView);
  }

  @override
  Size get contentSize {
    _ensureNotDestroyed();
    final _Size size = _getWindowContentSize(getWindowHandle());
    return Size(size.width, size.height);
  }

  @override
  void destroy() {
    if (_destroyed) {
      return;
    }
    final Pointer<Void> handle = getWindowHandle();
    _destroyWindow(PlatformDispatcher.instance.engineId!, handle);
  }

  @override
  bool get destroyed => _destroyed;

  bool _destroyed = false;

  late final NativeCallable<Void Function()> _onShouldClose;
  late final NativeCallable<Void Function()> _onWillClose;
  late final NativeCallable<Void Function()> _onResize;
  late final NativeCallable<
    Pointer<_Rect> Function(
      Pointer<_Size> childSize,
      Pointer<_Rect> parentRect,
      Pointer<_Rect> outputRect,
    )
  >
  _onGetWindowPosition;

  late final WindowingOwnerMacOS _owner;
}

class TooltipWindowControllerMacOS extends TooltipWindowController with _WindowControllerMixin {
  TooltipWindowControllerMacOS({
    required WindowingOwnerMacOS owner,
    required TooltipWindowControllerDelegate delegate,
    required BoxConstraints contentSizeConstraints,
    required FlutterView parent,
    required this.anchorRect,
    required this.positioner,
  }) : _delegate = delegate,
       super.empty() {
    _initController(owner);

    final Pointer<_WindowCreationRequest> request =
        ffi.calloc<_WindowCreationRequest>()
          ..ref.contentSize.set(WindowSizing(constraints: contentSizeConstraints))
          ..ref.onShouldClose = _onShouldClose.nativeFunction
          ..ref.onWillClose = _onWillClose.nativeFunction
          ..ref.onSizeChange = _onResize.nativeFunction
          ..ref.onGetWindowPosition = _onGetWindowPosition.nativeFunction
          ..ref.parentViewId = parent.viewId;

    final int viewId = _createTooltipWindow(PlatformDispatcher.instance.engineId!, request);
    ffi.calloc.free(request);

    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);
  }

  @override
  void _handleOnShouldClose() {
    _delegate.onWindowCloseRequested(this);
  }

  @override
  void _handleOnWillClose() {
    super._handleOnWillClose();
    _delegate.onWindowDestroyed();
  }

  @override
  void _handleOnResize() {
    notifyListeners();
  }

  final WindowPositioner positioner;
  final Rect anchorRect;

  @override
  Pointer<_Rect> _handleOnGetWindowPosition(
    Pointer<_Size> childSize,
    Pointer<_Rect> parentRect,
    Pointer<_Rect> outputRect,
  ) {
    super._handleOnGetWindowPosition(childSize, parentRect, outputRect);
    final Pointer<_Rect> result = ffi.calloc<_Rect>();
    final Rect targetRect = positioner.placeWindow(
      childSize: childSize.ref.toSize(),
      anchorRect: anchorRect.translate(parentRect.ref.left, parentRect.ref.top),
      parentRect: parentRect.ref.toRect(),
      outputRect: outputRect.ref.toRect(),
    );
    result.ref.left = targetRect.left;
    result.ref.top = targetRect.top;
    result.ref.width = childSize.ref.width;
    result.ref.height = childSize.ref.height;
    return result;
  }

  final TooltipWindowControllerDelegate _delegate;
}

/// The macOS implementation of the regular window controller.
class RegularWindowControllerMacOS extends RegularWindowController with _WindowControllerMixin {
  /// Creates a new regular window controller for macOS. When this constructor
  /// completes the FlutterView is created and framework is aware of it.
  RegularWindowControllerMacOS({
    required WindowingOwnerMacOS owner,
    required RegularWindowControllerDelegate delegate,
    required WindowSizing contentSize,
  }) : _delegate = delegate,
       super.empty() {
    _initController(owner);

    final Pointer<_WindowCreationRequest> request =
        ffi.calloc<_WindowCreationRequest>()
          ..ref.contentSize.set(contentSize)
          ..ref.onShouldClose = _onShouldClose.nativeFunction
          ..ref.onWillClose = _onWillClose.nativeFunction
          ..ref.onSizeChange = _onResize.nativeFunction;

    final int viewId = _createRegularWindow(PlatformDispatcher.instance.engineId!, request);
    ffi.calloc.free(request);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);
  }

  @override
  void _handleOnShouldClose() {
    _delegate.onWindowCloseRequested(this);
  }

  @override
  void _handleOnWillClose() {
    super._handleOnWillClose();
    _delegate.onWindowDestroyed();
  }

  @override
  void _handleOnResize() {
    notifyListeners();
  }

  @override
  void updateContentSize(WindowSizing sizing) {
    _ensureNotDestroyed();
    final Pointer<_Sizing> ffiSizing = ffi.calloc<_Sizing>();
    ffiSizing.ref.set(sizing);
    _setWindowContentSize(getWindowHandle(), ffiSizing);
    ffi.calloc.free(ffiSizing);
  }

  @override
  void setTitle(String title) {
    _ensureNotDestroyed();
    final Pointer<ffi.Utf8> titlePointer = title.toNativeUtf8();
    _setWindowTitle(getWindowHandle(), titlePointer);
    ffi.calloc.free(titlePointer);
  }

  final RegularWindowControllerDelegate _delegate;

  @override
  void activate() {
    _ensureNotDestroyed();
    _activate(getWindowHandle());
  }

  @override
  void setMaximized(bool maximized) {
    _ensureNotDestroyed();
    _setMaximized(getWindowHandle(), maximized);
  }

  @override
  bool isMaximized() {
    _ensureNotDestroyed();
    return _isMaximized(getWindowHandle());
  }

  @override
  void setMinimized(bool minimized) {
    _ensureNotDestroyed();
    if (minimized) {
      _minimize(getWindowHandle());
    } else {
      _unminimize(getWindowHandle());
    }
  }

  @override
  bool isMinimized() {
    _ensureNotDestroyed();
    return _isMinimized(getWindowHandle());
  }

  @override
  void setFullscreen(bool fullscreen, {int? displayId}) {
    _ensureNotDestroyed();
    _setFullscreen(getWindowHandle(), fullscreen);
  }

  @override
  bool isFullscreen() {
    _ensureNotDestroyed();
    return _isFullscreen(getWindowHandle());
  }
}

/// The macOS implementation of the regular window controller.
class DialogWindowControllerMacOS extends DialogWindowController with _WindowControllerMixin {
  /// Creates a new regular window controller for macOS. When this constructor
  /// completes the FlutterView is created and framework is aware of it.
  DialogWindowControllerMacOS({
    required WindowingOwnerMacOS owner,
    required WindowSizing contentSize,
    this.parent,
    required DialogWindowControllerDelegate delegate,
  }) : _delegate = delegate,
       super.empty() {
    _initController(owner);

    final Pointer<_WindowCreationRequest> request =
        ffi.calloc<_WindowCreationRequest>()
          ..ref.contentSize.set(contentSize)
          ..ref.parentViewId = parent?.viewId ?? 0
          ..ref.onShouldClose = _onShouldClose.nativeFunction
          ..ref.onWillClose = _onWillClose.nativeFunction
          ..ref.onSizeChange = _onResize.nativeFunction;

    final int viewId = _createDialogWindow(PlatformDispatcher.instance.engineId!, request);
    ffi.calloc.free(request);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);
  }

  @override
  void _handleOnShouldClose() {
    _delegate.onWindowCloseRequested(this);
  }

  @override
  void _handleOnWillClose() {
    super._handleOnWillClose();
    _delegate.onWindowDestroyed();
  }

  @override
  void _handleOnResize() {
    notifyListeners();
  }

  @override
  void updateContentSize(WindowSizing sizing) {
    _ensureNotDestroyed();
    final Pointer<_Sizing> ffiSizing = ffi.calloc<_Sizing>();
    ffiSizing.ref.set(sizing);
    _setWindowContentSize(getWindowHandle(), ffiSizing);
    ffi.calloc.free(ffiSizing);
  }

  @override
  void setTitle(String title) {
    _ensureNotDestroyed();
    final Pointer<ffi.Utf8> titlePointer = title.toNativeUtf8();
    _setWindowTitle(getWindowHandle(), titlePointer);
    ffi.calloc.free(titlePointer);
  }

  @override
  final FlutterView? parent;
  final DialogWindowControllerDelegate _delegate;

  @override
  Size get contentSize {
    _ensureNotDestroyed();
    final _Size size = _getWindowContentSize(getWindowHandle());
    return Size(size.width, size.height);
  }

  @override
  void setMinimized(bool minimized) {
    _ensureNotDestroyed();
    if (minimized) {
      _minimize(getWindowHandle());
    } else {
      _unminimize(getWindowHandle());
    }
  }

  @override
  bool isMinimized() {
    _ensureNotDestroyed();
    return _isMinimized(getWindowHandle());
  }
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
    final Size? size = sizing.preferredSize;
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

  @Int64()
  external int parentViewId;

  external Pointer<NativeFunction<Void Function()>> onShouldClose;
  external Pointer<NativeFunction<Void Function()>> onWillClose;
  external Pointer<NativeFunction<Void Function()>> onSizeChange;
  external Pointer<
    NativeFunction<
      Pointer<_Rect> Function(
        Pointer<_Size> childSize,
        Pointer<_Rect> parentRect,
        Pointer<_Rect> outputRect,
      )
    >
  >
  onGetWindowPosition;
}

final class _Size extends Struct {
  @Double()
  external double width;

  @Double()
  external double height;

  @override
  String toString() {
    return 'Size(width: $width, height: $height)';
  }

  Size toSize() {
    return Size(width, height);
  }
}

final class _Rect extends Struct {
  @Double()
  external double left;

  @Double()
  external double top;

  @Double()
  external double width;

  @Double()
  external double height;

  Rect toRect() {
    return Rect.fromLTWH(left, top, width, height);
  }

  @override
  String toString() {
    return 'Rect(left: $left, top: $top, width: $width, height: $height)';
  }
}

@Native<Int64 Function(Int64, Pointer<_WindowCreationRequest>)>(
  symbol: 'InternalFlutter_WindowController_CreateRegularWindow',
)
external int _createRegularWindow(int engineId, Pointer<_WindowCreationRequest> request);

@Native<Int64 Function(Int64, Pointer<_WindowCreationRequest>)>(
  symbol: 'InternalFlutter_WindowController_CreateDialogWindow',
)
external int _createDialogWindow(int engineId, Pointer<_WindowCreationRequest> request);

@Native<Int64 Function(Int64, Pointer<_WindowCreationRequest>)>(
  symbol: 'InternalFlutter_WindowController_CreateTooltipWindow',
)
external int _createTooltipWindow(int engineId, Pointer<_WindowCreationRequest> request);

@Native<Void Function(Int64, Pointer<Void>)>(symbol: 'InternalFlutter_Window_Destroy')
external void _destroyWindow(int engineId, Pointer<Void> handle);

@Native<_Size Function(Pointer<Void>)>(symbol: 'InternalFlutter_Window_GetContentSize')
external _Size _getWindowContentSize(Pointer<Void> windowHandle);

@Native<Void Function(Pointer<Void>, Pointer<_Sizing>)>(
  symbol: 'InternalFlutter_Window_SetContentSize',
)
external void _setWindowContentSize(Pointer<Void> windowHandle, Pointer<_Sizing> size);

@Native<Void Function(Pointer<Void>, Pointer<ffi.Utf8>)>(symbol: 'InternalFlutter_Window_SetTitle')
external void _setWindowTitle(Pointer<Void> windowHandle, Pointer<ffi.Utf8> title);

@Native<Void Function(Pointer<Void>, Bool)>(symbol: 'InternalFlutter_Window_SetMaximized')
external void _setMaximized(Pointer<Void> windowHandle, bool maximized);

@Native<Bool Function(Pointer<Void>)>(symbol: 'InternalFlutter_Window_IsMaximized')
external bool _isMaximized(Pointer<Void> windowHandle);

@Native<Void Function(Pointer<Void>)>(symbol: 'InternalFlutter_Window_Minimize')
external void _minimize(Pointer<Void> windowHandle);

@Native<Void Function(Pointer<Void>)>(symbol: 'InternalFlutter_Window_Unminimize')
external void _unminimize(Pointer<Void> windowHandle);

@Native<Bool Function(Pointer<Void>)>(symbol: 'InternalFlutter_Window_IsMinimized')
external bool _isMinimized(Pointer<Void> windowHandle);

@Native<Void Function(Pointer<Void>, Bool)>(symbol: 'InternalFlutter_Window_SetFullScreen')
external void _setFullscreen(Pointer<Void> windowHandle, bool fullscreen);

@Native<Bool Function(Pointer<Void>)>(symbol: 'InternalFlutter_Window_IsFullScreen')
external bool _isFullscreen(Pointer<Void> windowHandle);

@Native<Void Function(Pointer<Void>)>(symbol: 'InternalFlutter_Window_Activate')
external void _activate(Pointer<Void> windowHandle);
