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
    final int hwnd = _createHwnd(contentSize);

    // Create the corresponding view
    final Pointer<_WindowCreationRequest> request = ffi.calloc<_WindowCreationRequest>();
    request.ref
      ..contentSize.set(contentSize)
      ..hwnd = hwnd;

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
    required Size? clientSize,
    Size? minSize,
    Size? maxSize,
    required int windowStyle,
    required int extendedWindowStyle,
    required int hwnd,
  }) {
    if (clientSize == null) {
      return null;
    }

    Size clampToVirtualScreen(Size size) {
      return size;
    }

    final int Function(Pointer<_RECT>, int, int, int, int) _adjustWindowRectExForDpi = user32
        .lookupFunction<_AdjustWindowRectExForDpiC, _AdjustWindowRectExForDpiDart>(
          'AdjustWindowRectExForDpi',
        );

    final dpi = 96; // TODO: Get this
    final scaleFactor = 1.0; // TODO: Get this

    final rectPtr = ffi.calloc<_RECT>();
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

  int _createHwnd(WindowSizing contentSize) {
    final Size? size = getWindowSizeForClientSize(
      clientSize: contentSize.preferredSize,
      windowStyle: _WindowStyle.overlappedWindow.value,
      extendedWindowStyle: 0,
      hwnd: 0,
    );

    final _CreateWindowExWDart createWindowExW = user32
        .lookupFunction<_CreateWindowExWNative, _CreateWindowExWDart>('CreateWindowExW');

    final Pointer<ffi.Utf16> className = 'FLUTTER_HOST_WINDOW'.toNativeUtf16();
    _registerWindowClass(className, IDI_APPLICATION);
    final Pointer<ffi.Utf16> windowName = ''.toNativeUtf16();
    final int hwnd = createWindowExW(
      0, // dwExStyle
      className, // lpClassName
      windowName, // lpWindowName
      _WindowStyle.overlappedWindow.value, // dwStyle (e.g. WS_OVERLAPPEDWINDOW)
      _CW_USEDEFAULT, // x
      _CW_USEDEFAULT, // y
      size?.width.toInt() ?? _CW_USEDEFAULT, // nWidth
      size?.height.toInt() ?? _CW_USEDEFAULT, // nHeight
      0, // hWndParent
      0, // hMenu
      0, // hInstance
      nullptr, // lpParam
    );

    ffi.calloc.free(className);
    ffi.calloc.free(windowName);
    return hwnd;
  }

  bool _isClassRegistered(Pointer<ffi.Utf16> className) {
    final GetModuleHandleWDart GetModuleHandle = _kernel32
      .lookupFunction<GetModuleHandleWNative, GetModuleHandleWDart>('GetModuleHandleW');

    final GetClassInfoExW = user32.lookupFunction<GetClassInfoExWNative, GetClassInfoExWDart>('GetClassInfoExW');

    final Pointer<_WNDCLASSEX> wndClass = ffi.calloc<_WNDCLASSEX>();
    wndClass.ref.cbSize = sizeOf<_WNDCLASSEX>();
    final hInstance = GetModuleHandle(nullptr);
    final int result = GetClassInfoExW(hInstance, className, wndClass);
    ffi.calloc.free(wndClass);
    return result != 0;
  }

  void _registerWindowClass(Pointer<ffi.Utf16> className, int idiAppIcon) {
    if (_isClassRegistered(className)) {
      return;
    }

    final LoadIconW = user32
      .lookupFunction<LoadIconWNative, LoadIconWDart>('LoadIconW');

    final loadCursorW = user32
      .lookupFunction<LoadCursorWNative, LoadCursorWDart>('LoadCursorW');

    final int Function(Pointer<_WNDCLASSEX>) RegisterClassEx = user32
        .lookupFunction<Uint16 Function(Pointer<_WNDCLASSEX>), int Function(Pointer<_WNDCLASSEX>)>(
          'RegisterClassExW',
        );

    final GetModuleHandleWDart GetModuleHandle = _kernel32
      .lookupFunction<GetModuleHandleWNative, GetModuleHandleWDart>('GetModuleHandleW');

    final Pointer<_WNDCLASSEX> wndClass = ffi.calloc<_WNDCLASSEX>();
    final hInstance = GetModuleHandle(nullptr);
    final Pointer<NativeFunction<IntPtr Function(IntPtr, Uint32, IntPtr, IntPtr)>> wndProcPointer =
        Pointer.fromFunction<_WndProcNative>(wndProc, 0);

    final icon = LoadIconW(hInstance, MAKEINTRESOURCE(idiAppIcon));
    final cursor = loadCursorW(nullptr, MAKEINTRESOURCE(IDC_HAND));

    wndClass.ref
      ..cbSize = sizeOf<_WNDCLASSEX>()
      ..style = CS_HREDRAW | CS_VREDRAW
      ..lpfnWndProc = wndProcPointer.cast()
      ..hInstance = hInstance
      ..hIcon = icon
      ..hCursor = cursor
      ..lpszClassName = className;

    if (wndClass.ref.hIcon == 0) {
      wndClass.ref.hIcon = LoadIconW(nullptr, MAKEINTRESOURCE(0x7F00)); // IDI_APPLICATION
    }

    final atom = RegisterClassEx(wndClass);
    if (atom == 0) {
      print('RegisterClassEx failed with error: ${getLastErrorAsString()}');
    }

    ffi.calloc.free(wndClass);
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
    _showWindow(getWindowHandle(), _SW_RESTORE);
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
      _showWindow(getWindowHandle(), _SW_MINIMIZE);
    } else {
      _showWindow(getWindowHandle(), _SW_RESTORE);
    }
  }

  @override
  void setMaximized(bool maximized) {
    _ensureNotDestroyed();
    if (maximized) {
      _showWindow(getWindowHandle(), _SW_MAXIMIZE);
    } else {
      _showWindow(getWindowHandle(), _SW_RESTORE);
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

  // Callback function
  static int wndProc(int hwnd, int msg, int wParam, int lParam) {
    final DefWindowProc = user32.lookupFunction<
      IntPtr Function(IntPtr hwnd, Uint32 msg, IntPtr wParam, IntPtr lParam),
      int Function(int hwnd, int msg, int wParam, int lParam)
    >('Def_WindowProcW');

    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  String getLastErrorAsString() {
    final GetLastErrorDart _GetLastError = _kernel32
      .lookupFunction<GetLastErrorC, GetLastErrorDart>('GetLastError');

    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessagew
    final _FormatMessageW = _kernel32
        .lookupFunction<FormatMessageW_C, FormatMessageW_Dart>('FormatMessageW');

    // https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
    final _WideCharToMultiByte = _kernel32
        .lookupFunction<WideCharToMultiByteC, WideCharToMultiByteDart>(
      'WideCharToMultiByte',
    );

    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-localfree
    final _LocalFree = _kernel32
      .lookupFunction<_LocalFreeC, LocalFreeDart>('LocalFree');
            
    final errorCode = _GetLastError();

    final lpBuffer = ffi.calloc<Pointer<ffi.Utf16>>();

    const formatFlags = 0x00000100 | // FORMAT_MESSAGE_ALLOCATE_BUFFER
        0x00001000 | // FORMAT_MESSAGE_FROM_SYSTEM
        0x00000200; // FORMAT_MESSAGE_IGNORE_INSERTS

    final result = _FormatMessageW(
      formatFlags,
      nullptr,
      errorCode,
      (0x00 << 10) | 0x01, // MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)
      lpBuffer,
      0,
      nullptr,
    );

    if (result != 0) {
      final messagePtr = lpBuffer.value;
      final wideStr = messagePtr.cast<ffi.Utf16>();

      // First, get required UTF-8 buffer size
      final utf8Len = _WideCharToMultiByte(65001, 0, wideStr, -1, nullptr, 0,
          nullptr, nullptr); // 65001 = CP_UTF8

      if (utf8Len > 0) {
        final utf8Buffer = ffi.calloc<Uint8>(utf8Len);
        _WideCharToMultiByte(65001, 0, wideStr, -1, utf8Buffer.cast(), utf8Len,
            nullptr, nullptr);

        final dartString = utf8Buffer.cast<ffi.Utf8>().toDartString();
        ffi.calloc.free(utf8Buffer);
        _LocalFree(messagePtr.cast());
        ffi.calloc.free(lpBuffer);
        return dartString.trim();
      }

      _LocalFree(messagePtr.cast());
    }

    ffi.calloc.free(lpBuffer);
    return 'FormatMessage failed with 0x${errorCode.toRadixString(16).padLeft(8, '0')}';
  }

  static const int _WM_SIZE = 0x0005;
  static const int _WM_CLOSE = 0x0010;

  static const int _SW_RESTORE = 9;
  static const int _SW_MAXIMIZE = 3;
  static const int _SW_MINIMIZE = 6;

  static const int _CW_USEDEFAULT = -2147483648;
  static int CS_HREDRAW = 0x0002;
  static int CS_VREDRAW = 0x0001;

  final WindowingOwnerWin32 _owner;
  static DynamicLibrary _kernel32 = DynamicLibrary.open('kernel32.dll');
  static DynamicLibrary user32 = DynamicLibrary.open('user32.dll');
  final IDC_ARROW = 0x7F00.toRadixString(16).padLeft(4, '0'); // "#32512"
  static int IDI_APPLICATION = 0x7F00;

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

  @Int64()
  external int hwnd;
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

/// https://learn.microsoft.com/en-us/windows/win32/winmsg/window-styles
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

// Signature of AdjustWindowRectExForDpi
typedef _AdjustWindowRectExForDpiC =
    Int32 Function(Pointer<_RECT> rect, Uint32 dwStyle, Int32 bMenu, Uint32 dwExStyle, Uint32 dpi);
typedef _AdjustWindowRectExForDpiDart =
    int Function(Pointer<_RECT> rect, int dwStyle, int bMenu, int dwExStyle, int dpi);

/// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexw
typedef _CreateWindowExWNative =
    IntPtr Function(
      Uint32 dwExStyle,
      Pointer<ffi.Utf16> lpClassName,
      Pointer<ffi.Utf16> lpWindowName,
      Uint32 dwStyle,
      Int32 x,
      Int32 y,
      Int32 nWidth,
      Int32 nHeight,
      IntPtr hWndParent,
      IntPtr hMenu,
      IntPtr hInstance,
      Pointer<Void> lpParam,
    );

/// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexw
typedef _CreateWindowExWDart =
    int Function(
      int dwExStyle,
      Pointer<ffi.Utf16> lpClassName,
      Pointer<ffi.Utf16> lpWindowName,
      int dwStyle,
      int x,
      int y,
      int nWidth,
      int nHeight,
      int hWndParent,
      int hMenu,
      int hInstance,
      Pointer<Void> lpParam,
    );

typedef _WndProcNative = IntPtr Function(IntPtr hwnd, Uint32 msg, IntPtr wParam, IntPtr lParam);

typedef _WindowProc = int Function(int hwnd, int msg, int wParam, int lParam);

final class _CREATESTRUCT extends Struct {
  @IntPtr()
  external int lpCreateParams;

  @IntPtr()
  external int hInstance;

  @IntPtr()
  external int hMenu;

  @IntPtr()
  external int hwndParent;

  @Int32()
  external int cy;

  @Int32()
  external int cx;

  @Int32()
  external int y;

  @Int32()
  external int x;

  @Int32()
  external int style;

  external Pointer<ffi.Utf16> lpszName;
  external Pointer<ffi.Utf16> lpszClass;

  @Uint32()
  external int dwExStyle;
}

Pointer<ffi.Utf16> MAKEINTRESOURCE(int id) {
  // According to Windows API docs, MAKEINTRESOURCE macro just casts the integer
  return Pointer<ffi.Utf16>.fromAddress(id);
}
// Define _RECT
final class _RECT extends Struct {
  @Int32()
  external int left;
  @Int32()
  external int top;
  @Int32()
  external int right;
  @Int32()
  external int bottom;
}

 typedef FormatMessageW_C = Uint32 Function(
    Uint32 dwFlags,
    Pointer<Void> lpSource,
    Uint32 dwMessageId,
    Uint32 dwLanguageId,
    Pointer<Pointer<ffi.Utf16>> lpBuffer,
    Uint32 nSize,
    Pointer<Void> Arguments,
  );

  typedef FormatMessageW_Dart = int Function(
    int dwFlags,
    Pointer<Void> lpSource,
    int dwMessageId,
    int dwLanguageId,
    Pointer<Pointer<ffi.Utf16>> lpBuffer,
    int nSize,
    Pointer<Void> Arguments,
  );

typedef WideCharToMultiByteC = Int32 Function(
  Uint32 codePage,
  Uint32 dwFlags,
  Pointer<ffi.Utf16> lpWideCharStr,
  Int32 cchWideChar,
  Pointer<ffi.Utf8> lpMultiByteStr,
  Int32 cbMultiByte,
  Pointer<ffi.Utf8> lpDefaultChar,
  Pointer<Bool> lpUsedDefaultChar,
);

typedef WideCharToMultiByteDart = int Function(
  int codePage,
  int dwFlags,
  Pointer<ffi.Utf16> lpWideCharStr,
  int cchWideChar,
  Pointer<ffi.Utf8> lpMultiByteStr,
  int cbMultiByte,
  Pointer<ffi.Utf8> lpDefaultChar,
  Pointer<Bool> lpUsedDefaultChar,
);

/// C function signature: HLOCAL LocalFree(HLOCAL hMem);
typedef _LocalFreeC = Pointer<Void> Function(Pointer<Void> hMem);

/// Dart function signature
typedef LocalFreeDart = Pointer<Void> Function(Pointer<Void> hMem);

typedef GetLastErrorC = Uint32 Function();
typedef GetLastErrorDart = int Function();
typedef GetModuleHandleWNative = Pointer<Void> Function(Pointer<ffi.Utf16> lpModuleName);
typedef GetModuleHandleWDart = Pointer<Void> Function(Pointer<ffi.Utf16> lpModuleName);

typedef UINT = Uint32;
typedef HWND = IntPtr;
typedef HINSTANCE = IntPtr;
typedef HICON = IntPtr;
typedef HCURSOR = IntPtr;
typedef HBRUSH = IntPtr;
typedef WNDPROC = Pointer<NativeFunction<IntPtr Function(IntPtr, Uint32, IntPtr, IntPtr)>>;

final class _WNDCLASSEX extends Struct {
  @Uint32()
  external int cbSize;

  @Uint32()
  external int style;

  external Pointer<NativeFunction<IntPtr Function(IntPtr, Uint32, IntPtr, IntPtr)>> lpfnWndProc;

  @Int32()
  external int cbClsExtra;

  @Int32()
  external int cbWndExtra;

  external Pointer<Void> hInstance;

  external Pointer<Void> hIcon;

  external Pointer<Void> hCursor;

  external Pointer<Void> hbrBackground;

  external Pointer<ffi.Utf16> lpszMenuName;

  external Pointer<ffi.Utf16> lpszClassName;

  external Pointer<Void> hIconSm;
}

typedef GetClassInfoExWNative = Int32 Function(
    Pointer<Void> hInstance,
    Pointer<ffi.Utf16> lpszClass,
    Pointer<_WNDCLASSEX> lpwcx);

typedef GetClassInfoExWDart = int Function(
    Pointer<Void> hInstance,
    Pointer<ffi.Utf16> lpszClass,
    Pointer<_WNDCLASSEX> lpwcx);

typedef LoadIconWNative = Pointer<Void> Function(
  Pointer<Void> hInstance,
  Pointer<ffi.Utf16> lpIconName,
);
typedef LoadIconWDart = Pointer<Void> Function(
  Pointer<Void> hInstance,
  Pointer<ffi.Utf16> lpIconName,
);

const int IDC_ARROW = 32512;
const int IDC_IBEAM = 32513;
const int IDC_WAIT = 32514;
const int IDC_CROSS = 32515;
const int IDC_UPARROW = 32516;
const int IDC_SIZE = 32640;
const int IDC_ICON = 32641;
const int IDC_SIZENWSE = 32642;
const int IDC_SIZENESW = 32643;
const int IDC_SIZEWE = 32644;
const int IDC_SIZENS = 32645;
const int IDC_SIZEALL = 32646;
const int IDC_NO = 32648;
const int IDC_HAND = 32649;
const int IDC_APPSTARTING = 32650;
const int IDC_HELP = 32651;

typedef LoadCursorWNative = Pointer<Void> Function(Pointer<Void> hInstance, Pointer<ffi.Utf16> lpCursorName);
typedef LoadCursorWDart = Pointer<Void> Function(Pointer<Void> hInstance, Pointer<ffi.Utf16> lpCursorName);

