import 'package:flutter/material.dart';

Future<Map<String, dynamic>?> windowSettingsDialog(
  BuildContext context,
  Map<String, dynamic> settings,
) async {
  return await showDialog(
      barrierDismissible: true,
      context: context,
      builder: (BuildContext ctx) {
        Size regularSize = settings['regularSize'] as Size;
        Size floatingRegularSize = settings['floatingRegularSize'] as Size;
        Size dialogSize = settings['dialogSize'] as Size;
        Size satelliteSize = settings['satelliteSize'] as Size;
        Size popupSize = settings['popupSize'] as Size;
        Size tipSize = settings['tipSize'] as Size;
        Rect anchorRect = settings['anchorRect'] as Rect;

        return StatefulBuilder(
            builder: (BuildContext ctx, StateSetter setState) {
          return SimpleDialog(
            contentPadding: const EdgeInsets.all(4),
            titlePadding: const EdgeInsets.fromLTRB(24, 10, 24, 0),
            title: const Center(
              child: Text('Window Settings'),
            ),
            children: [
              SizedBox(
                width: 600,
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Row(
                      children: [
                        Expanded(
                          child: ListTile(
                            title: const Text('Regular'),
                            subtitle: Row(
                              children: [
                                Expanded(
                                  child: TextFormField(
                                    initialValue: regularSize.width.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial width',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => regularSize = Size(
                                          double.tryParse(value) ?? 0,
                                          regularSize.height),
                                    ),
                                  ),
                                ),
                                const SizedBox(
                                  width: 20,
                                ),
                                Expanded(
                                  child: TextFormField(
                                    initialValue: regularSize.height.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial height',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => regularSize = Size(
                                          regularSize.width,
                                          double.tryParse(value) ?? 0),
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                        const SizedBox(
                          width: 10,
                        ),
                        Expanded(
                          child: ListTile(
                            title: const Text('Floating Regular'),
                            subtitle: Row(
                              children: [
                                Expanded(
                                  child: TextFormField(
                                    initialValue:
                                        floatingRegularSize.width.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial width',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => floatingRegularSize = Size(
                                          double.tryParse(value) ?? 0,
                                          floatingRegularSize.height),
                                    ),
                                  ),
                                ),
                                const SizedBox(
                                  width: 20,
                                ),
                                Expanded(
                                  child: TextFormField(
                                    initialValue:
                                        floatingRegularSize.height.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial height',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => floatingRegularSize = Size(
                                          regularSize.width,
                                          double.tryParse(value) ?? 0),
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(
                      height: 2,
                    ),
                    Row(
                      children: [
                        Expanded(
                          child: ListTile(
                            title: const Text('Dialog'),
                            subtitle: Row(
                              children: [
                                Expanded(
                                  child: TextFormField(
                                    initialValue: dialogSize.width.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial width',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => dialogSize = Size(
                                          double.tryParse(value) ?? 0,
                                          dialogSize.height),
                                    ),
                                  ),
                                ),
                                const SizedBox(
                                  width: 20,
                                ),
                                Expanded(
                                  child: TextFormField(
                                    initialValue: dialogSize.height.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial height',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => dialogSize = Size(dialogSize.width,
                                          double.tryParse(value) ?? 0),
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                        const SizedBox(
                          width: 10,
                        ),
                        Expanded(
                          child: ListTile(
                            title: const Text('Satellite'),
                            subtitle: Row(
                              children: [
                                Expanded(
                                  child: TextFormField(
                                    initialValue:
                                        satelliteSize.width.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial width',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => satelliteSize = Size(
                                          double.tryParse(value) ?? 0,
                                          satelliteSize.height),
                                    ),
                                  ),
                                ),
                                const SizedBox(
                                  width: 20,
                                ),
                                Expanded(
                                  child: TextFormField(
                                    initialValue:
                                        satelliteSize.height.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial height',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => satelliteSize = Size(
                                          satelliteSize.width,
                                          double.tryParse(value) ?? 0),
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(
                      height: 2,
                    ),
                    Row(
                      children: [
                        Expanded(
                          child: ListTile(
                            title: const Text('Popup'),
                            subtitle: Row(
                              children: [
                                Expanded(
                                  child: TextFormField(
                                    initialValue: popupSize.width.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial width',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => popupSize = Size(
                                          double.tryParse(value) ?? 0,
                                          popupSize.height),
                                    ),
                                  ),
                                ),
                                const SizedBox(
                                  width: 20,
                                ),
                                Expanded(
                                  child: TextFormField(
                                    initialValue: popupSize.height.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial height',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => popupSize = Size(popupSize.width,
                                          double.tryParse(value) ?? 0),
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                        const SizedBox(
                          width: 10,
                        ),
                        Expanded(
                          child: ListTile(
                            title: const Text('Tip'),
                            subtitle: Row(
                              children: [
                                Expanded(
                                  child: TextFormField(
                                    initialValue: tipSize.width.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial width',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => tipSize = Size(
                                          double.tryParse(value) ?? 0,
                                          tipSize.height),
                                    ),
                                  ),
                                ),
                                const SizedBox(
                                  width: 20,
                                ),
                                Expanded(
                                  child: TextFormField(
                                    initialValue: tipSize.height.toString(),
                                    decoration: const InputDecoration(
                                      labelText: 'Initial height',
                                    ),
                                    onChanged: (String value) => setState(
                                      () => tipSize = Size(tipSize.width,
                                          double.tryParse(value) ?? 0),
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(
                      height: 2,
                    ),
                    const Divider(),
                    Row(
                      children: [
                        Expanded(
                          child: ListTile(
                            title: const Text('Anchor Rectangle'),
                            subtitle: Column(
                              children: [
                                const Align(
                                  alignment: Alignment.centerLeft,
                                  child: Text(
                                      "These values will be clamped to the size of the parent window"),
                                ),
                                Row(
                                  children: [
                                    Expanded(
                                      child: TextFormField(
                                        initialValue:
                                            anchorRect.left.toString(),
                                        decoration: const InputDecoration(
                                          labelText: 'Left',
                                        ),
                                        onChanged: (String value) => setState(
                                          () => anchorRect = Rect.fromLTWH(
                                              double.tryParse(value) ?? 0,
                                              anchorRect.top,
                                              anchorRect.width,
                                              anchorRect.height),
                                        ),
                                      ),
                                    ),
                                    const SizedBox(
                                      width: 20,
                                    ),
                                    Expanded(
                                      child: TextFormField(
                                        initialValue: anchorRect.top.toString(),
                                        decoration: const InputDecoration(
                                          labelText: 'Top',
                                        ),
                                        onChanged: (String value) => setState(
                                          () => anchorRect = Rect.fromLTWH(
                                              anchorRect.left,
                                              double.tryParse(value) ?? 0,
                                              anchorRect.width,
                                              anchorRect.height),
                                        ),
                                      ),
                                    ),
                                    const SizedBox(
                                      width: 20,
                                    ),
                                    Expanded(
                                      child: TextFormField(
                                        initialValue:
                                            anchorRect.width.toString(),
                                        decoration: const InputDecoration(
                                          labelText: 'Width',
                                        ),
                                        onChanged: (String value) => setState(
                                          () => anchorRect = Rect.fromLTWH(
                                              anchorRect.left,
                                              anchorRect.top,
                                              double.tryParse(value) ?? 0,
                                              anchorRect.height),
                                        ),
                                      ),
                                    ),
                                    const SizedBox(
                                      width: 20,
                                    ),
                                    Expanded(
                                      child: TextFormField(
                                        initialValue:
                                            anchorRect.height.toString(),
                                        decoration: const InputDecoration(
                                          labelText: 'Height',
                                        ),
                                        onChanged: (String value) => setState(
                                          () => anchorRect = Rect.fromLTWH(
                                              anchorRect.left,
                                              anchorRect.top,
                                              anchorRect.width,
                                              double.tryParse(value) ?? 0),
                                        ),
                                      ),
                                    ),
                                  ],
                                ),
                              ],
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(
                      height: 2,
                    ),
                  ],
                ),
              ),
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16),
                child: TextButton(
                  onPressed: () {
                    Navigator.of(context, rootNavigator: true)
                        .pop(<String, dynamic>{
                      'regularSize': regularSize,
                      'floatingRegularSize': floatingRegularSize,
                      'dialogSize': dialogSize,
                      'satelliteSize': satelliteSize,
                      'popupSize': popupSize,
                      'tipSize': tipSize,
                      'anchorRect': anchorRect,
                    });
                  },
                  child: const Text('Apply'),
                ),
              ),
              const SizedBox(
                height: 2,
              ),
            ],
          );
        });
      });
}
