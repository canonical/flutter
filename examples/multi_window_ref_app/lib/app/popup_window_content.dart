import 'package:flutter/material.dart';
import 'package:multi_window_ref_app/app/child_window_renderer.dart';
import 'package:multi_window_ref_app/app/positioner_settings.dart';
import 'package:multi_window_ref_app/app/child_window_controller_text.dart';
import 'window_settings.dart';
import 'window_manager_model.dart';

class PopupWindowContent extends StatelessWidget {
  const PopupWindowContent(
      {super.key,
      required this.controller,
      required this.windowSettings,
      required this.positionerSettingsModifier,
      required this.windowManagerModel});

  final PopupWindowController controller;
  final WindowSettings windowSettings;
  final PositionerSettingsModifier positionerSettingsModifier;
  final WindowManagerModel windowManagerModel;

  @override
  Widget build(BuildContext context) {
    return ViewAnchor(
        view: ChildWindowRenderer(
            windowManagerModel: windowManagerModel,
            windowSettings: windowSettings,
            positionerSettingsModifier: positionerSettingsModifier,
            controller: controller),
        child: FocusScope(
          skipTraversal: false,
          autofocus: true,
          child: SizedBox(
                width: windowSettings.popupSizeNotifier.value.width,
                height: windowSettings.popupSizeNotifier.value.height,
                child: Container(
                    decoration: BoxDecoration(
                      gradient: LinearGradient(
                        begin: Alignment.topCenter,
                        end: Alignment.bottomCenter,
                        colors: [
                          Theme.of(context).colorScheme.primary,
                          Theme.of(context).colorScheme.secondary,
                        ],
                        stops: const [0.0, 1.0],
                      ),
                      borderRadius: BorderRadius.circular(12.0),
                    ),
                    child: SingleChildScrollView(
                      child: ClipRRect(
                        borderRadius: BorderRadius.circular(12.0),
                        child: Padding(
                          padding: const EdgeInsets.all(16.0),
                          child: Column(
                            mainAxisSize: MainAxisSize.max,
                            crossAxisAlignment: CrossAxisAlignment.center,
                            children: [
                              Text(
                                'Popup',
                                style: Theme.of(context)
                                    .textTheme
                                    .headlineMedium
                                    ?.copyWith(
                                      color: Theme.of(context)
                                          .colorScheme
                                          .onPrimary,
                                    ),
                              ),
                              const SizedBox(height: 20.0),
                              ElevatedButton(
                                  onPressed: () {
                                  final UniqueKey key = UniqueKey();
                                    windowManagerModel.add(KeyedWindowController(
                                        key: key,
                                        controller: RegularWindowController(
                                          onDestroyed: () =>
                                              windowManagerModel.remove(key),
                                          onError: (String error) =>
                                              windowManagerModel.remove(key),
                                          title: "Regular",
                                          size: windowSettings.regularSizeNotifier.value,
                                        )));
                                  },
                                  child: const Text('Regular window')),
                              const SizedBox(height: 20.0),
                              ElevatedButton(
                                  onPressed: () {
                                    final UniqueKey key = UniqueKey();
                                    windowManagerModel
                                        .add(KeyedWindowController(
                                            key: key,
                                            parent: controller,
                                            controller: PopupWindowController(
                                              parent: controller.rootView,
                                              onDestroyed: () =>
                                                  windowManagerModel
                                                      .remove(key),
                                              onError: (String error) =>
                                                  windowManagerModel
                                                      .remove(key),
                                              size: windowSettings
                                                  .popupSizeNotifier.value,
                                              sizeConstraints:
                                                  BoxConstraints.loose(
                                                      const Size(500, 500)),
                                              anchorRect: null,
                                              positioner:
                                                  const WindowPositioner(
                                                      parentAnchor:
                                                          WindowPositionerAnchor
                                                              .topLeft,
                                                      childAnchor:
                                                          WindowPositionerAnchor
                                                              .topLeft,
                                                      offset: Offset(50, 50),
                                                      constraintAdjustment: {
                                                    WindowPositionerConstraintAdjustment
                                                        .slideX,
                                                    WindowPositionerConstraintAdjustment
                                                        .slideY
                                                  }),
                                            )));
                                  },
                                  child: const Text('Another popup')),
                              const SizedBox(height: 16.0),
                              ChildWindowControllerText(controller: controller)
                            ],
                          ),
                        ),
                      ),
                    )))));
  }
}
