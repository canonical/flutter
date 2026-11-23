// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_SOFTWARE_COMPOSITOR_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_SOFTWARE_COMPOSITOR_H_

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux_next/compositor.h"

namespace flutter {

// A compositor where frames are rendered in software into CPU memory.
class SoftwareCompositor : public Compositor {
 public:
  SoftwareCompositor();
  ~SoftwareCompositor() override;

  // |Compositor|
  FlutterRendererConfig GetRendererConfig() override;

  // |Compositor|
  bool CreateBackingStore(const FlutterBackingStoreConfig& config,
                          FlutterBackingStore* result) override;

  // |Compositor|
  bool CollectBackingStore(const FlutterBackingStore* store) override;

 protected:
  // |Compositor|
  bool PresentToView(FlutterLinuxView* view,
                     const FlutterLayer** layers,
                     size_t layers_count) override;

 private:
  FML_DISALLOW_COPY_AND_ASSIGN(SoftwareCompositor);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_SOFTWARE_COMPOSITOR_H_
