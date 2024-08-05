import 'package:flutter/material.dart';

import 'my_app.dart';

class PopupWindow extends StatelessWidget {
  const PopupWindow({super.key});

  @override
  Widget build(BuildContext context) {
    final window = WindowContext.of(context)!.window;

    return Container(
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
      child: ClipRRect(
        borderRadius: BorderRadius.circular(12.0),
        child: Directionality(
          textDirection: TextDirection.ltr,
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                Text(
                  'Popup',
                  style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                        color: Theme.of(context).colorScheme.onPrimary,
                      ),
                ),
                const SizedBox(height: 20.0),
                ElevatedButton(
                  onPressed: () async {
                    await createPopupWindow(
                        context: context,
                        parent: window,
                        size: const Size(200, 200),
                        anchorRect: Rect.fromLTWH(
                            0, 0, window.size.width, window.size.height),
                        positioner: const WindowPositioner(
                          parentAnchor: WindowPositionerAnchor.center,
                          childAnchor: WindowPositionerAnchor.center,
                          offset: Offset(100, 100),
                          constraintAdjustment: <WindowPositionerConstraintAdjustment>{
                            WindowPositionerConstraintAdjustment.slideX,
                            WindowPositionerConstraintAdjustment.slideY,
                          },
                        ),
                        builder: (BuildContext context) => const MyApp());
                  },
                  child: const Text('Another popup'),
                ),
                const SizedBox(height: 16.0),
                Text(
                  'View #${window.view.viewId}\n'
                  'Parent View: ${window.parent?.view.viewId}\n'
                  'Logical ${MediaQuery.of(context).size}',
                  textAlign: TextAlign.center,
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
