// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ffi' hide Size;
import 'dart:ui' show FlutterView;
import 'dart:math';
import 'package:ffi/ffi.dart' as ffi;
import 'package:flutter/foundation.dart';
import 'package:flutter/rendering.dart';

import 'binding.dart';
import 'window.dart';

enum _WindowStyle {
  overlapped(0x00000000),
  popup(0x80000000),
  child(0x40000000),
  minimized(0x20000000),
  visible(0x10000000),
  disabled(0x08000000),
  clipSiblings(0x04000000),
  clipChildren(0x02000000),
  maximize(0x01000000),
  caption(0x00C00000),
  border(0x00800000),
  dlgFrame(0x00400000),
  vScroll(0x00200000),
  hScroll(0x00100000),
  sysMenu(0x00080000),
  thickFrame(0x00040000),
  group(0x00020000),
  tabStop(0x00010000),
  minimizeBox(0x00020000),
  maximizeBox(0x00010000),
  overlappedWindow(0x00CF0000),
  popupWindow(0x80880000),
  childWindow(0x40000000);

  final int value;
  const _WindowStyle(this.value);
}

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

// Define RECT
final class RECT extends Struct {
  @Int32()
  external int left;
  @Int32()
  external int top;
  @Int32()
  external int right;
  @Int32()
  external int bottom;
}

// Signature of AdjustWindowRectExForDpi
typedef AdjustWindowRectExForDpiC =
    Int32 Function(Pointer<RECT> rect, Uint32 dwStyle, Int32 bMenu, Uint32 dwExStyle, Uint32 dpi);
