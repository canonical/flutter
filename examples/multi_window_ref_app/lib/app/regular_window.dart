import 'package:flutter/material.dart';

import 'my_app.dart';

class RegularWindow extends StatelessWidget {
  const RegularWindow({super.key});

  @override
  Widget build(BuildContext context) {
    final window = WindowContext.of(context)!.window;

    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: Text('${window.archetype}')),
        body: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              ElevatedButton(
                onPressed: () async {
                  await createRegularWindow(
                      context: context,
                      size: const Size(400, 300),
                      builder: (BuildContext context) => const MyApp());
                },
                child: const Text('Create Regular Window'),
              ),
              const SizedBox(height: 10),
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
                        offset: Offset(0, 0),
                        constraintAdjustment: <WindowPositionerConstraintAdjustment>{
                          WindowPositionerConstraintAdjustment.slideX,
                          WindowPositionerConstraintAdjustment.slideY,
                        },
                      ),
                      builder: (BuildContext context) => const MyApp());
                },
                child: const Text('Create Popup Window'),
              ),
              const SizedBox(height: 20),
              Text(
                'View #${window.view.viewId}\n'
                'Parent View: ${window.parent?.view.viewId}\n'
                'Logical ${MediaQuery.of(context).size}\n'
                'DPR: ${MediaQuery.of(context).devicePixelRatio}',
                textAlign: TextAlign.center,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
