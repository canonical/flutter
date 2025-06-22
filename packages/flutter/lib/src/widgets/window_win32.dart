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

typedef HWND = Pointer<Void>;

/// Handler for Win32 messages.
abstract class WindowsMessageHandler {
  /// Handles a window message. Returned value, if not null will be
  /// returned to the system as LRESULT and will stop all other
  /// handlers from being called.
  int? handleWindowsMessage(
    FlutterView view,
    Pointer<Void> windowHandle,
    int message,
    int wParam,
    int lParam,
  );
}

/// Windowing owner implementation for Windows.
class WindowingOwnerWin32 extends WindowingOwner {
  /// Creates a new [WindowingOwnerWin32] instance.
  WindowingOwnerWin32() {
    final Pointer<_WindowingInitRequest> request =
        ffi.calloc<_WindowingInitRequest>()
          ..ref.onMessage =
              NativeCallable<Void Function(Pointer<_WindowsMessage>)>.isolateLocal(
                _onMessage,
              ).nativeFunction;
    _initializeWindowing(PlatformDispatcher.instance.engineId!, request);
    ffi.calloc.free(request);
  }

  @override
  RegularWindowController createRegularWindowController({
    required WindowSizing contentSize,
    required RegularWindowControllerDelegate delegate,
  }) {
    return RegularWindowControllerWin32(owner: this, delegate: delegate, contentSize: contentSize);
  }

  @override
  DialogWindowController createDialogWindowController({
    required WindowSizing contentSize,
    required DialogWindowControllerDelegate delegate,
    FlutterView? parent,
  }) {
    return DialogWindowControllerWin32(
      owner: this,
      delegate: delegate,
      contentSize: contentSize,
      parent: parent,
    );
  }

  /// Register new message handler. The handler will be called for unhandled
  /// messages for all top level windows.
  void addMessageHandler(WindowsMessageHandler handler) {
    _messageHandlers.add(handler);
  }

  /// Unregister message handler.
  void removeMessageHandler(WindowsMessageHandler handler) {
    _messageHandlers.remove(handler);
  }

  final List<WindowsMessageHandler> _messageHandlers = <WindowsMessageHandler>[];

  void _onMessage(Pointer<_WindowsMessage> message) {
    final List<WindowsMessageHandler> handlers = List<WindowsMessageHandler>.from(_messageHandlers);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == message.ref.viewId,
    );
    for (final WindowsMessageHandler handler in handlers) {
      final int? result = handler.handleWindowsMessage(
        flutterView,
        message.ref.windowHandle,
        message.ref.message,
        message.ref.wParam,
        message.ref.lParam,
      );
      if (result != null) {
        message.ref.handled = true;
        message.ref.lResult = result;
        return;
      }
    }
  }

  @override
  bool hasTopLevelWindows() {
    return _hasTopLevelWindows(PlatformDispatcher.instance.engineId!);
  }

  @Native<Bool Function(Int64)>(symbol: 'FlutterWindowingHasTopLevelWindows')
  external static bool _hasTopLevelWindows(int engineId);

  @Native<Void Function(Int64, Pointer<_WindowingInitRequest>)>(
    symbol: 'FlutterWindowingInitialize',
  )
  external static void _initializeWindowing(int engineId, Pointer<_WindowingInitRequest> request);
}

class _HwndWrapper {
  _HwndWrapper({required this.hwnd});

  @Native<Pointer<Void> Function(Int64, Int64)>(symbol: 'FlutterGetWindowHandle')
  external static Pointer<Void> getWindowHandle(int engineId, int viewId);

  @Native<Void Function(Pointer<Void>)>(symbol: 'DestroyWindow')
  external static void _destroyWindow(Pointer<Void> windowHandle);

  @Native<_Size Function(Pointer<Void>)>(symbol: 'FlutterGetWindowContentSize')
  external static _Size _getWindowContentSize(Pointer<Void> windowHandle);

