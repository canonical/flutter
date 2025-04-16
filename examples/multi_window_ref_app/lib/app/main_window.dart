import 'package:flutter/material.dart';

import 'child_window_renderer.dart';
import 'window_settings.dart';
import 'window_settings_dialog.dart';
import 'window_manager_model.dart';
import 'regular_window_edit_dialog.dart';
import 'dialog_window_edit_dialog.dart';

class MainWindow extends StatefulWidget {
  const MainWindow({
    super.key,
    required this.controller,
    required this.windowSettings,
    required this.windowManagerModel,
  });

  final WindowManagerModel windowManagerModel;
  final WindowSettings windowSettings;
  final RegularWindowController controller;

  @override
  State<MainWindow> createState() => _MainWindowState();
}

class _MainWindowState extends State<MainWindow> {
  @override
  Widget build(BuildContext context) {
    final child = Scaffold(
      appBar: AppBar(
        title: const Text('Multi Window Reference App'),
      ),
      body: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            flex: 60,
            child: SingleChildScrollView(
              scrollDirection: Axis.vertical,
              child: _ActiveWindowsTable(
                windowManagerModel: widget.windowManagerModel,
              ),
            ),
          ),
          Expanded(
            flex: 40,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                ListenableBuilder(
                  listenable: widget.windowManagerModel,
                  builder: (BuildContext context, Widget? child) {
                    return _WindowCreatorCard(
                      selectedWindow: widget.windowManagerModel.selected,
                      windowManagerModel: widget.windowManagerModel,
                      windowSettings: widget.windowSettings,
                    );
                  },
                )
              ],
            ),
          ),
        ],
      ),
    );

    return ViewAnchor(
      view: ChildWindowRenderer(
        windowManagerModel: widget.windowManagerModel,
        windowSettings: widget.windowSettings,
        controller: widget.controller,
      ),
      child: child,
    );
  }
}

class _ActiveWindowsTable extends StatelessWidget {
  const _ActiveWindowsTable({required this.windowManagerModel});

  final WindowManagerModel windowManagerModel;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
        listenable: windowManagerModel,
        builder: (BuildContext context, Widget? widget) {
          return DataTable(
            showBottomBorder: true,
            onSelectAll: (selected) {
              windowManagerModel.select(null);
            },
            columns: const [
              DataColumn(
                label: SizedBox(
                  width: 20,
                  child: Text(
                    'ID',
                    style: TextStyle(
                      fontSize: 16,
                    ),
                  ),
                ),
              ),
              DataColumn(
                label: SizedBox(
                  width: 120,
                  child: Text(
                    'Type',
                    style: TextStyle(
                      fontSize: 16,
                    ),
                  ),
                ),
              ),
              DataColumn(
                  label: SizedBox(
                    width: 20,
                    child: Text(''),
                  ),
                  numeric: true),
            ],
            rows: (windowManagerModel.windows).map<DataRow>(
              (KeyedWindowController controller) {
                return DataRow(
                  key: controller.key,
                  color: WidgetStateColor.resolveWith(
                    (states) {
                      if (states.contains(WidgetState.selected)) {
                        return Theme.of(context)
                            .colorScheme
                            .primary
                            .withAlpha(20);
                      }
                      return Colors.transparent;
                    },
                  ),
                  selected:
                      controller.controller == windowManagerModel.selected,
                  onSelectChanged: (selected) {
                    if (selected != null) {
                      windowManagerModel.select(selected
                          ? controller.controller.rootView.viewId
                          : null);
                    }
                  },
                  cells: [
                    DataCell(Text('${controller.controller.rootView.viewId}')),
                    DataCell(
                      ListenableBuilder(
                        listenable: controller.controller,
                        builder: (BuildContext context, Widget? _) => Text(
                          controller.controller.type
                              .toString()
                              .replaceFirst('WindowArchetype.', ''),
                        ),
                      ),
                    ),
                    DataCell(
                      ListenableBuilder(
                        listenable: controller.controller,
                        builder: (BuildContext context, Widget? _) => Row(
                          children: [
                            IconButton(
                              icon: const Icon(Icons.edit_outlined),
                              onPressed: () {
                                if (controller.controller.type ==
                                    WindowArchetype.regular) {
                                  showRegularWindowEditDialog(
                                    context,
                                    initialWidth:
                                        controller.controller.contentSize.width,
                                    initialHeight: controller
                                        .controller.contentSize.height,
                                    initialTitle: "",
                                    initialState: (controller.controller
                                            as RegularWindowController)
                                        .state,
                                    onSave: (double? width, double? height,
                                        String? title, WindowState? state) {
                                      final regularController =
                                          controller.controller
                                              as RegularWindowController;
                                      if (width != null && height != null) {
                                        regularController.setContentSize(
                                          WindowSizing(
                                            size: Size(width, height),
                                          ),
                                        );
                                      }
                                      if (title != null) {
                                        regularController.setTitle(title);
                                      }
                                      if (state != null) {
                                        regularController.setState(state);
                                      }
                                    },
                                  );
                                } else if (controller.controller.type ==
                                    WindowArchetype.dialog) {
                                  showDialogWindowEditDialog(
                                    context,
                                    initialWidth:
                                        controller.controller.contentSize.width,
                                    initialHeight: controller
                                        .controller.contentSize.height,
                                    initialTitle: "",
                                    onSave: (double? width, double? height,
                                        String? title) {
                                      final dialogController = controller
                                          .controller as DialogWindowController;
                                      if (width != null && height != null) {
                                        dialogController.setContentSize(
                                          WindowSizing(
                                            size: Size(width, height),
                                          ),
                                        );
                                      }
                                      if (title != null) {
                                        dialogController.setTitle(title);
                                      }
                                    },
                                  );
                                }
                              },
                            ),
                            IconButton(
                              icon: const Icon(Icons.delete_outlined),
                              onPressed: () {
                                controller.controller.destroy();
                              },
                            )
                          ],
                        ),
                      ),
                    ),
                  ],
                );
              },
            ).toList(),
          );
        });
  }
}

