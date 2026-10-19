// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/atspi_accessible.h"

namespace flutter {

const char* AtspiRoleName(AtspiRole role) {
  switch (role) {
    case AtspiRole::kInvalid:
      return "invalid";
    case AtspiRole::kCheckBox:
      return "check box";
    case AtspiRole::kDialog:
      return "dialog";
    case AtspiRole::kFrame:
      return "frame";
    case AtspiRole::kImage:
      return "image";
    case AtspiRole::kLabel:
      return "label";
    case AtspiRole::kList:
      return "list";
    case AtspiRole::kListItem:
      return "list item";
    case AtspiRole::kMenu:
      return "menu";
    case AtspiRole::kMenuItem:
      return "menu item";
    case AtspiRole::kPageTab:
      return "page tab";
    case AtspiRole::kPageTabList:
      return "page tab list";
    case AtspiRole::kPanel:
      return "panel";
    case AtspiRole::kPasswordText:
      return "password text";
    case AtspiRole::kProgressBar:
      return "progress bar";
    case AtspiRole::kButton:
      return "button";
    case AtspiRole::kRadioButton:
      return "radio button";
    case AtspiRole::kScrollPane:
      return "scroll pane";
    case AtspiRole::kSlider:
      return "slider";
    case AtspiRole::kToggleButton:
      return "toggle button";
    case AtspiRole::kToolTip:
      return "tool tip";
    case AtspiRole::kWindow:
      return "window";
    case AtspiRole::kApplication:
      return "application";
    case AtspiRole::kEntry:
      return "entry";
    case AtspiRole::kHeading:
      return "heading";
    case AtspiRole::kSection:
      return "section";
    case AtspiRole::kLink:
      return "link";
    case AtspiRole::kGrouping:
      return "grouping";
    case AtspiRole::kStatic:
      return "static";
    case AtspiRole::kSwitch:
      return "switch";
  }
  return "unknown";
}

const char* AtspiStateName(AtspiState state) {
  switch (state) {
    case AtspiState::kActive:
      return "active";
    case AtspiState::kChecked:
      return "checked";
    case AtspiState::kCollapsed:
      return "collapsed";
    case AtspiState::kDefunct:
      return "defunct";
    case AtspiState::kEditable:
      return "editable";
    case AtspiState::kEnabled:
      return "enabled";
    case AtspiState::kExpandable:
      return "expandable";
    case AtspiState::kExpanded:
      return "expanded";
    case AtspiState::kFocusable:
      return "focusable";
    case AtspiState::kFocused:
      return "focused";
    case AtspiState::kHasTooltip:
      return "has-tooltip";
    case AtspiState::kModal:
      return "modal";
    case AtspiState::kMultiLine:
      return "multi-line";
    case AtspiState::kPressed:
      return "pressed";
    case AtspiState::kResizable:
      return "resizable";
    case AtspiState::kSelectable:
      return "selectable";
    case AtspiState::kSelected:
      return "selected";
    case AtspiState::kSensitive:
      return "sensitive";
    case AtspiState::kShowing:
      return "showing";
    case AtspiState::kSingleLine:
      return "single-line";
    case AtspiState::kVisible:
      return "visible";
    case AtspiState::kIndeterminate:
      return "indeterminate";
    case AtspiState::kRequired:
      return "required";
    case AtspiState::kSelectableText:
      return "selectable-text";
    case AtspiState::kCheckable:
      return "checkable";
    case AtspiState::kReadOnly:
      return "read-only";
  }
  return "invalid";
}

int32_t AtspiAccessible::GetIndexInParent() {
  AtspiAccessible* parent = GetParent();
  if (parent == nullptr) {
    return -1;
  }
  size_t count = parent->GetChildCount();
  for (size_t i = 0; i < count; i++) {
    if (parent->GetChild(i) == this) {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

AtspiAccessible* AtspiAccessible::GetAccessibleAtPoint(int32_t x, int32_t y) {
  std::optional<AtspiRect> extents = GetExtents();
  if (!extents.has_value() || !extents->Contains(x, y) ||
      !GetStates().Contains(AtspiState::kShowing)) {
    return nullptr;
  }
  // Later children are drawn on top, so are checked first.
  for (size_t i = GetChildCount(); i > 0; i--) {
    AtspiAccessible* child = GetChild(i - 1);
    AtspiAccessible* result =
        child != nullptr ? child->GetAccessibleAtPoint(x, y) : nullptr;
    if (result != nullptr) {
      return result;
    }
  }
  return this;
}

}  // namespace flutter
