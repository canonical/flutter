// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui' show FlutterView;

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// Defines the anchor point for the anchor rectangle or child [Window] when
/// positioning a [Window]. The specified anchor is used to derive an anchor
/// point on the anchor rectangle that the anchor point for the child [Window]
/// will be positioned relative to. If a corner anchor is set (e.g. [topLeft]
/// or [bottomRight]), the anchor point will be at the specified corner;
/// otherwise, the derived anchor point will be centered on the specified edge,
/// or in the center of the anchor rectangle if no edge is specified.
enum WindowPositionerAnchor {
  /// If the [WindowPositioner.parentAnchor] is set to [center], then the
  /// child [Window] will be positioned relative to the center
  /// of the parent [Window].
  ///
  /// If [WindowPositioner.childAnchor] is set to  [center], then the middle
  /// of the child [Window] will be positioned relative to
  /// [WindowPositioner.parentAnchor].
  center,

  /// If the [WindowPositioner.parentAnchor] is set to [top], then the
  /// child [Window] will be positioned relative to the top
  /// of the parent [Window].
  ///
  /// If [WindowPositioner.childAnchor] is set to  [top], then the top
  /// of the child [Window] will be positioned relative to
  /// [WindowPositioner.parentAnchor].
  top,

  /// If the [WindowPositioner.parentAnchor] is set to [bottom], then the
  /// child [Window] will be positioned relative to the bottom
  /// of the parent [Window].
  ///
  /// If [WindowPositioner.childAnchor] is set to  [bottom], then the bottom
  /// of the child [Window] will be positioned relative to
  /// [WindowPositioner.parentAnchor].
  bottom,

  /// If the [WindowPositioner.parentAnchor] is set to [left], then the
  /// child [Window] will be positioned relative to the left
  /// of the parent [Window].
  ///
  /// If [WindowPositioner.childAnchor] is set to  [left], then the left
  /// of the child [Window] will be positioned relative to
  /// [WindowPositioner.parentAnchor].
  left,

  /// If the [WindowPositioner.parentAnchor] is set to [right], then the
  /// child [Window] will be positioned relative to the right
  /// of the parent [Window].
  ///
  /// If [WindowPositioner.childAnchor] is set to  [right], then the right
  /// of the child [Window] will be positioned relative to
  /// [WindowPositioner.parentAnchor].
  right,

  /// If the [WindowPositioner.parentAnchor] is set to [topLeft], then the
  /// child [Window] will be positioned relative to the top left
  /// of the parent [Window].
  ///
  /// If [WindowPositioner.childAnchor] is set to  [topLeft], then the top left
  /// of the child [Window] will be positioned relative to
  /// [WindowPositioner.parentAnchor].
  topLeft,

  /// If the [WindowPositioner.parentAnchor] is set to [bottomLeft], then the
  /// child [Window] will be positioned relative to the bottom left
  /// of the parent [Window].
  ///
  /// If [WindowPositioner.childAnchor] is set to  [bottomLeft], then the bottom left
  /// of the child [Window] will be positioned relative to
  /// [WindowPositioner.parentAnchor].
  bottomLeft,

  /// If the [WindowPositioner.parentAnchor] is set to [topRight], then the
  /// child [Window] will be positioned relative to the top right
  /// of the parent [Window].
  ///
  /// If [WindowPositioner.childAnchor] is set to  [topRight], then the top right
  /// of the child [Window] will be positioned relative to
  /// [WindowPositioner.parentAnchor].
  topRight,

  /// If the [WindowPositioner.parentAnchor] is set to [bottomRight], then the
  /// child [Window] will be positioned relative to the bottom right
  /// of the parent [Window].
  ///
  /// If [WindowPositioner.childAnchor] is set to  [bottomRight], then the bottom right
  /// of the child [Window] will be positioned relative to
  /// [WindowPositioner.parentAnchor].
  bottomRight,
}