  @Native<Void Function(Pointer<Void>, Pointer<ffi.Utf16>)>(symbol: 'SetWindowTextW')
  external static void _setWindowTitle(Pointer<Void> windowHandle, Pointer<ffi.Utf16> title);

  @Native<Void Function(Pointer<Void>, Pointer<_Sizing>)>(symbol: 'FlutterSetWindowContentSize')
  external static void _setWindowContentSize(Pointer<Void> windowHandle, Pointer<_Sizing> size);

  @Native<Void Function(Pointer<Void>, Int32)>(symbol: 'ShowWindow')
  external static void _showWindow(Pointer<Void> windowHandle, int command);

  @Native<Int32 Function(Pointer<Void>)>(symbol: 'IsIconic')
  external static int _isIconic(Pointer<Void> windowHandle);

  @Native<Int32 Function(Pointer<Void>)>(symbol: 'IsZoomed')
  external static int _isZoomed(Pointer<Void> windowHandle);

  @Native<Pointer<Void> Function(Pointer<Void>, Uint32)>(symbol: 'GetWindow')
  external static Pointer<Void> _getWindow(Pointer<Void> windowHandle, int cmd);

  Size contentSize() {
    _ensureNotDestroyed();
    final _Size size = _getWindowContentSize(hwnd);
    final Size result = Size(size.width, size.height);
    return result;
  }

  void setTitle(String title) {
    _ensureNotDestroyed();
    final Pointer<ffi.Utf16> titlePointer = title.toNativeUtf16();
    _setWindowTitle(hwnd, titlePointer);
    ffi.calloc.free(titlePointer);
  }

  void updateContentSize(WindowSizing sizing) {
    _ensureNotDestroyed();
    final Pointer<_Sizing> ffiSizing = ffi.calloc<_Sizing>();
    ffiSizing.ref.set(sizing);
    _setWindowContentSize(hwnd, ffiSizing);
    ffi.calloc.free(ffiSizing);
  }

  void activate() {
    _ensureNotDestroyed();
    _showWindow(hwnd, SW_RESTORE);
  }

  bool isFullscreen() {
    return false;
  }

  void setFullscreen(bool fullscreen, {int? displayId}) {}

  bool isMaximized() {
    _ensureNotDestroyed();
    return _isZoomed(hwnd) != 0;
  }

  bool isMinimized() {
    _ensureNotDestroyed();
    return _isIconic(hwnd) != 0;
  }

  void setMinimized(bool minimized) {
    _ensureNotDestroyed();
    if (minimized) {
      _showWindow(hwnd, SW_MINIMIZE);
    } else {
      _showWindow(hwnd, SW_RESTORE);
    }
  }

  void setMaximized(bool maximized) {
    _ensureNotDestroyed();
    if (maximized) {
      _showWindow(hwnd, SW_MAXIMIZE);
    } else {
      _showWindow(hwnd, SW_RESTORE);
    }
  }

  void _ensureNotDestroyed() {
    if (_destroyed) {
      throw StateError('Window has been destroyed.');
    }
  }

  HWND? get parent {
    _ensureNotDestroyed();
    return _getWindow(hwnd, _GW_OWNER);
  }

  bool destroy() {
    if (_destroyed) {
      return false;
    }
    _destroyWindow(hwnd);
    _destroyed = true;
    return true;
  }

  /// Marks the HWND as destroyed. This is used when destruction is driven by the
  /// native side, for example when the owner is destroyed.
  /// Returns true if the window was not destroyed before, false otherwise.
  bool markDestroyed() {
    if (_destroyed) {
      return false;
    }
    _destroyed = true;
    return true;
  }

  final HWND hwnd;
  bool _destroyed = false;
  static const int _WM_SIZE = 0x0005;
  static const int _WM_CLOSE = 0x0010;
  static const int _WM_DESTROY = 0x0002;

  static const int SW_RESTORE = 9;
  static const int SW_MAXIMIZE = 3;
  static const int SW_MINIMIZE = 6;

  static const int _GW_OWNER = 4;
}