class _RegularWindowControllerDelegate extends RegularWindowControllerDelegate {
  _RegularWindowControllerDelegate({required this.onDestroyed});

  @override
  void onWindowDestroyed() {
    onDestroyed();
    super.onWindowDestroyed();
  }

  final VoidCallback onDestroyed;
}

class _DialogWindowControllerDelegate extends DialogWindowControllerDelegate {
  _DialogWindowControllerDelegate({required this.onDestroyed});

  @override
  void onWindowDestroyed() {
    onDestroyed();
    super.onWindowDestroyed();
  }

  final VoidCallback onDestroyed;
}

class _WindowCreatorCard extends StatelessWidget {
  const _WindowCreatorCard(
      {required this.selectedWindow,
      required this.windowManagerModel,
      required this.windowSettings});

  final WindowController? selectedWindow;
  final WindowManagerModel windowManagerModel;
  final WindowSettings windowSettings;

  @override
  Widget build(BuildContext context) {
    return Card.outlined(
      margin: const EdgeInsets.symmetric(horizontal: 25),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(25, 0, 25, 5),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            const Padding(
              padding: EdgeInsets.only(top: 10, bottom: 10),
              child: Text(
                'New Window',
                style: TextStyle(
                  fontWeight: FontWeight.bold,
                  fontSize: 16.0,
                ),
              ),
            ),
            Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                OutlinedButton(
                  onPressed: () {
                    final UniqueKey key = UniqueKey();
                    windowManagerModel.add(
                      KeyedWindowController(
                        key: key,
                        controller: RegularWindowController(
                          delegate: _RegularWindowControllerDelegate(
                            onDestroyed: () => windowManagerModel.remove(key),
                          ),
                          title: "Regular",
                          contentSize: WindowSizing(
                            size: windowSettings.regularSizeNotifier.value,
                          ),
                        ),
                      ),
                    );
                  },
                  child: const Text('Regular'),
                ),
                const SizedBox(height: 8),
                OutlinedButton(
                  onPressed: () {
                    final UniqueKey key = UniqueKey();
                    final parentController =
                        _canSelectedWindowBeParentOf(WindowArchetype.dialog)
                            ? windowManagerModel.selected
                            : null;
                    windowManagerModel.add(
                      KeyedWindowController(
                        parent: parentController,
                        key: key,
                        controller: DialogWindowController(
                          delegate: _DialogWindowControllerDelegate(
                            onDestroyed: () => windowManagerModel.remove(key),
                          ),
                          title: "Dialog",
                          parent: parentController?.rootView,
                          contentSize: WindowSizing(
                            size: windowSettings.dialogSizeNotifier.value,
                          ),
                        ),
                      ),
                    );
                  },
                  child: Text(_canSelectedWindowBeParentOf(
                          WindowArchetype.dialog)
                      ? 'Dialog of ID ${windowManagerModel.selected?.rootView.viewId}'
                      : 'Dialog'),
                ),
                const SizedBox(height: 8),
                Container(
                  alignment: Alignment.bottomRight,
                  child: TextButton(
                    child: const Text('SETTINGS'),
                    onPressed: () {
                      windowSettingsDialog(context, windowSettings);
                    },
                  ),
                ),
                const SizedBox(width: 8),
              ],
            ),
          ],
        ),
      ),
    );
  }

  // Check if the currently selected window can be made the parent of a
  // window with the specified archetype.
  bool _canSelectedWindowBeParentOf(WindowArchetype archetype) {
    switch (archetype) {
      case WindowArchetype.regular:
        return false;
      case WindowArchetype.dialog:
        return windowManagerModel.selected != null;
    }
  }
}
