import 'package:flutter/material.dart';
import 'window_settings.dart';

Future<void> windowSettingsDialog(
    BuildContext context, WindowSettings settings) async {
  return await showDialog(
    barrierDismissible: true,
    context: context,
    builder: (BuildContext ctx) {
      return AlertDialog(
        contentPadding: const EdgeInsets.all(4),
        titlePadding: const EdgeInsets.fromLTRB(24, 10, 24, 0),
        title: const Center(
          child: Text('Window Settings'),
        ),
        content: SingleChildScrollView(
          child: ListBody(
            children: [
              _WindowSettingsTile(
                title: "Regular",
                size: settings.regularSizeNotifier,
                onChange: (Size size) => settings.regularSize = size,
              ),
              _WindowSettingsTile(
                title: "Dialog",
                size: settings.dialogSizeNotifier,
                onChange: (Size size) => settings.dialogSize = size,
              ),
            ],
          ),
        ),
        actions: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: TextButton(
              onPressed: () {
                Navigator.of(context, rootNavigator: true).pop();
              },
              child: const Text('Apply'),
            ),
          )
        ],
      );
    },
  );
}

class _WindowSettingsTile extends StatelessWidget {
  const _WindowSettingsTile(
      {required this.title, required this.size, required this.onChange});

  final String title;
  final SettingsValueNotifier<Size> size;
  final void Function(Size) onChange;

  @override
  Widget build(BuildContext context) {
    return ListTile(
      title: Text(title),
      subtitle: ListenableBuilder(
        listenable: size,
        builder: (BuildContext ctx, Widget? _) {
          return Row(
            children: [
              Expanded(
                child: TextFormField(
                  initialValue: size.value.width.toStringAsFixed(1),
                  decoration: const InputDecoration(
                    labelText: 'Initial width',
                  ),
                  onChanged: (String value) => onChange(
                    Size(double.tryParse(value) ?? 0, size.value.height),
                  ),
                ),
              ),
              const SizedBox(
                width: 20,
              ),
              Expanded(
                child: TextFormField(
                  initialValue: size.value.height.toStringAsFixed(1),
                  decoration: const InputDecoration(
                    labelText: 'Initial height',
                  ),
                  onChanged: (String value) => onChange(
                    Size(size.value.width, double.tryParse(value) ?? 0),
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}