/// The [WindowPositionerConstraintAdjustment] value defines the ways in which
/// Flutter will adjust the position of the [Window], if the unadjusted position would result
/// in the surface being partly constrained.
///
/// Whether a [Window] is considered 'constrained' is left to the platform
/// to determine. For example, the surface may be partly outside the
/// compositor's defined 'work area', thus necessitating the child [Window]'s
/// position be adjusted until it is entirely inside the work area.
///
/// 'Flip' means reverse the anchor points and offset along an axis.
/// 'Slide' means adjust the offset along an axis.
/// 'Resize' means adjust the client [Window] size along an axis.
///
/// The adjustments can be combined, according to a defined precedence: 1)
/// Flip, 2) Slide, 3) Resize.
enum WindowPositionerConstraintAdjustment {
  /// If [slideX] is specified in [WindowPositioner.constraintAdjustment]
  /// and the [Window] would be displayed off the screen in the X-axis, then it will be
  /// translated in the X-direction (either negative or positive) in order
  /// to best display the window on screen.
  slideX,

  /// If [slideY] is specified in [WindowPositioner.constraintAdjustment]
  /// and the [Window] would be displayed off the screen in the Y-axis, then it will be
  /// translated in the Y-direction (either negative or positive) in order
  /// to best display the window on screen.
  slideY,

  /// If [flipX] is specified in [WindowPositioner.constraintAdjustment]
  /// and the [Window] would be displayed off the screen in the X-axis in one direction, then
  /// it will be flipped to the opposite side of its parent in order to show
  /// to best display the window on screen.
  flipX,

  /// If [flipY] is specified in [WindowPositioner.constraintAdjustment]
  /// and then [Window] would be displayed off the screen in the Y-axis in one direction, then
  /// it will be flipped to the opposite side of its parent in order to show
  /// it on screen.
  flipY,

  /// If [resizeX] is specified in [WindowPositioner.constraintAdjustment]
  /// and the [Window] would be displayed off the screen in the X-axis, then
  /// its width will be reduced such that it fits on screen.
  resizeX,

  /// If [resizeY] is specified in [WindowPositioner.constraintAdjustment]
  /// and the [Window] would be displayed off the screen in the Y-axis, then
  /// its height will be reduced such that it fits on screen.
  resizeY,
}

/// Defines the type of a [Window]
enum WindowArchetype {
  /// Defines a standard [Window]
  regular,

  /// Defines a [Window] that is on a layer above [regular] [Window]s and is not dockable
  floatingRegular,

  /// Defines a dialog [Window]
  dialog,

  /// Defines a satellite [Window]
  satellite,

  /// Defines a popup [Window]
  popup,

  /// Defines a tooltip
  tip,
}

/// The [WindowPositioner] provides a collection of rules for the placement
/// of a child [Window] relative to a parent [Window]. Rules can be defined to ensure
/// the child [Window] remains within the visible area's borders, and to
/// specify how the child [Window] changes its position, such as sliding along
/// an axis, or flipping around a rectangle.
class WindowPositioner {
  /// Const constructor for [WindowPositioner].
  const WindowPositioner({
    this.parentAnchor = WindowPositionerAnchor.center,
    this.childAnchor = WindowPositionerAnchor.center,
    this.offset = Offset.zero,
    this.constraintAdjustment = const <WindowPositionerConstraintAdjustment>{},
  });

  /// Copy a [WindowPositioner] with some fields replaced.`
  WindowPositioner copyWith({
    WindowPositionerAnchor? parentAnchor,
    WindowPositionerAnchor? childAnchor,
    Offset? offset,
    Set<WindowPositionerConstraintAdjustment>? constraintAdjustment,
  }) {
    return WindowPositioner(
      parentAnchor: parentAnchor ?? this.parentAnchor,
      childAnchor: childAnchor ?? this.childAnchor,
      offset: offset ?? this.offset,
      constraintAdjustment: constraintAdjustment ?? this.constraintAdjustment,
    );
  }

  /// Defines the anchor point for the anchor rectangle. The specified anchor
  /// is used to derive an anchor point that the child [Window] will be
  /// positioned relative to. If a corner anchor is set (e.g. [topLeft] or
  /// [bottomRight]), the anchor point will be at the specified corner;
  /// otherwise, the derived anchor point will be centered on the specified
  /// edge, or in the center of the anchor rectangle if no edge is specified.
  final WindowPositionerAnchor parentAnchor;

