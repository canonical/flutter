import 'package:flutter/material.dart';

import 'main_window.dart';
import 'popup_window.dart';
import 'regular_window.dart';

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  // Window _getWindowFromContext(context) {
  //   Window? findWindowByViewId(Window window, int viewId) {
  //     if (window.view.viewId == viewId) {
  //       return window;
  //     }
  //     for (final child in window.children) {
  //       final result = findWindowByViewId(child, viewId);
  //       if (result != null) {
  //         return result;
  //       }
  //     }
  //     return null;
  //   }

  //   final topLevelWindows = MultiWindowAppContext.of(context)!.windows;
  //   final viewId = View.of(context).viewId;
  //   for (final window in topLevelWindows) {
  //     final result = findWindowByViewId(window, viewId);
  //     if (result != null) return result;
  //   }
  //   throw AssertionError('No matching window found for viewId: $viewId');
  // }

  @override
  Widget build(BuildContext context) {
    // final window = _getWindowFromContext(context);
    final window = WindowContext.of(context)!.window;
    // ignore: unused_local_variable
    final multiWindowAppContext = MultiWindowAppContext.of(context);

    final Widget? widget;
    switch (window.archetype) {
      case WindowArchetype.regular:
        if (window.view.viewId == 0) {
          widget = const MaterialApp(home: MainWindow());
          break;
        }
        widget = const RegularWindow();
        break;
      case WindowArchetype.popup:
        widget = const PopupWindow();
        break;
      default:
        throw AssertionError(
            'Build method not implemented for window archetype ${window.archetype}');
    }

    final List<Widget> childViews = window.children.map((childWindow) {
      return View(
        view: childWindow.view,
        child: WindowContext(
          window: childWindow,
          child: childWindow.builder(context),
        ),
      );
    }).toList();

    return ViewAnchor(view: ViewCollection(views: childViews), child: widget);
  }
}
