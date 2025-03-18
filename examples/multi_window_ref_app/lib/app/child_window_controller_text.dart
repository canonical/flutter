import 'package:flutter/material.dart';

class ChildWindowControllerText extends StatelessWidget {
  const ChildWindowControllerText({super.key, required this.controller});

  final ChildWindowController controller;

  @override
  Widget build(BuildContext context) {
    final dpr = MediaQuery.of(context).devicePixelRatio;

    return ListenableBuilder(
        listenable: controller,
        builder: (BuildContext context, Widget? _) {
          return Text(
              'View #${controller.rootView.viewId}\n'
              'Parent View: ${controller.parent.viewId}\n'
              'Size: ${controller.size.width.toStringAsFixed(1)}\u00D7${controller.size.height.toStringAsFixed(1)}\n'
              'Device Pixel Ratio: $dpr',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    color: Theme.of(context).colorScheme.onPrimary,
                  ));
        });
  }
}