  /// Defines the anchor point for the child [Window]. The specified anchor
  /// is used to derive an anchor point that will be positioned relative to the
  /// parentAnchor. If a corner anchor is set (e.g. [topLeft] or
  /// [bottomRight]), the anchor point will be at the specified corner;
  /// otherwise, the derived anchor point will be centered on the specified
  /// edge, or in the center of the anchor rectangle if no edge is specified.
  final WindowPositionerAnchor childAnchor;

  /// Specify the [Window] position offset relative to the position of the
  /// anchor on the anchor rectangle and the anchor on the child. For
  /// example if the anchor of the anchor rectangle is at (x, y), the [Window]
  /// has the child_anchor [topLeft], and the offset is (ox, oy), the calculated
  /// [Window] position will be (x + ox, y + oy). The offset position of the
  /// [Window] is the one used for constraint testing. See constraintAdjustment.
  ///
  /// An example use case is placing a popup menu on top of a user interface
  /// element, while aligning the user interface element of the parent [Window]
  /// with some user interface element placed somewhere in the popup [Window].
  final Offset offset;

  /// The constraintAdjustment value define ways Flutter will adjust
  /// the position of the [Window], if the unadjusted position would result
  /// in the surface being partly constrained.
  ///
  /// Whether a [Window] is considered 'constrained' is left to the platform
  /// to determine. For example, the surface may be partly outside the
  /// output's 'work area', thus necessitating the child [Window]'s
  /// position be adjusted until it is entirely inside the work area.
  ///
  /// The adjustments can be combined, according to a defined precedence: 1)
  /// Flip, 2) Slide, 3) Resize.
  final Set<WindowPositionerConstraintAdjustment> constraintAdjustment;
}

/// Defines a [Window] created by the application. To use [Window]s, you must wrap
/// your application in the [MultiWindowApp] widget New [Window]s are created via
/// global functions like [createRegularWindow] and [createPopupWindow].
class Window {
  /// [view] the underlying [FlutterView]
  /// [builder] render function containing the content of this [Window]
  /// [archetype] the archetype of the [Window]
  /// [size] initial [Size] of the [Window]
  /// [parentViewId] view ID of the parent of this [Window] if any
  Window(
      {required this.view,
      required this.builder,
      required this.archetype,
      required this.size,
      required this.parentViewId});

  /// The underlying [FlutterView] associated with this [Window]
  final FlutterView view;

  /// The render function containing the content of this [Window]
  final Widget Function(BuildContext context) builder;

  /// Defines the archetype of the [Window]
  WindowArchetype? archetype;

  /// The current [Size] of the [Window]
  Size? size;

  /// The view ID of the parent of this [Window] if any
  int? parentViewId;
}

/// Creates a new regular [Window].
///
/// [context] the current [BuildContext], which must include a [MultiWindowAppContext]
/// [size] the size of the new [Window] in pixels
/// [builder] a builder function that returns the contents of the new [Window]
Future<Window> createRegularWindow(
    {required BuildContext context,
    required Size size,
    required WidgetBuilder builder}) async {
  final MultiWindowAppContext? multiViewAppContext =
      MultiWindowAppContext.of(context);
  if (multiViewAppContext == null) {
    throw Exception(
        'Cannot create a window: your application does not use MultiViewApp. Try wrapping your toplevel application in a MultiViewApp widget');
  }

  return multiViewAppContext.windowController
      .createRegularWindow(size: size, builder: builder);
}

