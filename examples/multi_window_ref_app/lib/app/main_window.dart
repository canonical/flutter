import 'package:flutter/material.dart';

import 'custom_positioner_dialog.dart';
import 'popup_window.dart';
import 'regular_window.dart';
import 'window_settings_dialog.dart';

class MainWindow extends StatefulWidget {
  const MainWindow({super.key});

  @override
  State<MainWindow> createState() => _MainWindowState();
}

class _MainWindowState extends State<MainWindow> {
  Map<String, dynamic> windowSettings = {
    'regularSize': const Size(400, 300),
    'floatingRegularSize': const Size(300, 300),
    'dialogSize': const Size(200, 200),
    'satelliteSize': const Size(150, 300),
    'popupSize': const Size(200, 200),
    'tipSize': const Size(140, 140),
    'anchorRect': const Rect.fromLTWH(0, 0, 1000, 1000),
  };

  int positionerIndex = 0;
  List<Map<String, dynamic>> positionerSettings = [
    // Left
    <String, dynamic>{
      'name': 'Left',
      'parentAnchor': WindowPositionerAnchor.left,
      'childAnchor': WindowPositionerAnchor.right,
      'offset': const Offset(0, 0),
      'constraintAdjustments': <WindowPositionerConstraintAdjustment>{
        WindowPositionerConstraintAdjustment.slideX,
        WindowPositionerConstraintAdjustment.slideY,
      }
    },
    // Right
    <String, dynamic>{
      'name': 'Right',
      'parentAnchor': WindowPositionerAnchor.right,
      'childAnchor': WindowPositionerAnchor.left,
      'offset': const Offset(0, 0),
      'constraintAdjustments': <WindowPositionerConstraintAdjustment>{
        WindowPositionerConstraintAdjustment.slideX,
        WindowPositionerConstraintAdjustment.slideY,
      }
    },
    // Bottom Left
    <String, dynamic>{
      'name': 'Bottom Left',
      'parentAnchor': WindowPositionerAnchor.bottomLeft,
      'childAnchor': WindowPositionerAnchor.topRight,
      'offset': const Offset(0, 0),
      'constraintAdjustments': <WindowPositionerConstraintAdjustment>{
        WindowPositionerConstraintAdjustment.slideX,
        WindowPositionerConstraintAdjustment.slideY,
      }
    },
    // Bottom
    <String, dynamic>{
      'name': 'Bottom',
      'parentAnchor': WindowPositionerAnchor.bottom,
      'childAnchor': WindowPositionerAnchor.top,
      'offset': const Offset(0, 0),
      'constraintAdjustments': <WindowPositionerConstraintAdjustment>{
        WindowPositionerConstraintAdjustment.slideX,
        WindowPositionerConstraintAdjustment.slideY,
      }
    },
    // Bottom Right
    <String, dynamic>{
      'name': 'Bottom Right',
      'parentAnchor': WindowPositionerAnchor.bottomRight,
      'childAnchor': WindowPositionerAnchor.topLeft,
      'offset': const Offset(0, 0),
      'constraintAdjustments': <WindowPositionerConstraintAdjustment>{
        WindowPositionerConstraintAdjustment.slideX,
        WindowPositionerConstraintAdjustment.slideY,
      }
    },
    // Center
    <String, dynamic>{
      'name': 'Center',
      'parentAnchor': WindowPositionerAnchor.center,
      'childAnchor': WindowPositionerAnchor.center,
      'offset': const Offset(0, 0),
      'constraintAdjustments': <WindowPositionerConstraintAdjustment>{
        WindowPositionerConstraintAdjustment.slideX,
        WindowPositionerConstraintAdjustment.slideY,
      }
    },
    // Custom
    <String, dynamic>{
      'name': 'Custom',
      'parentAnchor': WindowPositionerAnchor.left,
      'childAnchor': WindowPositionerAnchor.right,
      'offset': const Offset(0, 50),
      'constraintAdjustments': <WindowPositionerConstraintAdjustment>{
        WindowPositionerConstraintAdjustment.slideX,
        WindowPositionerConstraintAdjustment.slideY,
      }
    }
  ];

  int selectedRowIndex = -1;

