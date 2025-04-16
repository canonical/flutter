// ignore_for_file: public_member_api_docs, avoid_unused_constructor_parameters

import 'dart:ffi' hide Size;
import 'dart:ui' show FlutterView;
import 'package:ffi/ffi.dart' as ffi;
import 'package:flutter/foundation.dart';

import 'package:flutter/material.dart';

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

class WindowingOwnerWin32 extends WindowingOwner {
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

  void addMessageHandler(WindowsMessageHandler handler) {
    _messageHandlers.add(handler);
  }

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

class RegularWindowControllerWin32 extends RegularWindowController
    implements WindowsMessageHandler {
  RegularWindowControllerWin32({
    required WindowingOwnerWin32 owner,
    required RegularWindowControllerDelegate delegate,
    required WindowSizing contentSize,
  }) : _owner = owner,
       _delegate = delegate,
       super.empty() {
    final Pointer<_RegularWindowCreationRequest> request =
        ffi.calloc<_RegularWindowCreationRequest>()..ref.contentSize.set(contentSize);
    final int viewId = _createWindow(PlatformDispatcher.instance.engineId!, request);
    ffi.calloc.free(request);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);
    owner.addMessageHandler(this);
  }

  @override
  Size get contentSize {
    _ensureNotDestroyed();
    final _Size size = _getWindowContentSize(getWindowHandle());
    final Size result = Size(size.width, size.height);
    return result;
  }

  @override
  WindowState get state {
    _ensureNotDestroyed();
    final int state = _getWindowState(getWindowHandle());
    return WindowState.values[state];
  }

  @override
  void setState(WindowState state) {
    _ensureNotDestroyed();
    _setWindowState(getWindowHandle(), state.index);
  }

  @override
  void setTitle(String title) {
    _ensureNotDestroyed();
    final Pointer<ffi.Utf16> titlePointer = title.toNativeUtf16();
    _setWindowTitle(getWindowHandle(), titlePointer);
    ffi.calloc.free(titlePointer);
  }

  @override
  void setContentSize(WindowSizing size) {
    _ensureNotDestroyed();
    final Pointer<_Sizing> sizing = ffi.calloc<_Sizing>();
    sizing.ref.set(size);
    _setWindowContentSize(getWindowHandle(), sizing);
    ffi.calloc.free(sizing);
  }

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

  static const int WM_SIZE = 0x0005;
  static const int WM_CLOSE = 0x0010;

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

    if (message == WM_CLOSE) {
      _delegate.onWindowCloseRequested(this);
      return 0;
    } else if (message == WM_SIZE) {
      notifyListeners();
    }
    return null;
  }

  final WindowingOwnerWin32 _owner;

  @Native<Int64 Function(Int64, Pointer<_RegularWindowCreationRequest>)>(
    symbol: 'FlutterCreateRegularWindow',
  )
  external static int _createWindow(int engineId, Pointer<_RegularWindowCreationRequest> request);

  @Native<Pointer<Void> Function(Int64, Int64)>(symbol: 'FlutterGetWindowHandle')
  external static Pointer<Void> _getWindowHandle(int engineId, int viewId);

  @Native<Void Function(Pointer<Void>)>(symbol: 'DestroyWindow')
  external static void _destroyWindow(Pointer<Void> windowHandle);

  @Native<_Size Function(Pointer<Void>)>(symbol: 'FlutterGetWindowContentSize')
  external static _Size _getWindowContentSize(Pointer<Void> windowHandle);

  @Native<Int64 Function(Pointer<Void>)>(symbol: 'FlutterGetWindowState')
  external static int _getWindowState(Pointer<Void> windowHandle);

  @Native<Void Function(Pointer<Void>, Int64)>(symbol: 'FlutterSetWindowState')
  external static void _setWindowState(Pointer<Void> windowHandle, int state);

  @Native<Void Function(Pointer<Void>, Pointer<ffi.Utf16>)>(symbol: 'SetWindowTextW')
  external static void _setWindowTitle(Pointer<Void> windowHandle, Pointer<ffi.Utf16> title);

  @Native<Void Function(Pointer<Void>, Pointer<_Sizing>)>(symbol: 'FlutterSetWindowContentSize')
  external static void _setWindowContentSize(Pointer<Void> windowHandle, Pointer<_Sizing> size);
}

class DialogWindowControllerWin32 extends DialogWindowController implements WindowsMessageHandler {
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
    Pointer<Void> parentWindow = nullptr;
    if (parent != null) {
      parentWindow = _getWindowHandle(engineId, parent.viewId);
      final int state = _getWindowState(parentWindow);
      if (WindowState.values[state] == WindowState.minimized) {
        _setWindowState(parentWindow, WindowState.restored.index);
      }
    }