/// Creates a new popup [Window]
///
/// [context] the current [BuildContext], which must include a [MultiWindowAppContext]
/// [parent] the [Window] to which this popup is associated
/// [size] the [Size] of the popup
/// [anchorRect] the [Rect] to which this popup is anchored
/// [positioner] defines the constraints by which the popup is positioned
/// [builder] a builder function that returns the contents of the new [Window]
Future<Window> createPopupWindow(
    {required BuildContext context,
    required Window parent,
    required Size size,
    required Rect anchorRect,
    required WindowPositioner positioner,
    required WidgetBuilder builder}) async {
  final MultiWindowAppContext? multiViewAppContext =
      MultiWindowAppContext.of(context);
  if (multiViewAppContext == null) {
    throw Exception(
        'Cannot create a window: your application does not use MultiViewApp. Try wrapping your toplevel application in a MultiViewApp widget');
  }

  return multiViewAppContext.windowController.createPopupWindow(
      parent: parent,
      size: size,
      anchorRect: anchorRect,
      positioner: positioner,
      builder: builder);
}

/// Destroys the provided [Window]
///
/// [context] the current [BuildContext], which must include a [MultiWindowAppContext]
/// [window] the [Window] to be destroyed
Future<void> destroyWindow(BuildContext context, Window window) async {
  final MultiWindowAppContext? multiViewAppContext =
      MultiWindowAppContext.of(context);
  if (multiViewAppContext == null) {
    throw Exception(
        'Cannot create a window: your application does not use MultiViewApp. Try wrapping your toplevel application in a MultiViewApp widget');
  }

  return multiViewAppContext.windowController.destroyWindow(window);
}

/// Declares that an application will create multiple [Window]s.
/// The current [Window] can be looked up with [WindowContext.of].
class MultiWindowApp extends StatefulWidget {
  /// [initialWindows] A list of [Function]s to create [Window]s that will be run as soon as the app starts.
  const MultiWindowApp({super.key, this.initialWindows});

  /// A list of [Function]s to create [Window]s that will be run as soon as the app starts.
  final List<Future<Window> Function(BuildContext)>? initialWindows;

  @override
  State<MultiWindowApp> createState() => WindowController();
}

/// Provides methods to create, update, and delete [Window]s. It is preferred that
/// you use the global functions like [createRegularWindow] and [destroyWindow] over
/// accessing the [WindowController] directly.
class WindowController extends State<MultiWindowApp> {
  Map<int, Window> _windows = <int, Window>{};
  final MethodChannel _channel = const MethodChannel('flutter/windowing');

  @override
  void initState() {
    super.initState();
    _channel.setMethodCallHandler(_methodCallHandler);
  }

  Future<void> _methodCallHandler(MethodCall call) async {
    final Map<Object?, Object?> arguments =
        call.arguments as Map<Object?, Object?>;

    switch (call.method) {
      case 'onWindowResized':
        final int viewId = arguments['viewId'] as int;
        final int width = arguments['width'] as int;
        final int height = arguments['height'] as int;
        final Size size = Size(width.toDouble(), height.toDouble());

        setState(() {
          final Window? viewData = _windows[viewId];
          if (viewData != null) {
            viewData.size = size;
            final Map<int, Window> copy = Map<int, Window>.from(_windows);
            copy[viewId] = viewData;
            _windows = copy;
          }
        });
      case 'onWindowDestroyed':
        final int viewId = arguments['viewId'] as int;
        _remove(viewId);
    }
  }

  Future<Window> _createWindow(
      {required Future<Map<Object?, Object?>> Function(MethodChannel channel)
          viewBuilder,
      required WidgetBuilder builder}) async {
    final Map<Object?, Object?> creationData = await viewBuilder(_channel);
    final int viewId = creationData['viewId'] as int;
    final WindowArchetype archetype =
        WindowArchetype.values[creationData['archetype'] as int];
    final int width = creationData['width'] as int;
    final int height = creationData['height'] as int;
    final int? parentViewId = creationData['parentViewId'] as int?;

    final FlutterView flView =
        WidgetsBinding.instance.platformDispatcher.views.firstWhere(
      (FlutterView view) => view.viewId == viewId,
      orElse: () {
        throw Exception('No matching view found for viewId: $viewId');
      },
    );

    final Window window = Window(
        view: flView,
        builder: builder,
        archetype: archetype,
        size: Size(width.toDouble(), height.toDouble()),
        parentViewId: parentViewId);
    _add(window);
    return window;
  }

