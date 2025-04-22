// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

void showRegularWindowEditDialog(BuildContext context,
    {double? initialWidth,
    double? initialHeight,
    String? initialTitle,
    WindowState? initialState,
    Function(double?, double?, String?, WindowState)? onSave}) {
  final TextEditingController widthController =
      TextEditingController(text: initialWidth?.toStringAsFixed(1) ?? '');
  final TextEditingController heightController =
      TextEditingController(text: initialHeight?.toStringAsFixed(1) ?? '');
  final TextEditingController titleController =
      TextEditingController(text: initialTitle ?? '');

  showDialog(
    context: context,
    builder: (context) {
      WindowState selectedState = initialState ?? WindowState.restored;

      return AlertDialog(
        title: const Text("Regular Window Properties"),
        content: StatefulBuilder(
          builder: (context, setState) {
            return Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                TextField(
                  controller: widthController,
                  keyboardType: TextInputType.number,
                  decoration: const InputDecoration(labelText: "Width"),
                ),
                TextField(
                  controller: heightController,
                  keyboardType: TextInputType.number,
                  decoration: const InputDecoration(labelText: "Height"),
                ),
                TextField(
                  controller: titleController,
                  decoration: const InputDecoration(labelText: "Title"),
                ),
                DropdownButton<WindowState>(
                  value: selectedState,
                  onChanged: (WindowState? newState) {
                    if (newState != null) {
                      setState(() => selectedState = newState);
                    }
                  },
                  items: WindowState.values.map(
                    (WindowState state) {
                      return DropdownMenuItem<WindowState>(
                        value: state,
                        child: Text(
                          state.toString().split('.').last[0].toUpperCase() +
                              state.toString().split('.').last.substring(1),
                        ),
                      );
                    },
                  ).toList(),
                ),
              ],
            );
          },
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text("Cancel"),
          ),
          TextButton(
            onPressed: () {
              double? width = double.tryParse(widthController.text);
              double? height = double.tryParse(heightController.text);
              String? title =
                  titleController.text.isEmpty ? null : titleController.text;

              onSave?.call(width, height, title, selectedState);
              Navigator.of(context).pop();
            },
            child: const Text("Save"),
          ),
        ],
      );
    },
  );
}