    final Pointer<_DialogWindowCreationRequest> request =
        ffi.calloc<_DialogWindowCreationRequest>()
          ..ref.contentSize.set(contentSize)
          ..ref.parentWindow = parentWindow;
    final int viewId = _createWindow(engineId, request);
    ffi.calloc.free(request);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);
    owner.addMessageHandler(this);
  }

  @override
  Size get contentSize {
    _ensureNotDestroyed();
    final _Size size = _getWindowContentSize(getWindowHandle());
    final Size result = Size(size.width, size.height);
    return result;
  }

  @override
  WindowState get state {
    _ensureNotDestroyed();
    final int state = _getWindowState(getWindowHandle());
    return WindowState.values[state];
  }

  static const int _GW_OWNER = 4;

  @override
  FlutterView? get parent {
    _ensureNotDestroyed();
    final int engineId = PlatformDispatcher.instance.engineId!;
    final Pointer<Void> parentWindow = _getWindow(getWindowHandle(), _GW_OWNER);
    return PlatformDispatcher.instance.views.cast<FlutterView?>().firstWhere(
      (FlutterView? view) => _getWindowHandle(engineId, view!.viewId) == parentWindow,
      orElse: () => null,
    );
  }

  @override
  void setTitle(String title) {
    _ensureNotDestroyed();
    final Pointer<ffi.Utf16> titlePointer = title.toNativeUtf16();
    _setWindowTitle(getWindowHandle(), titlePointer);
    ffi.calloc.free(titlePointer);
  }

  @override
  void setContentSize(WindowSizing size) {
    _ensureNotDestroyed();
    final Pointer<_Sizing> sizing = ffi.calloc<_Sizing>();
    sizing.ref.set(size);
    _setWindowContentSize(getWindowHandle(), sizing);
    ffi.calloc.free(sizing);
  }

  Pointer<Void> getWindowHandle() {
    _ensureNotDestroyed();
    return _getWindowHandle(PlatformDispatcher.instance.engineId!, rootView.viewId);
  }

  void _ensureNotDestroyed() {
    if (_destroyed) {
      throw StateError('Window has been destroyed.');
    }
  }

  final DialogWindowControllerDelegate _delegate;
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

  static const int WM_SIZE = 0x0005;
  static const int WM_CLOSE = 0x0010;

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

    if (message == WM_CLOSE) {
      _delegate.onWindowCloseRequested(this);
      return 0;
    } else if (message == WM_SIZE) {
      notifyListeners();
    }
    return null;
  }

  final WindowingOwnerWin32 _owner;

  @Native<Int64 Function(Int64, Pointer<_DialogWindowCreationRequest>)>(
    symbol: 'FlutterCreateDialogWindow',
  )
  external static int _createWindow(int engineId, Pointer<_DialogWindowCreationRequest> request);

  @Native<Pointer<Void> Function(Int64, Int64)>(symbol: 'FlutterGetWindowHandle')
  external static Pointer<Void> _getWindowHandle(int engineId, int viewId);

  @Native<Void Function(Pointer<Void>)>(symbol: 'DestroyWindow')
  external static void _destroyWindow(Pointer<Void> windowHandle);

  @Native<_Size Function(Pointer<Void>)>(symbol: 'FlutterGetWindowContentSize')
  external static _Size _getWindowContentSize(Pointer<Void> windowHandle);

  @Native<Int64 Function(Pointer<Void>)>(symbol: 'FlutterGetWindowState')
  external static int _getWindowState(Pointer<Void> windowHandle);

  @Native<Void Function(Pointer<Void>, Int64)>(symbol: 'FlutterSetWindowState')
  external static void _setWindowState(Pointer<Void> windowHandle, int state);

  @Native<Void Function(Pointer<Void>, Pointer<ffi.Utf16>)>(symbol: 'SetWindowTextW')
  external static void _setWindowTitle(Pointer<Void> windowHandle, Pointer<ffi.Utf16> title);

  @Native<Void Function(Pointer<Void>, Pointer<_Sizing>)>(symbol: 'FlutterSetWindowContentSize')
  external static void _setWindowContentSize(Pointer<Void> windowHandle, Pointer<_Sizing> size);

  @Native<Pointer<Void> Function(Pointer<Void>, Uint32)>(symbol: 'GetWindow')
  external static Pointer<Void> _getWindow(Pointer<Void> windowHandle, int cmd);
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