/// The Win32 implementation of the regular window controller.
class RegularWindowControllerWin32 extends RegularWindowController
    implements WindowsMessageHandler {
  /// Creates a new regular window controller for Win32. When this constructor
  /// completes the FlutterView is created and framework is aware of it.
  RegularWindowControllerWin32({
    required WindowingOwnerWin32 owner,
    required RegularWindowControllerDelegate delegate,
    required WindowSizing contentSize,
  }) : _owner = owner,
       _delegate = delegate,
       super.empty() {
    owner.addMessageHandler(this);
    final Pointer<_RegularWindowCreationRequest> request =
        ffi.calloc<_RegularWindowCreationRequest>()..ref.contentSize.set(contentSize);
    final int viewId = _createWindow(PlatformDispatcher.instance.engineId!, request);
    ffi.calloc.free(request);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);
    _hwnd = _HwndWrapper(
      hwnd: _HwndWrapper.getWindowHandle(PlatformDispatcher.instance.engineId!, flutterView.viewId),
    );
  }

  @override
  Size get contentSize {
    return _hwnd.contentSize();
  }

  @override
  void setTitle(String title) {
    _hwnd.setTitle(title);
  }

  @override
  void updateContentSize(WindowSizing sizing) {
    _hwnd.updateContentSize(sizing);
  }

  @override
  void activate() {
    _hwnd.activate();
  }

  @override
  bool isFullscreen() {
    return _hwnd.isFullscreen();
  }

  @override
  void setFullscreen(bool fullscreen, {int? displayId}) {
    _hwnd.setFullscreen(fullscreen, displayId: displayId);
  }

  @override
  bool isMaximized() {
    return _hwnd.isMaximized();
  }

  @override
  bool isMinimized() {
    return _hwnd.isMinimized();
  }

  @override
  void setMinimized(bool minimized) {
    _hwnd.setMinimized(minimized);
  }

  @override
  void setMaximized(bool maximized) {
    _hwnd.setMaximized(maximized);
  }

  /// Returns HWND pointer to the top level window.
  HWND get hwnd {
    return _hwnd.hwnd;
  }

  @override
  void destroy() {
    if (_hwnd.destroy()) {
      _delegate.onWindowDestroyed();
      _owner.removeMessageHandler(this);
    }
  }

  @override
  int? handleWindowsMessage(
    FlutterView view,
    Pointer<Void> windowHandle,
    int message,
    int wParam,
    int lParam,
  ) {
    if (view.viewId != rootView.viewId) {
      return null;
    }

    if (message == _HwndWrapper._WM_DESTROY && _hwnd.markDestroyed()) {
      _delegate.onWindowDestroyed();
    } else if (message == _HwndWrapper._WM_CLOSE) {
      _delegate.onWindowCloseRequested(this);
      return 0;
    } else if (message == _HwndWrapper._WM_SIZE) {
      notifyListeners();
    }
    return null;
  }

  final RegularWindowControllerDelegate _delegate;
  final WindowingOwnerWin32 _owner;
  late final _HwndWrapper _hwnd;

  @Native<Int64 Function(Int64, Pointer<_RegularWindowCreationRequest>)>(
    symbol: 'FlutterCreateRegularWindow',
  )
  external static int _createWindow(int engineId, Pointer<_RegularWindowCreationRequest> request);
}