  /// Creates a new regular [Window].
  ///
  /// [size] the size of the new [Window] in pixels
  /// [builder] a builder function that returns the contents of the new [Window]
  Future<Window> createRegularWindow(
      {required Size size, required WidgetBuilder builder}) {
    int clampToZeroInt(double value) => value < 0 ? 0 : value.toInt();
    final int width = clampToZeroInt(size.width);
    final int height = clampToZeroInt(size.height);

    return _createWindow(
        viewBuilder: (MethodChannel channel) async {
          return await channel.invokeMethod('createRegularWindow',
                  <String, int>{'width': width, 'height': height})
              as Map<Object?, Object?>;
        },
        builder: builder);
  }

  /// Creates a new popup [Window]
  ///
  /// [parent] the [Window] to which this popup is associated
  /// [size] the [Size] of the popup
  /// [anchorRect] the [Rect] to which this popup is anchored
  /// [positioner] defines the constraints by which the popup is positioned
  /// [builder] a builder function that returns the contents of the new [Window]
  Future<Window> createPopupWindow(
      {required Window parent,
      required Size size,
      required Rect anchorRect,
      required WindowPositioner positioner,
      required WidgetBuilder builder}) async {
    int clampToZeroInt(double value) => value < 0 ? 0 : value.toInt();
    int constraintAdjustmentBitmask = 0;
    for (final WindowPositionerConstraintAdjustment adjustment
        in positioner.constraintAdjustment) {
      constraintAdjustmentBitmask |= 1 << adjustment.index;
    }

    return _createWindow(
        viewBuilder: (MethodChannel channel) async {
          return await channel
              .invokeMethod('createPopupWindow', <String, dynamic>{
            'parent': parent.view.viewId,
            'size': <int>[
              clampToZeroInt(size.width),
              clampToZeroInt(size.height)
            ],
            'anchorRect': <int>[
              anchorRect.left.toInt(),
              anchorRect.top.toInt(),
              anchorRect.width.toInt(),
              anchorRect.height.toInt()
            ],
            'positionerParentAnchor': positioner.parentAnchor.index,
            'positionerChildAnchor': positioner.childAnchor.index,
            'positionerOffset': <int>[
              positioner.offset.dx.toInt(),
              positioner.offset.dy.toInt()
            ],
            'positionerConstraintAdjustment': constraintAdjustmentBitmask
          }) as Map<Object?, Object?>;
        },
        builder: builder);
  }

  /// Destroys the provided [Window]
  ///
  /// [window] the [Window] to be destroyed
  Future<void> destroyWindow(Window window) async {
    try {
      await _channel.invokeMethod('destroyWindow', <int>[window.view.viewId]);
      _remove(window.view.viewId);
    } on PlatformException catch (e) {
      throw ArgumentError(
          'Unable to delete window with view_id=${window.view.viewId}. Does the window exist? Error: $e');
    }
  }

  void _add(Window window) {
    setState(() {
      final Map<int, Window> copy = Map<int, Window>.from(_windows);
      copy[window.view.viewId] = window;
      _windows = copy;
    });
  }

  void _remove(int viewId) {
    setState(() {
      final Map<int, Window> copy = Map<int, Window>.from(_windows);
      copy.removeWhere((int key, Window data) => data.view.viewId == viewId);
      _windows = copy;
    });
  }

  @override
  Widget build(BuildContext context) {
    return MultiWindowAppContext(
        windows: _windows,
        windowController: this,
        child: _MultiWindowAppView(
            initialWindows: widget.initialWindows, windows: _windows));
  }
}

/// Provides access to a mapping from [int] identifiers to [Window]s.
/// Users may provide the identifier of a [View] to look up a particular
/// [Window] if any exists.
///
/// This class also provides access to the [WindowController] which is
/// used internally to provide access to create, update, and delete methods
/// on the windowing system.
class MultiWindowAppContext extends InheritedWidget {
  /// [windows] a mapping from the [int] [View] identifier to its associated [Window]
  /// [windowController] the [WindowController] active in this context
  const MultiWindowAppContext(
      {super.key,
      required super.child,
      required this.windows,
      required this.windowController});