  @override
  Widget build(BuildContext context) {
    List<Window> getWindowsInTree(List<Window> topLevelWindows) {
      List<Window> allWindows = [];
      void getWindowsInSubtree(Window window) {
        allWindows.add(window);
        for (final child in window.children) {
          getWindowsInSubtree(child);
        }
      }

      for (final window in topLevelWindows) {
        getWindowsInSubtree(window);
      }
      return allWindows;
    }

    final windows =
        getWindowsInTree(MultiWindowAppContext.of(context)!.windows);

    final window = WindowContext.of(context)!.window;

    final widget = Scaffold(
      appBar: AppBar(
        title: const Text('Multi Window Test'),
      ),
      body: Column(
        children: [
          Expanded(
            child: ListView(
              padding: const EdgeInsets.symmetric(horizontal: 10.0),
              children: [
                Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Expanded(
                      flex: 60,
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          SizedBox(
                            width: 400,
                            height: 500,
                            child: SingleChildScrollView(
                              scrollDirection: Axis.vertical,
                              child: DataTable(
                                showBottomBorder: true,
                                onSelectAll: (selected) {
                                  selectedRowIndex = -1;
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
                                rows: windows
                                    .asMap()
                                    .entries
                                    .map<DataRow>((indexedEntry) {
                                  final index = indexedEntry.key;
                                  final Window entry = indexedEntry.value;
                                  final window = entry;
                                  final viewId = window.view.viewId;
                                  final archetype = window.archetype;
                                  final isSelected = selectedRowIndex == index;

                                  return DataRow(
                                    color:
                                        WidgetStateColor.resolveWith((states) {
                                      if (states
                                          .contains(WidgetState.selected)) {
                                        return Theme.of(context)
                                            .colorScheme
                                            .primary
                                            .withOpacity(0.08);
                                      }
                                      return Colors.transparent;
                                    }),
                                    selected: isSelected,
                                    onSelectChanged: (selected) {
                                      if (selected != null) {
                                        setState(() {
                                          selectedRowIndex =
                                              selected ? index : -1;
                                        });
                                      }
                                    },
                                    cells: [
                                      DataCell(
                                        Text('$viewId'),
                                      ),
                                      DataCell(
                                        Text(archetype.toString().replaceFirst(
                                            'WindowArchetype.', '')),
                                      ),
                                      DataCell(
                                        IconButton(
                                          icon:
                                              const Icon(Icons.delete_outlined),
                                          onPressed: () {
                                            destroyWindow(context, window);
                                          },
                                        ),
                                      ),
                                    ],
                                  );
                                }).toList(),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                    Expanded(
                      flex: 40,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          Card.outlined(
                            margin: const EdgeInsets.symmetric(horizontal: 25),
                            child: Padding(
                              padding: const EdgeInsets.fromLTRB(25, 0, 25, 5),
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.center,
                                children: [
                                  const Padding(
                                    padding:
                                        EdgeInsets.only(top: 10, bottom: 10),
                                    child: Text(
                                      'New Window',
                                      style: TextStyle(
                                        fontWeight: FontWeight.bold,
                                        fontSize: 16.0,
                                      ),
                                    ),
                                  ),
                                  Column(
                                    crossAxisAlignment:
                                        CrossAxisAlignment.stretch,
                                    children: [
                                      OutlinedButton(
                                        onPressed: () async {
                                          await createRegularWindow(
                                              context: context,
                                              size:
                                                  windowSettings['regularSize'],
                                              builder: (BuildContext context) {
                                                return const MaterialApp(
                                                    home: RegularWindow());
                                              });
                                        },
                                        child: const Text('Regular'),
                                      ),
                                      const SizedBox(height: 8),
                                      // OutlinedButton(
                                      //   onPressed: () async {
                                      //     final windowId =
                                      //         await createFloatingRegularWindow(
                                      //             windowSettings[
                                      //                 'floatingRegularSize']);
                                      //     await setWindowId(windowId);
                                      //     setState(() {
                                      //       selectedRowIndex =
                                      //           windows.indexWhere((window) =>
                                      //               window['id'] == windowId);
                                      //     });
                                      //   },
                                      //   child: const Text('Floating Regular'),
                                      // ),
                                      // const SizedBox(height: 8),
                                      // OutlinedButton(
                                      //   onPressed: () async {
                                      //     final windowId =
                                      //         await createDialogWindow(
                                      //             windowSettings['dialogSize'],
                                      //             selectedRowIndex >= 0 &&
                                      //                     isMirShellWindow(
                                      //                         selectedRowIndex)
                                      //                 ? windows[
                                      //                         selectedRowIndex]
                                      //                     ['id']
                                      //                 : null);
                                      //     await setWindowId(windowId);
                                      //   },
                                      //   child: Text(selectedRowIndex >= 0 &&
                                      //           isMirShellWindow(
                                      //               selectedRowIndex)
                                      //       ? 'Dialog of ID ${windows[selectedRowIndex]['id']}'
                                      //       : 'Dialog'),
                                      // ),
                                      // const SizedBox(height: 8),
                                      // OutlinedButton(
                                      //   onPressed: selectedRowIndex >= 0 &&
                                      //           isMirShellWindow(
                                      //               selectedRowIndex)
                                      //       ? () async {
                                      //           final windowId =
                                      //               await createSatelliteWindow(
                                      //             windows[selectedRowIndex]
                                      //                 ['id'],
                                      //             windowSettings[
                                      //                 'satelliteSize'],
                                      //             clampAnchorRectToSize(
                                      //                 await getWindowSize(windows[
                                      //                         selectedRowIndex]
                                      //                     ['id'])),
                                      //             FlutterViewPositioner(
                                      //               parentAnchor:
                                      //                   positionerSettings[
                                      //                           positionerIndex]
                                      //                       ['parentAnchor'],
                                      //               childAnchor:
                                      //                   positionerSettings[
                                      //                           positionerIndex]
                                      //                       ['childAnchor'],
                                      //               offset: positionerSettings[
                                      //                       positionerIndex]
                                      //                   ['offset'],
                                      //               constraintAdjustment:
                                      //                   positionerSettings[
                                      //                           positionerIndex]
                                      //                       [
                                      //                       'constraintAdjustments'],
                                      //             ),
                                      //           );
                                      //           await setWindowId(windowId);
                                      //           setState(() {
                                      //             // Cycle through presets when the last one (Custom preset) is not selected
                                      //             if (positionerIndex !=
                                      //                 positionerSettings
                                      //                         .length -
                                      //                     1) {
                                      //               positionerIndex =
                                      //                   (positionerIndex + 1) %
                                      //                       (positionerSettings
                                      //                               .length -
                                      //                           1);
                                      //             }
                                      //           });
                                      //         }
                                      //       : null,
                                      //   child: Text(selectedRowIndex >= 0
                                      //       ? 'Satellite of ID ${windows[selectedRowIndex]['id']}'
                                      //       : 'Satellite'),
                                      // ),
                                      // const SizedBox(height: 8),
                                      OutlinedButton(
                                        onPressed: selectedRowIndex >= 0 &&
                                                selectedRowIndex <
                                                    windows.length
                                            ? () async {
                                                final selectedPositionerSettings =
                                                    positionerSettings[
                                                        positionerIndex];
                                                final selectedParent =
                                                    windows[selectedRowIndex];
                                                await createPopupWindow(
                                                    context: context,
                                                    parent: selectedParent,
                                                    size: windowSettings[
                                                        'popupSize'],
                                                    anchorRect:
                                                        _clampRectToSize(
                                                            windowSettings[
                                                                'anchorRect'],
                                                            selectedParent
                                                                .size),
                                                    positioner:
                                                        WindowPositioner(
                                                      parentAnchor:
                                                          selectedPositionerSettings[
                                                              'parentAnchor'],
                                                      childAnchor:
                                                          selectedPositionerSettings[
                                                              'childAnchor'],
                                                      offset:
                                                          selectedPositionerSettings[
                                                              'offset'],
                                                      constraintAdjustment:
                                                          selectedPositionerSettings[
                                                              'constraintAdjustments'],
                                                    ),
                                                    builder:
                                                        (BuildContext context) {
                                                      return const PopupWindow();
                                                    });
                                              }
                                            : null,
                                        child: Text(selectedRowIndex >= 0 &&
                                                selectedRowIndex <
                                                    windows.length
                                            ? 'Popup of ID ${windows[selectedRowIndex].view.viewId}'
                                            : 'Popup'),
                                      ),
                                      // const SizedBox(height: 8),
                                      // OutlinedButton(
                                      //   onPressed: selectedRowIndex >= 0
                                      //       ? () async {
                                      //           final windowId =
                                      //               await createTipWindow(
                                      //             windows[selectedRowIndex]
                                      //                 ['id'],
                                      //             windowSettings['tipSize'],
                                      //             clampAnchorRectToSize(
                                      //                 await getWindowSize(windows[
                                      //                         selectedRowIndex]
                                      //                     ['id'])),
                                      //             FlutterViewPositioner(
                                      //               parentAnchor:
                                      //                   positionerSettings[
                                      //                           positionerIndex]
                                      //                       ['parentAnchor'],
                                      //               childAnchor:
                                      //                   positionerSettings[
                                      //                           positionerIndex]
                                      //                       ['childAnchor'],
                                      //               offset: positionerSettings[
                                      //                       positionerIndex]
                                      //                   ['offset'],
                                      //               constraintAdjustment:
                                      //                   positionerSettings[
                                      //                           positionerIndex]
                                      //                       [
                                      //                       'constraintAdjustments'],
                                      //             ),
                                      //           );
                                      //           await setWindowId(windowId);
                                      //           setState(() {
                                      //             // Cycle through presets when the last one (Custom preset) is not selected
                                      //             if (positionerIndex !=
                                      //                 positionerSettings
                                      //                         .length -
                                      //                     1) {
                                      //               positionerIndex =
                                      //                   (positionerIndex + 1) %
                                      //                       (positionerSettings
                                      //                               .length -
                                      //                           1);
                                      //             }
                                      //           });
                                      //         }
                                      //       : null,
                                      //   child: Text(selectedRowIndex >= 0
                                      //       ? 'Tip of ID ${windows[selectedRowIndex]['id']}'
                                      //       : 'Tip'),
                                      // ),
                                      const SizedBox(height: 8),
                                      Container(
                                        alignment: Alignment.bottomRight,
                                        child: TextButton(
                                          child: const Text('SETTINGS'),
                                          onPressed: () {
                                            windowSettingsDialog(
                                                    context, windowSettings)
                                                .then(
                                              (Map<String, dynamic>? settings) {
                                                if (settings != null) {
                                                  windowSettings = settings;
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
                          ),
                          const SizedBox(height: 12),
                          Card.outlined(
                            margin: const EdgeInsets.symmetric(horizontal: 25),
                            child: Padding(
                              padding: const EdgeInsets.fromLTRB(25, 0, 15, 5),
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.center,
                                children: [
                                  const Padding(
                                    padding: EdgeInsets.only(top: 10),
                                    child: Text(
                                      'Positioner',
                                      style: TextStyle(
                                        fontWeight: FontWeight.bold,
                                        fontSize: 16.0,
                                      ),
                                    ),
                                  ),
                                  ListTile(
                                    title: const Text('Preset'),
                                    subtitle: DropdownButton(
                                      items: positionerSettings
                                          .map((map) => map['name'] as String)
                                          .toList()
                                          .map<DropdownMenuItem<String>>(
                                              (String value) {
                                        return DropdownMenuItem<String>(
                                          value: value,
                                          child: Text(value),
                                        );
                                      }).toList(),
                                      value: positionerSettings
                                          .map((map) => map['name'] as String)
                                          .toList()[positionerIndex],
                                      isExpanded: true,
                                      focusColor: Colors.transparent,
                                      onChanged: (String? value) {
                                        setState(() {
                                          positionerIndex = positionerSettings
                                              .map((map) =>
                                                  map['name'] as String)
                                              .toList()
                                              .indexOf(value!);
                                        });
                                      },
                                    ),
                                  ),
                                  Container(
                                    alignment: Alignment.bottomRight,
                                    child: Padding(
                                      padding: const EdgeInsets.only(right: 10),
                                      child: TextButton(
                                        child: const Text('CUSTOM PRESET'),
                                        onPressed: () {
                                          customPositionerDialog(context,
                                                  positionerSettings.last)
                                              .then(
                                            (Map<String, dynamic>? settings) {
                                              if (settings != null) {
                                                setState(() {
                                                  positionerSettings[
                                                      positionerSettings
                                                              .length -
                                                          1] = settings;
                                                  positionerIndex =
                                                      positionerSettings
                                                              .length -
                                                          1;
                                                });
                                              }
                                            },
                                          );
                                        },
                                      ),
                                    ),
                                  ),
                                  const SizedBox(width: 8),
                                ],
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );

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

  Rect _clampRectToSize(Rect anchorRect, Size? size) {
    double left = anchorRect.left.clamp(0, size?.width as double);
    double top = anchorRect.top.clamp(0, size?.height as double);
    double right = anchorRect.right.clamp(0, size?.width as double);
    double bottom = anchorRect.bottom.clamp(0, size?.height as double);
    return Rect.fromLTRB(left, top, right, bottom);
  }
}
