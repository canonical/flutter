import 'package:flutter/material.dart';
import 'app/main_window.dart';
import 'app/window_controller_render.dart';
import 'app/window_manager_model.dart';
import 'app/window_settings.dart';

class MainRegularWindowControllerDelegate
    extends RegularWindowControllerDelegate {
  MainRegularWindowControllerDelegate({required this.onDestroyed});

  @override
  void onWindowDestroyed() {
    onDestroyed();
    super.onWindowDestroyed();
  }

  final VoidCallback onDestroyed;
}

void main() {
  final WindowManagerModel windowManagerModel = WindowManagerModel();
  final WindowSettings windowSettings = WindowSettings();

  final RegularWindowController controller = RegularWindowController(
    contentSize: WindowSizing(
      size: const Size(800, 600),
      constraints: const BoxConstraints(minWidth: 640, minHeight: 480),
    ),
    title: "Multi-Window Reference Application",
    delegate: MainRegularWindowControllerDelegate(
      onDestroyed: () => windowManagerModel.removeAll(),
    ),
  );

  windowManagerModel.add(
    KeyedWindowController(
        isMainWindow: true, key: UniqueKey(), controller: controller),
  );

  runWidget(
    ListenableBuilder(
      listenable: windowManagerModel,
      builder: (BuildContext context, Widget? _) {
        final List<Widget> childViews = <Widget>[
          RegularWindow(
            controller: controller,
            child: MaterialApp(
              home: MainWindow(
                  controller: controller,
                  windowSettings: windowSettings,
                  windowManagerModel: windowManagerModel),
            ),
          ),
        ];
        for (final KeyedWindowController controller
            in windowManagerModel.windows) {
          if (controller.parent == null && !controller.isMainWindow) {
            childViews.add(
              WindowControllerRender(
                controller: controller.controller,
                key: controller.key,
                windowSettings: windowSettings,
                windowManagerModel: windowManagerModel,
                onDestroyed: () => windowManagerModel.remove(controller.key),
                onError: () => windowManagerModel.remove(controller.key),
              ),
            );
          }
        }
        return ViewCollection(views: childViews);
      },
    ),
  );
}
