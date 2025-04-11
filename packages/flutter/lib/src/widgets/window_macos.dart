import 'dart:ffi' hide Size;
import 'dart:ui' show FlutterView;

import 'package:ffi/ffi.dart' as ffi;
import 'package:flutter/material.dart';
import 'package:flutter/src/foundation/binding.dart';

/// The macOS implementation of the windowing API.
class WindowingOwnerMacOS extends WindowingOwner {
  @override
  RegularWindowController createRegularWindowController({
    required Size size,
    required RegularWindowControllerDelegate delegate,
    BoxConstraints? sizeConstraints,
  }) {
    final RegularWindowControllerMacOS res = RegularWindowControllerMacOS(
      owner: this,
      delegate: delegate,
      size: size,
      sizeConstraints: sizeConstraints,
    );
    _activeControllers.add(res);
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

  @Native<Pointer<Void> Function(Int64, Int64)>(symbol: 'FlutterGetWindowHandle')
  external static Pointer<Void> _getWindowHandle(int engineId, int viewId);
}

/// The macOS implementation of the regular window controller.
class RegularWindowControllerMacOS extends RegularWindowController {
  /// Creates a new regular window controller for macOS. When this constructor
  /// completes the FlutterView is created and framework is aware of it.
  RegularWindowControllerMacOS({
    required WindowingOwnerMacOS owner,
    required RegularWindowControllerDelegate delegate,
    BoxConstraints? sizeConstraints,
    required Size size,
    String? title,
  }) : _owner = owner,
       _delegate = delegate,
       super.empty() {
    _onClose = NativeCallable<Void Function()>.isolateLocal(_handleOnClose);
    _onResize = NativeCallable<Void Function()>.isolateLocal(_handleOnResize);
    final Pointer<_WindowCreationRequest> request =
        ffi.calloc<_WindowCreationRequest>()
          ..ref.width = size.width
          ..ref.height = size.height
          ..ref.minWidth = sizeConstraints?.minWidth ?? 0
          ..ref.minHeight = sizeConstraints?.minHeight ?? 0
          ..ref.maxWidth = sizeConstraints?.maxWidth ?? 0
          ..ref.maxHeight = sizeConstraints?.maxHeight ?? 0
          ..ref.onClose = _onClose.nativeFunction
          ..ref.onSizeChange = _onResize.nativeFunction;

    final int viewId = _createWindow(PlatformDispatcher.instance.engineId!, request);
    ffi.calloc.free(request);
    final FlutterView flutterView = WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
    );
    setView(flutterView);
    if (title != null) {
      setTitle(title);
    }
  }

  /// Returns window handle for the current window.
  /// The handle is a pointer to NSWindow instance.
  Pointer<Void> getWindowHandle() {
    return WindowingOwnerMacOS.getWindowHandle(rootView);
  }

  bool _destroyed = false;

  @override
  void destroy() {
    if (_destroyed) {
      return;
    }
    _destroyed = true;
    _owner._activeControllers.remove(this);
    _destroyWindow(PlatformDispatcher.instance.engineId!, getWindowHandle());
    _delegate.onWindowDestroyed();
    _onClose.close();
    _onResize.close();
  }

  void _handleOnClose() {
    _delegate.onWindowCloseRequested(this);
  }

  void _handleOnResize() {
    notifyListeners();
  }

  /// Updates the window size.
  void setSize(Size size) {
    _setWindowSize(getWindowHandle(), size.width, size.height);
  }

  /// Updates the window title.
  void setTitle(String title) {
    final Pointer<ffi.Utf8> titlePointer = title.toNativeUtf8();
    _setWindowTitle(getWindowHandle(), titlePointer);
    ffi.calloc.free(titlePointer);
  }

  @override
  void modify({Size? size, String? title, WindowState? state}) {
    if (size != null) {
      setSize(size);
    }
    if (title != null) {
      setTitle(title);
    }
    if (state != null) {
      setState(state);
    }
  }

  final WindowingOwnerMacOS _owner;
  final RegularWindowControllerDelegate _delegate;
  late final NativeCallable<Void Function()> _onClose;
  late final NativeCallable<Void Function()> _onResize;

  @override
  Size get size {
    final Pointer<_Size> size = ffi.calloc<_Size>();
    _getWindowSize(getWindowHandle(), size);
    final Size result = Size(size.ref.width, size.ref.height);
    ffi.calloc.free(size);
    return result;
  }

  @override
  WindowState get state => WindowState.values[_getWindowState(getWindowHandle())];

  /// Updates window state.
  void setState(WindowState state) {
    _setWindowState(getWindowHandle(), state.index);
  }

  @Native<Int64 Function(Int64, Pointer<_WindowCreationRequest>)>(
    symbol: 'FlutterCreateRegularWindow',
  )
  external static int _createWindow(int engineId, Pointer<_WindowCreationRequest> request);

  @Native<Void Function(Int64, Pointer<Void>)>(symbol: 'FlutterDestroyWindow')
  external static void _destroyWindow(int engineId, Pointer<Void> handle);

  @Native<Void Function(Pointer<Void>, Pointer<_Size>)>(symbol: 'FlutterGetWindowSize')
  external static void _getWindowSize(Pointer<Void> windowHandle, Pointer<_Size> size);

  @Native<Void Function(Pointer<Void>, Double, Double)>(symbol: 'FlutterSetWindowSize')
  external static void _setWindowSize(Pointer<Void> windowHandle, double width, double height);

  @Native<Void Function(Pointer<Void>, Pointer<ffi.Utf8>)>(symbol: 'FlutterSetWindowTitle')
  external static void _setWindowTitle(Pointer<Void> windowHandle, Pointer<ffi.Utf8> title);

  @Native<Int64 Function(Pointer<Void>)>(symbol: 'FlutterGetWindowState')
  external static int _getWindowState(Pointer<Void> windowHandle);

  @Native<Void Function(Pointer<Void>, Int64)>(symbol: 'FlutterSetWindowState')
  external static void _setWindowState(Pointer<Void> windowHandle, int state);
}

final class _WindowCreationRequest extends Struct {
  @Double()
  external double width;

  @Double()
  external double height;

  @Double()
  external double minWidth;

  @Double()
  external double minHeight;

  @Double()
  external double maxWidth;

  @Double()
  external double maxHeight;

  external Pointer<NativeFunction<Void Function()>> onClose;
  external Pointer<NativeFunction<Void Function()>> onSizeChange;
}

final class _Size extends Struct {
  @Double()
  external double width;

  @Double()
  external double height;
}