typedef AdjustWindowRectExForDpiDart =
    int Function(Pointer<RECT> rect, int dwStyle, int bMenu, int dwExStyle, int dpi);

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
    // Create the native window
    _createHwnd(contentSize);

    // Create the corresponding view
    final Pointer<_WindowCreationRequest> request =
        ffi.calloc<_WindowCreationRequest>()..ref.contentSize.set(contentSize);
    final int viewId = _createWindow(PlatformDispatcher.instance.engineId!, request);
    ffi.calloc.free(request);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);

    // Adjust the window position

    // Display the window
  }

  Size? getWindowSizeForClientSize({
    required Size clientSize,
    Size? minSize,
    Size? maxSize,
    required int windowStyle,
    required int extendedWindowStyle,
    required int hwnd,
  }) {
    Size clampToVirtualScreen(Size size) {
      return size;
    }

    final _user32 = DynamicLibrary.open('user32.dll');

    final _adjustWindowRectExForDpi = _user32
        .lookupFunction<AdjustWindowRectExForDpiC, AdjustWindowRectExForDpiDart>(
          'AdjustWindowRectExForDpi',
        );

    final dpi = 96; // TODO: Get this
    final scaleFactor = 1.0; // TODO: Get this

    final rectPtr = ffi.calloc<RECT>();
    rectPtr.ref.left = 0;
    rectPtr.ref.top = 0;
    rectPtr.ref.right = (clientSize.width * scaleFactor).toInt();
    rectPtr.ref.bottom = (clientSize.height * scaleFactor).toInt();

    final success = _adjustWindowRectExForDpi(rectPtr, windowStyle, 0, extendedWindowStyle, dpi);
    if (success == 0) {
      ffi.calloc.free(rectPtr);
      return null;
    }

    final width = (rectPtr.ref.right - rectPtr.ref.left).toDouble();
    final height = (rectPtr.ref.bottom - rectPtr.ref.top).toDouble();
    ffi.calloc.free(rectPtr);

    final nonClientWidth = width - (clientSize.width * scaleFactor);
    final nonClientHeight = height - (clientSize.height * scaleFactor);

    double resultWidth = width;
    double resultHeight = height;

    if (minSize != null) {
      final minPhysicalSize = clampToVirtualScreen(
        Size(
          minSize.width * scaleFactor + nonClientWidth,
          minSize.height * scaleFactor + nonClientHeight,
        ),
      );
      resultWidth = max(resultWidth, minPhysicalSize.width);
      resultHeight = max(resultHeight, minPhysicalSize.height);
    }

    if (maxSize != null) {
      final maxPhysicalSize = clampToVirtualScreen(
        Size(
          maxSize.width * scaleFactor + nonClientWidth,
          maxSize.height * scaleFactor + nonClientHeight,
        ),
      );
      resultWidth = min(resultWidth, maxPhysicalSize.width);
      resultHeight = min(resultHeight, maxPhysicalSize.height);
    }

    return Size(resultWidth, resultHeight);
  }

  void _createHwnd(WindowSizing contentSize) {
    getWindowSizeForClientSize(
      clientSize: contentSize.preferredSize ?? Size(640, 480),
      windowStyle: _WindowStyle.overlappedWindow.value,
      extendedWindowStyle: 0,
      hwnd: 0,
    );
  }

  @override
  Size get contentSize {
    _ensureNotDestroyed();
    final _Size size = _getWindowContentSize(getWindowHandle());
    final Size result = Size(size.width, size.height);
    return result;
  }

  @override
  void setTitle(String title) {
    _ensureNotDestroyed();
    final Pointer<ffi.Utf16> titlePointer = title.toNativeUtf16();
    _setWindowTitle(getWindowHandle(), titlePointer);
    ffi.calloc.free(titlePointer);
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
  void activate() {
    _ensureNotDestroyed();
    _showWindow(getWindowHandle(), SW_RESTORE);
  }

  @override
  bool isFullscreen() {
    return false;
  }

  @override
  void setFullscreen(bool fullscreen, {int? displayId}) {}

  @override
  bool isMaximized() {
    _ensureNotDestroyed();
    return _isZoomed(getWindowHandle()) != 0;
  }

  @override
  bool isMinimized() {
    _ensureNotDestroyed();
    return _isIconic(getWindowHandle()) != 0;
  }

  @override
  void setMinimized(bool minimized) {
    _ensureNotDestroyed();
    if (minimized) {
      _showWindow(getWindowHandle(), SW_MINIMIZE);
    } else {
      _showWindow(getWindowHandle(), SW_RESTORE);
    }
  }

  @override
  void setMaximized(bool maximized) {
    _ensureNotDestroyed();
    if (maximized) {
      _showWindow(getWindowHandle(), SW_MAXIMIZE);
    } else {
      _showWindow(getWindowHandle(), SW_RESTORE);
    }
  }

  /// Returns HWND pointer to the top level window.
  Pointer<Void> getWindowHandle() {
    _ensureNotDestroyed();
    return _getWindowHandle(PlatformDispatcher.instance.engineId!, rootView.viewId);
  }

  void _ensureNotDestroyed() {
    if (_destroyed) {
      throw StateError('Window has been destroyed.');
    }
  }

  final RegularWindowControllerDelegate _delegate;
  bool _destroyed = false;

  @override
  void destroy() {
    if (_destroyed) {
      return;
    }
    _destroyWindow(getWindowHandle());
    _destroyed = true;
    _delegate.onWindowDestroyed();
    _owner.removeMessageHandler(this);
  }

  static const int _WM_SIZE = 0x0005;
  static const int _WM_CLOSE = 0x0010;

  static const int SW_RESTORE = 9;
  static const int SW_MAXIMIZE = 3;
  static const int SW_MINIMIZE = 6;

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

    if (message == _WM_CLOSE) {
      _delegate.onWindowCloseRequested(this);
      return 0;
    } else if (message == _WM_SIZE) {
      notifyListeners();
    }
    return null;
  }

  final WindowingOwnerWin32 _owner;

  @Native<Int64 Function(Int64, Pointer<_WindowCreationRequest>)>(
    symbol: 'FlutterCreateRegularWindow',
  )
  external static int _createWindow(int engineId, Pointer<_WindowCreationRequest> request);

  @Native<Pointer<Void> Function(Int64, Int64)>(symbol: 'FlutterGetWindowHandle')
  external static Pointer<Void> _getWindowHandle(int engineId, int viewId);

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

final class _WindowCreationRequest extends Struct {
  external _Sizing contentSize;
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
