import 'package:flutter/material.dart';
import 'package:multi_window_ref_app/app/window_controller_render.dart';

import 'window_settings.dart';
import 'window_settings_dialog.dart';
import 'window_manager_model.dart';

class MainWindow extends StatefulWidget {
  MainWindow({super.key, required WindowController mainController})
      : _mainKeyedController =
            KeyedWindowController(controller: mainController);

  final KeyedWindowController _mainKeyedController;

  @override
  State<MainWindow> createState() => _MainWindowState();
}

class _MainWindowState extends State<MainWindow> {
  final WindowManagerModel _windowManagerModel = WindowManagerModel();
  final WindowSettings _settings = WindowSettings();

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
                  mainController: widget._mainKeyedController,
                  windowManagerModel: _windowManagerModel),
            ),
          ),
          Expanded(
            flex: 40,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                ListenableBuilder(
                    listenable: _windowManagerModel,
                    builder: (BuildContext context, Widget? child) {
                      return _WindowCreatorCard(
                          selectedWindow: _windowManagerModel.selected,
                          windowManagerModel: _windowManagerModel,
                          windowSettings: _settings);
                    })
              ],
            ),
          ),
        ],
      ),
    );

    return ViewAnchor(
        view: ListenableBuilder(
            listenable: _windowManagerModel,
            builder: (BuildContext context, Widget? widget) {
              final List<Widget> childViews = <Widget>[];
              for (final KeyedWindowController controller
                  in _windowManagerModel.windows) {
                if (controller.parent == null) {
                  childViews.add(WindowControllerRender(
                    controller: controller.controller,
                    key: controller.key,
                    windowSettings: _settings,
                    windowManagerModel: _windowManagerModel,
                    onDestroyed: () => _windowManagerModel.remove(controller),
                    onError: () => _windowManagerModel.remove(controller),
                  ));
                }
              }

              return ViewCollection(views: childViews);
            }),
        child: child);
  }
}

class _ActiveWindowsTable extends StatelessWidget {
  const _ActiveWindowsTable(
      {required this.mainController, required this.windowManagerModel});

  final KeyedWindowController mainController;
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
            rows: ([mainController] + windowManagerModel.windows)
                .map<DataRow>((KeyedWindowController controller) {
              return DataRow(
                key: controller.key,
                color: WidgetStateColor.resolveWith((states) {
                  if (states.contains(WidgetState.selected)) {
                    return Theme.of(context)
                        .colorScheme
                        .primary
                        .withOpacity(0.08);
                  }
                  return Colors.transparent;
                }),
                selected: controller.controller == windowManagerModel.selected,
                onSelectChanged: (selected) {
                  if (selected != null) {
                    windowManagerModel.select(
                        selected ? controller.controller.view?.viewId : null);
                  }
                },
                cells: [
                  DataCell(
                    ListenableBuilder(
                        listenable: controller.controller,
                        builder: (BuildContext context, Widget? _) => Text(
                            controller.controller.view != null
                                ? '${controller.controller.view?.viewId}'
                                : 'Loading...')),
                  ),
                  DataCell(
                    ListenableBuilder(
                        listenable: controller.controller,
                        builder: (BuildContext context, Widget? _) => Text(
                            controller.controller.type
                                .toString()
                                .replaceFirst('WindowArchetype.', ''))),
                  ),
                  DataCell(
                    ListenableBuilder(
                        listenable: controller.controller,
                        builder: (BuildContext context, Widget? _) =>
                            IconButton(
                              icon: const Icon(Icons.delete_outlined),
                              onPressed: () async {
                                await controller.controller.destroy();
                              },
                            )),
                  ),
                ],
              );
            }).toList(),
          );
        });
  }
}

class _WindowCreatorCard extends StatelessWidget {
  _WindowCreatorCard(
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
                  onPressed: () async {
                    windowManagerModel.add(KeyedWindowController(
                        controller: RegularWindowController()));
                  },
                  child: const Text('Regular'),
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
}