// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_POPUP_POSITIONER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_POPUP_POSITIONER_H_

#include "flutter/shell/platform/linux/window.h"

namespace flutter {

// Places a popup of |width| x |height| according to |positioner|, for display
// servers that leave this to the client. Coordinates are relative to the
// parent's content. If |bounds| is not empty, the popup is kept inside it
// using the positioner's flip and slide constraint adjustments. Returns the
// position of the popup's top left corner.
void PlacePopup(const Window::Positioner& positioner,
                int width,
                int height,
                const LogicalRect& bounds,
                int* x,
                int* y);

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_POPUP_POSITIONER_H_
