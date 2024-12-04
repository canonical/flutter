import 'package:flutter/material.dart';
import 'package:multi_window_ref_app/app/window_metadata_content.dart';

import 'window_settings.dart';
import 'window_settings_dialog.dart';

class _WindowManagerModel extends ChangeNotifier {
  final List<WindowController> _windows = <WindowController>[];
  List<WindowController> get windows => _windows;
  int? _selectedViewId;
  WindowController? get selected {
    if (_selectedViewId == null) {
      return null;
    }

    for (final WindowController window in _windows) {
      if (window.view?.viewId == _selectedViewId) {
        return window;
      }
    }

    return null;
  }

  void add(WindowController window) {
    _windows.add(window);
    notifyListeners();
  }

  void remove(WindowController window) {
    _windows.remove(window);
    notifyListeners();
  }

  void select(int? viewId) {
    _selectedViewId = viewId;
    notifyListeners();
  }
}

class MainWindow extends StatefulWidget {
  const MainWindow({super.key});

  @override
  State<MainWindow> createState() => _MainWindowState();
}

class _MainWindowState extends State<MainWindow> {
  final _WindowManagerModel _windowManagerModel = _WindowManagerModel();

  @override
  Widget build(BuildContext context) {
    final widget = Scaffold(
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
              child:
                  _ActiveWindowsTable(windowManagerModel: _windowManagerModel),
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
                          windowManagerModel: _windowManagerModel);
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
              return _ViewCollection(windowManagerModel: _windowManagerModel);
            }),
        child: widget);
  }
}

class _ViewCollection extends StatelessWidget {
  _ViewCollection({required this.windowManagerModel});

  _WindowManagerModel windowManagerModel;

  @override
  Widget build(BuildContext context) {
    final List<Widget> childViews = <Widget>[];
    for (final WindowController childWindow in windowManagerModel.windows) {
      childViews.add(WindowMetadataContent(controller: childWindow));
    }

    return ViewCollection(views: childViews);
  }
}

class _ActiveWindowsTable extends StatelessWidget {
  const _ActiveWindowsTable({required this.windowManagerModel});

  final _WindowManagerModel windowManagerModel;

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
            rows: windowManagerModel.windows
                .map<DataRow>((WindowController controller) {
              return DataRow(
                color: WidgetStateColor.resolveWith((states) {
                  if (states.contains(WidgetState.selected)) {
                    return Theme.of(context)
                        .colorScheme
                        .primary
                        .withOpacity(0.08);
                  }
                  return Colors.transparent;
                }),
                selected: controller.view?.viewId ==
                    windowManagerModel._selectedViewId,
                onSelectChanged: (selected) {
                  if (selected != null) {
                    windowManagerModel
                        .select(selected ? controller.view?.viewId : null);
                  }
                },
                cells: [
                  DataCell(
                    ListenableBuilder(
                        listenable: controller,
                        builder: (BuildContext context, Widget? _) => Text(
                            controller.view != null
                                ? '${controller.view?.viewId}'
                                : 'Loading...')),
                  ),
                  DataCell(
                    ListenableBuilder(
                        listenable: controller,
                        builder: (BuildContext context, Widget? _) => Text(
                            controller.type
                                .toString()
                                .replaceFirst('WindowArchetype.', ''))),
                  ),
                  DataCell(
                    ListenableBuilder(
                        listenable: controller,
                        builder: (BuildContext context, Widget? _) =>
                            IconButton(
                              icon: const Icon(Icons.delete_outlined),
                              onPressed: () async {
                                await controller.destroy();
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

class _WindowCreatorCard extends StatefulWidget {
  const _WindowCreatorCard(
      {required this.selectedWindow, required this.windowManagerModel});

  final WindowController? selectedWindow;
  final _WindowManagerModel windowManagerModel;

  @override
  State<StatefulWidget> createState() => _WindowCreatorCardState();
}

class _WindowCreatorCardState extends State<_WindowCreatorCard> {
  WindowSettings _settings = WindowSettings();

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
                    widget.windowManagerModel.add(RegularWindowController());
                  },
                  child: const Text('Regular'),
                ),
                const SizedBox(height: 8),
                Container(
                  alignment: Alignment.bottomRight,
                  child: TextButton(
                    child: const Text('SETTINGS'),
                    onPressed: () {
                      windowSettingsDialog(context, _settings).then(
                        (WindowSettings? settings) {
                          if (settings != null) {
                            _settings = settings;
                          }
                        },
                      );
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