/// The Win32 implementation of the dialog window controller.
class DialogWindowControllerWin32 extends DialogWindowController implements WindowsMessageHandler {
  /// Creates a new dialog window controller for Win32. When this constructor
  /// completes the FlutterView is created and framework is aware of it.
  DialogWindowControllerWin32({
    required WindowingOwnerWin32 owner,
    required DialogWindowControllerDelegate delegate,
    required WindowSizing contentSize,
    FlutterView? parent,
  }) : _owner = owner,
       _delegate = delegate,
       super.empty() {
    final int engineId = PlatformDispatcher.instance.engineId!;

    // If the parent is minimized, restore it to prevent the modal dialog from being hidden.
    _HwndWrapper? parentWindow;
    if (parent != null) {
      parentWindow = _HwndWrapper(hwnd: _HwndWrapper.getWindowHandle(engineId, parent.viewId));
      if (parentWindow.isMinimized()) {
        setMinimized(true);
      }
    }

    final Pointer<_DialogWindowCreationRequest> request =
        ffi.calloc<_DialogWindowCreationRequest>()
          ..ref.contentSize.set(contentSize)
          ..ref.parentWindow = parentWindow?.hwnd ?? nullptr;
    final int viewId = _createWindow(engineId, request);
    ffi.calloc.free(request);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);
    owner.addMessageHandler(this);
    _hwnd = _HwndWrapper(hwnd: _HwndWrapper.getWindowHandle(engineId, viewId));
  }

  @override
  Size get contentSize {
    return _hwnd.contentSize();
  }

  @override
  FlutterView? get parent {
    final HWND? owner = _hwnd.parent;
    if (owner == nullptr) {
      return null;
    }

    final int engineId = PlatformDispatcher.instance.engineId!;
    return PlatformDispatcher.instance.views.cast<FlutterView?>().firstWhere(
      (FlutterView? view) => _HwndWrapper.getWindowHandle(engineId, view!.viewId) == owner,
      orElse: () => null,
    );
  }

  @override
  void setTitle(String title) {
    _hwnd.setTitle(title);
  }

  @override
  void updateContentSize(WindowSizing size) {
    _hwnd.updateContentSize(size);
  }

  @override
  void setMinimized(bool minimized) {
    _hwnd.setMinimized(minimized);
  }

  @override
  bool isMinimized() {
    return _hwnd.isMinimized();
  }

  /// Returns HWND pointer to the top level window.
  Pointer<Void> getWindowHandle() {
    return _hwnd.hwnd;
  }

  @override
  void destroy() {
    if (_hwnd.destroy()) {
      _delegate.onWindowDestroyed();
      _owner.removeMessageHandler(this);
    }
  }

  @override
  int? handleWindowsMessage(
    FlutterView view,
    Pointer<Void> windowHandle,
    int message,
    int wParam,
    int lParam,
  ) {
    if (view.viewId != rootView.viewId) {
      return null;
    }

    // Window will be destroyed from native side when owner is destroyed.
    if (message == _HwndWrapper._WM_DESTROY && _hwnd.markDestroyed()) {
      _delegate.onWindowDestroyed();
    } else if (message == _HwndWrapper._WM_CLOSE) {
      _delegate.onWindowCloseRequested(this);
      return 0;
    } else if (message == _HwndWrapper._WM_SIZE) {
      notifyListeners();
    }
    return null;
  }

  final DialogWindowControllerDelegate _delegate;
  final WindowingOwnerWin32 _owner;
  late final _HwndWrapper _hwnd;

  @Native<Int64 Function(Int64, Pointer<_DialogWindowCreationRequest>)>(
    symbol: 'FlutterCreateDialogWindow',
  )
  external static int _createWindow(int engineId, Pointer<_DialogWindowCreationRequest> request);
}

/// Request to initialize windowing system.
final class _WindowingInitRequest extends Struct {
  external Pointer<NativeFunction<Void Function(Pointer<_WindowsMessage>)>> onMessage;
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

final class _RegularWindowCreationRequest extends Struct {
  external _Sizing contentSize;
}

final class _DialogWindowCreationRequest extends Struct {
  external _Sizing contentSize;
  external Pointer<Void> parentWindow;
}

/// Windows message received for all top level windows (regardless whether
/// they are created using a windowing controller).
final class _WindowsMessage extends Struct {
  @Int64()
  external int viewId;

  external Pointer<Void> windowHandle;

  @Int32()
  external int message;

  @Int64()
  external int wParam;

  @Int64()
  external int lParam;

  @Int64()
  external int lResult;

  @Bool()
  external bool handled;
}

final class _Size extends Struct {
  @Double()
  external double width;

  @Double()
  external double height;
}