  /// A mapping from the [int] [View] identifier to its associated [Window].
  final Map<int, Window> windows;

  /// The [WindowController] active in this context
  final WindowController windowController;

  /// Returns the [MultiWindowAppContext] if any
  static MultiWindowAppContext? of(BuildContext context) {
    return context.dependOnInheritedWidgetOfExactType<MultiWindowAppContext>();
  }

  @override
  bool updateShouldNotify(MultiWindowAppContext oldWidget) {
    return windows != oldWidget.windows ||
        windowController != oldWidget.windowController;
  }
}

class _MultiWindowAppView extends StatefulWidget {
  const _MultiWindowAppView(
      {required this.initialWindows, required this.windows});

  final List<Future<Window> Function(BuildContext)>? initialWindows;
  final Map<int, Window> windows;

  @override
  State<StatefulWidget> createState() => _MultiWindowAppViewState();
}

class _MultiWindowAppViewState extends State<_MultiWindowAppView> {
  @override
  void initState() {
    super.initState();
    SchedulerBinding.instance.addPostFrameCallback((_) async {
      if (widget.initialWindows != null) {
        for (final Future<Window> Function(BuildContext) window
            in widget.initialWindows!) {
          await window(context);
        }
      }
    });
  }

  Widget? _buildTree(FlutterView? parentView) {
    final List<Window> children = widget.windows.values
        .where((Window window) => window.parentViewId == parentView?.viewId)
        .toList();

    if (children.isEmpty) {
      return null;
    }

    Widget getChild(Window viewData) =>
        _buildTree(viewData.view) ?? viewData.builder(context);

    if (parentView == null) {
      if (children.length == 1) {
        return children.first.archetype != null
            ? WindowContext(
                window: children.first,
                child: View(
                  view: children.first.view,
                  child: getChild(children.first),
                ))
            : null;
      } else {
        final List<Widget> widgets = <Widget>[];
        for (final Window window in children
            .where((Window otherWindow) => otherWindow.archetype != null)) {
          widgets.add(WindowContext(
              window: window,
              child: View(
                view: window.view,
                child: getChild(window),
              )));
        }
        return widgets.isNotEmpty ? ViewCollection(views: widgets) : null;
      }
    } else {
      if (children.length == 1) {
        return children.first.archetype != null
            ? WindowContext(
                window: children.first,
                child: ViewAnchor(
                  view: View(
                    view: children.first.view,
                    child: getChild(children.first),
                  ),
                  child: widget.windows.values
                      .where((Window otherWindow) =>
                          otherWindow.view == parentView)
                      .single
                      .builder(context),
                ))
            : null;
      } else {
        final List<Widget> widgets = <Widget>[];
        for (final Window window in children
            .where((Window otherWindow) => otherWindow.archetype != null)) {
          widgets.add(WindowContext(
              window: window,
              child: View(view: window.view, child: getChild(window))));
        }
        return widgets.isNotEmpty
            ? WindowContext(
                window: children.first,
                child: ViewAnchor(
                  view: ViewCollection(views: widgets),
                  child: widget.windows.values
                      .where((Window otherWindow) =>
                          otherWindow.view == parentView)
                      .single
                      .builder(context),
                ))
            : null;
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    Widget? result = _buildTree(null);
    if (result == null) {
      if (widget.windows.isNotEmpty) {
        final Window window = widget.windows.entries.first.value;
        result = View(view: window.view, child: window.builder(context));
      } else {
        result = const ViewCollection(views: <View>[]);
      }
    }

    return result;
  }
}

/// Provides descendents with access to the [Window] in which their rendered
class WindowContext extends InheritedWidget {
  /// [window] the [Window]
  const WindowContext({super.key, required this.window, required super.child});

  /// he [Window] in this context
  final Window window;

  /// Returns the [WindowContext] if any
  static WindowContext? of(BuildContext context) {
    return context.dependOnInheritedWidgetOfExactType<WindowContext>();
  }

  @override
  bool updateShouldNotify(WindowContext oldWidget) {
    return window != oldWidget.window;
  }
}
