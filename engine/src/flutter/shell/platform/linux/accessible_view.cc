// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/accessible_view.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_message_codec.h"
#include "flutter/shell/platform/linux/atspi/atspi_text.h"

namespace flutter {

namespace {

std::string ToString(const char* value) {
  return value != nullptr ? value : "";
}

std::vector<uint8_t> Encode(const EncodableValue& value) {
  std::unique_ptr<std::vector<uint8_t>> data =
      StandardMessageCodec::GetInstance().EncodeMessage(value);
  return data ? std::move(*data) : std::vector<uint8_t>();
}

// Applies |transform| to the point (x, y).
void Transform(const FlutterTransformation& transform, double* x, double* y) {
  double tx = transform.scaleX * *x + transform.skewX * *y + transform.transX;
  double ty = transform.skewY * *x + transform.scaleY * *y + transform.transY;
  double w = transform.pers0 * *x + transform.pers1 * *y + transform.pers2;
  if (w != 0 && w != 1) {
    tx /= w;
    ty /= w;
  }
  *x = tx;
  *y = ty;
}

// Parses a numeric value such as "50%" or "3.5". Returns false if |value|
// isn't one.
bool ParseNumber(const std::string& value, double* number, bool* percent) {
  if (value.empty()) {
    return false;
  }
  char* end = nullptr;
  *number = strtod(value.c_str(), &end);
  if (end == value.c_str()) {
    return false;
  }
  while (*end == ' ') {
    end++;
  }
  *percent = *end == '%';
  if (*percent) {
    end++;
  }
  return *end == '\0' && std::isfinite(*number);
}

// The changed region between |old_text| and |new_text|, as the character
// offset it starts at and the characters removed and inserted there.
struct TextChange {
  int32_t offset;
  std::u16string removed;
  std::u16string inserted;
};
TextChange DiffText(const std::u16string& old_text,
                    const std::u16string& new_text) {
  size_t prefix = 0;
  while (prefix < old_text.size() && prefix < new_text.size() &&
         old_text[prefix] == new_text[prefix]) {
    prefix++;
  }
  size_t suffix = 0;
  while (suffix < old_text.size() - prefix &&
         suffix < new_text.size() - prefix &&
         old_text[old_text.size() - 1 - suffix] ==
             new_text[new_text.size() - 1 - suffix]) {
    suffix++;
  }
  // Don't split surrogate pairs.
  auto is_trail = [](char16_t c) { return c >= 0xdc00 && c <= 0xdfff; };
  while (prefix > 0 && prefix < old_text.size() && is_trail(old_text[prefix])) {
    prefix--;
  }
  while (suffix > 0 && is_trail(old_text[old_text.size() - suffix])) {
    suffix--;
  }
  return {atspi_text::ToCharacterOffset(old_text, static_cast<int32_t>(prefix)),
          old_text.substr(prefix, old_text.size() - prefix - suffix),
          new_text.substr(prefix, new_text.size() - prefix - suffix)};
}

constexpr AtspiState kReportedStates[] = {
    AtspiState::kActive,        AtspiState::kChecked,
    AtspiState::kCollapsed,     AtspiState::kEditable,
    AtspiState::kEnabled,       AtspiState::kExpandable,
    AtspiState::kExpanded,      AtspiState::kFocusable,
    AtspiState::kModal,         AtspiState::kMultiLine,
    AtspiState::kPressed,       AtspiState::kResizable,
    AtspiState::kSelectable,    AtspiState::kSelected,
    AtspiState::kSensitive,     AtspiState::kShowing,
    AtspiState::kSingleLine,    AtspiState::kVisible,
    AtspiState::kIndeterminate, AtspiState::kRequired,
    AtspiState::kCheckable,     AtspiState::kReadOnly,
};

// The most increase or decrease actions sent to set a value.
constexpr int32_t kMaxValueSteps = 1000;

}  // namespace

AccessibleNode::AccessibleNode(AccessibleView* view, int32_t id)
    : view_(view), id_(id), path_(view->GetPath() + "/" + std::to_string(id)) {
  flags_.struct_size = sizeof(flags_);
}

AccessibleNode::~AccessibleNode() = default;

bool AccessibleNode::is_focused() const {
  return flags_.is_focused == kFlutterTristateTrue;
}

void AccessibleNode::Update(const FlutterSemanticsNode2& node) {
  if (node.flags2 != nullptr) {
    flags_ = *node.flags2;
  } else {
    flags_ = {};
    flags_.struct_size = sizeof(flags_);
  }
  actions_ = node.actions;
  label_ = ToString(node.label);
  hint_ = ToString(node.hint);
  value_ = atspi_text::FromUtf8(ToString(node.value));
  tooltip_ = ToString(node.tooltip);
  identifier_ = ToString(node.identifier);
  text_selection_base_ = node.text_selection_base;
  text_selection_extent_ = node.text_selection_extent;
  rect_ = node.rect;
  transform_ = node.transform;
  children_.assign(node.children_in_traversal_order,
                   node.children_in_traversal_order + node.child_count);
  custom_actions_.assign(node.custom_accessibility_actions,
                         node.custom_accessibility_actions +
                             node.custom_accessibility_actions_count);
  heading_level_ = node.heading_level;
}

AtspiAccessible* AccessibleNode::GetParent() {
  if (parent_ != nullptr) {
    return parent_;
  }
  return id_ == 0 ? view_ : nullptr;
}

AtspiAccessible* AccessibleNode::GetChild(size_t index) {
  if (index >= children_.size()) {
    return nullptr;
  }
  return view_->FindNode(children_[index]);
}

AtspiRole AccessibleNode::GetRole() {
  if (flags_.is_text_field) {
    return flags_.is_obscured ? AtspiRole::kPasswordText : AtspiRole::kEntry;
  }
  if (flags_.is_slider) {
    return AtspiRole::kSlider;
  }
  if (flags_.is_link) {
    return AtspiRole::kLink;
  }
  if (flags_.is_header) {
    return AtspiRole::kHeading;
  }
  if (flags_.is_image) {
    return AtspiRole::kImage;
  }
  if (flags_.is_checked != kFlutterCheckStateNone) {
    return flags_.is_in_mutually_exclusive_group ? AtspiRole::kRadioButton
                                                 : AtspiRole::kCheckBox;
  }
  if (flags_.is_toggled != kFlutterTristateNone) {
    return AtspiRole::kSwitch;
  }
  if (flags_.is_button || flags_.is_keyboard_key) {
    return AtspiRole::kButton;
  }
  if (flags_.scopes_route && flags_.names_route) {
    return AtspiRole::kDialog;
  }
  if (flags_.has_implicit_scrolling) {
    return AtspiRole::kScrollPane;
  }
  if (children_.empty() && !label_.empty()) {
    return AtspiRole::kLabel;
  }
  return AtspiRole::kPanel;
}

std::string AccessibleNode::GetName() {
  if (!label_.empty()) {
    return label_;
  }
  if (is_unnamed_route()) {
    return GetRouteName();
  }
  // Icon buttons are often only described by their tooltip.
  return tooltip_;
}

std::string AccessibleNode::GetRouteName() {
  for (int32_t child_id : children_) {
    AccessibleNode* child = view_->FindNode(child_id);
    if (child == nullptr) {
      continue;
    }
    if (child->flags_.names_route && !child->flags_.scopes_route) {
      std::string name = child->GetName();
      if (!name.empty()) {
        return name;
      }
    }
    // Nested routes are named separately.
    if (child->flags_.scopes_route) {
      continue;
    }
    std::string name = child->GetRouteName();
    if (!name.empty()) {
      return name;
    }
  }
  return "";
}

std::string AccessibleNode::GetDescription() {
  if (!hint_.empty()) {
    return hint_;
  }
  return label_.empty() ? "" : tooltip_;
}

AtspiStateSet AccessibleNode::GetStates() {
  AtspiStateSet states;
  bool enabled = flags_.is_enabled != kFlutterTristateFalse;
  states.Set(AtspiState::kEnabled, enabled);
  states.Set(AtspiState::kSensitive, enabled);
  states.Set(AtspiState::kVisible, !flags_.is_hidden);
  states.Set(AtspiState::kShowing, !flags_.is_hidden);

  states.Set(AtspiState::kFocusable,
             flags_.is_focused != kFlutterTristateNone ||
                 HasAction(kFlutterSemanticsActionFocus));
  states.Set(AtspiState::kFocused, is_focused());

  if (flags_.is_checked != kFlutterCheckStateNone) {
    states.Add(AtspiState::kCheckable);
    states.Set(AtspiState::kChecked,
               flags_.is_checked == kFlutterCheckStateTrue);
    states.Set(AtspiState::kIndeterminate,
               flags_.is_checked == kFlutterCheckStateMixed);
  }
  if (flags_.is_toggled != kFlutterTristateNone) {
    states.Add(AtspiState::kCheckable);
    states.Set(AtspiState::kChecked, flags_.is_toggled == kFlutterTristateTrue);
  }
  if (flags_.is_selected != kFlutterTristateNone) {
    states.Add(AtspiState::kSelectable);
    states.Set(AtspiState::kSelected,
               flags_.is_selected == kFlutterTristateTrue);
  }
  if (flags_.is_expanded != kFlutterTristateNone) {
    bool expanded = flags_.is_expanded == kFlutterTristateTrue;
    states.Add(AtspiState::kExpandable);
    states.Set(AtspiState::kExpanded, expanded);
    states.Set(AtspiState::kCollapsed, !expanded);
  }
  states.Set(AtspiState::kRequired, flags_.is_required == kFlutterTristateTrue);
  if (flags_.is_text_field) {
    states.Set(AtspiState::kEditable, !flags_.is_read_only);
    states.Set(AtspiState::kReadOnly, flags_.is_read_only);
    states.Set(AtspiState::kMultiLine, flags_.is_multiline);
    states.Set(AtspiState::kSingleLine, !flags_.is_multiline);
    states.Add(AtspiState::kSelectableText);
  }
  states.Set(AtspiState::kHasTooltip, !tooltip_.empty());
  return states;
}

std::map<std::string, std::string> AccessibleNode::GetAttributes() {
  std::map<std::string, std::string> attributes;
  if (flags_.is_header && heading_level_ > 0) {
    attributes["level"] = std::to_string(heading_level_);
  }
  if (flags_.is_live_region) {
    attributes["live"] = "polite";
    attributes["container-live"] = "polite";
  }
  if (!identifier_.empty()) {
    attributes["id"] = identifier_;
  }
  if (flags_.is_text_field && !hint_.empty()) {
    attributes["placeholder-text"] = hint_;
  }
  return attributes;
}

std::vector<AtspiRelation> AccessibleNode::GetRelations() {
  std::vector<AtspiRelation> relations;
  if (GetRole() == AtspiRole::kRadioButton && parent_ != nullptr) {
    AtspiRelation group = {AtspiRelationType::kMemberOf, {}};
    for (int32_t id : parent_->children_) {
      AccessibleNode* sibling = view_->FindNode(id);
      if (sibling != nullptr && sibling->GetRole() == AtspiRole::kRadioButton) {
        group.targets.push_back(sibling);
      }
    }
    relations.push_back(std::move(group));
  }
  return relations;
}

std::optional<AtspiRect> AccessibleNode::GetExtents() {
  double xs[4] = {rect_.left, rect_.right, rect_.right, rect_.left};
  double ys[4] = {rect_.top, rect_.top, rect_.bottom, rect_.bottom};
  for (AccessibleNode* node = this; node != nullptr; node = node->parent_) {
    for (int i = 0; i < 4; i++) {
      Transform(node->transform_, &xs[i], &ys[i]);
    }
  }
  double ratio = view_->host_->GetPixelRatio();
  if (ratio <= 0) {
    ratio = 1;
  }
  double left = *std::min_element(xs, xs + 4) / ratio;
  double right = *std::max_element(xs, xs + 4) / ratio;
  double top = *std::min_element(ys, ys + 4) / ratio;
  double bottom = *std::max_element(ys, ys + 4) / ratio;
  AtspiRect extents;
  extents.x = static_cast<int32_t>(std::floor(left));
  extents.y = static_cast<int32_t>(std::floor(top));
  extents.width = static_cast<int32_t>(std::ceil(right)) - extents.x;
  extents.height = static_cast<int32_t>(std::ceil(bottom)) - extents.y;
  return extents;
}

bool AccessibleNode::GrabFocus() {
  if (HasAction(kFlutterSemanticsActionFocus)) {
    SendAction(kFlutterSemanticsActionFocus);
    return true;
  }
  return false;
}

bool AccessibleNode::ScrollTo() {
  if (HasAction(kFlutterSemanticsActionShowOnScreen)) {
    SendAction(kFlutterSemanticsActionShowOnScreen);
    return true;
  }
  return false;
}

std::vector<AccessibleNode::Action> AccessibleNode::GetActionList() {
  static constexpr struct {
    FlutterSemanticsAction action;
    const char* name;
  } kActions[] = {
      {kFlutterSemanticsActionTap, "click"},
      {kFlutterSemanticsActionLongPress, "long-press"},
      {kFlutterSemanticsActionIncrease, "increase"},
      {kFlutterSemanticsActionDecrease, "decrease"},
      {kFlutterSemanticsActionExpand, "expand"},
      {kFlutterSemanticsActionCollapse, "collapse"},
      {kFlutterSemanticsActionDismiss, "dismiss"},
      {kFlutterSemanticsActionScrollUp, "scroll-up"},
      {kFlutterSemanticsActionScrollDown, "scroll-down"},
      {kFlutterSemanticsActionScrollLeft, "scroll-left"},
      {kFlutterSemanticsActionScrollRight, "scroll-right"},
      {kFlutterSemanticsActionCopy, "copy"},
      {kFlutterSemanticsActionCut, "cut"},
      {kFlutterSemanticsActionPaste, "paste"},
  };

  std::vector<Action> actions;
  for (const auto& entry : kActions) {
    if (!HasAction(entry.action)) {
      continue;
    }
    Action action;
    action.name = entry.name;
    action.action = entry.action;
    actions.push_back(std::move(action));
  }
  for (int32_t id : custom_actions_) {
    auto it = view_->custom_actions_.find(id);
    if (it == view_->custom_actions_.end()) {
      continue;
    }
    const AccessibleView::CustomAction& custom = it->second;
    if (custom.override_action != 0) {
      // Describes a standard action, e.g. a long press that "Pins" an item.
      for (Action& action : actions) {
        if (action.action == custom.override_action) {
          action.description = custom.label;
        }
      }
      continue;
    }
    Action action;
    action.name = custom.label;
    action.description = custom.label;
    action.action = kFlutterSemanticsActionCustomAction;
    action.custom_action_id = id;
    actions.push_back(std::move(action));
  }
  return actions;
}

std::vector<AtspiAction> AccessibleNode::GetActions() {
  std::vector<AtspiAction> result;
  for (const Action& action : GetActionList()) {
    result.push_back({action.name, action.description});
  }
  return result;
}

bool AccessibleNode::DoAction(size_t index) {
  std::vector<Action> actions = GetActionList();
  if (index >= actions.size()) {
    return false;
  }
  const Action& action = actions[index];
  if (action.action == kFlutterSemanticsActionCustomAction) {
    SendAction(action.action, Encode(EncodableValue(action.custom_action_id)));
  } else {
    SendAction(action.action);
  }
  return true;
}

std::optional<AtspiValue> AccessibleNode::GetValue() {
  if (!flags_.is_slider && !HasAction(kFlutterSemanticsActionIncrease) &&
      !HasAction(kFlutterSemanticsActionDecrease)) {
    return std::nullopt;
  }
  AtspiValue value;
  value.text = atspi_text::ToUtf8(value_);
  double number;
  bool percent;
  if (ParseNumber(value.text, &number, &percent)) {
    value.current = number;
    value.maximum = percent || number <= 100 ? 100 : number;
    value.minimum = std::min(0.0, number);
  }
  return value;
}

bool AccessibleNode::SetValue(double value) {
  std::optional<AtspiValue> current = GetValue();
  if (!current.has_value()) {
    return false;
  }
  target_value_.reset();
  if (value == current->current) {
    return true;
  }
  // Flutter can only step the value, so step until the target is reached.
  FlutterSemanticsAction action = value > current->current
                                      ? kFlutterSemanticsActionIncrease
                                      : kFlutterSemanticsActionDecrease;
  if (!HasAction(action)) {
    return false;
  }
  target_value_ = value;
  remaining_steps_ = kMaxValueSteps;
  SendAction(action);
  return true;
}

void AccessibleNode::ContinueSetValue(const std::optional<AtspiValue>& old) {
  if (!target_value_.has_value()) {
    return;
  }
  double target = *target_value_;
  std::optional<AtspiValue> current = GetValue();
  // Stop if the value didn't move, e.g. at a limit.
  if (!current.has_value() || !old.has_value() ||
      current->current == old->current || --remaining_steps_ <= 0) {
    target_value_.reset();
    return;
  }
  bool increasing = current->current > old->current;
  if (increasing ? current->current >= target : current->current <= target) {
    target_value_.reset();
    return;
  }
  FlutterSemanticsAction action = increasing ? kFlutterSemanticsActionIncrease
                                             : kFlutterSemanticsActionDecrease;
  if (!HasAction(action)) {
    target_value_.reset();
    return;
  }
  SendAction(action);
}

std::u16string AccessibleNode::GetText() {
  return flags_.is_text_field ? value_ : u"";
}

int32_t AccessibleNode::GetCaretOffset() {
  if (!flags_.is_text_field || text_selection_extent_ < 0) {
    return -1;
  }
  return atspi_text::ToCharacterOffset(value_, text_selection_extent_);
}

bool AccessibleNode::GetSelection(int32_t* start, int32_t* end) {
  if (!flags_.is_text_field || text_selection_base_ < 0 ||
      text_selection_base_ == text_selection_extent_) {
    return false;
  }
  int32_t base = atspi_text::ToCharacterOffset(value_, text_selection_base_);
  int32_t extent =
      atspi_text::ToCharacterOffset(value_, text_selection_extent_);
  *start = std::min(base, extent);
  *end = std::max(base, extent);
  return true;
}

bool AccessibleNode::SetSelection(int32_t start, int32_t end) {
  if (!flags_.is_text_field ||
      !HasAction(kFlutterSemanticsActionSetSelection)) {
    return false;
  }
  SendAction(kFlutterSemanticsActionSetSelection,
             Encode(EncodableValue(EncodableMap{
                 {EncodableValue("base"),
                  EncodableValue(atspi_text::ToUtf16Offset(value_, start))},
                 {EncodableValue("extent"),
                  EncodableValue(atspi_text::ToUtf16Offset(value_, end))},
             })));
  return true;
}

bool AccessibleNode::SetTextContents(const std::u16string& text) {
  if (!IsEditableText() || !HasAction(kFlutterSemanticsActionSetText)) {
    return false;
  }
  SendAction(kFlutterSemanticsActionSetText,
             Encode(EncodableValue(atspi_text::ToUtf8(text))));
  return true;
}

bool AccessibleNode::DoClipboardAction(ClipboardAction action) {
  FlutterSemanticsAction semantics_action;
  switch (action) {
    case ClipboardAction::kCopy:
      semantics_action = kFlutterSemanticsActionCopy;
      break;
    case ClipboardAction::kCut:
      semantics_action = kFlutterSemanticsActionCut;
      break;
    case ClipboardAction::kPaste:
      semantics_action = kFlutterSemanticsActionPaste;
      break;
  }
  if (!HasAction(semantics_action)) {
    return false;
  }
  SendAction(semantics_action);
  return true;
}

bool AccessibleNode::IsSelectionContainer() {
  for (int32_t id : children_) {
    AccessibleNode* child = view_->FindNode(id);
    if (child != nullptr && child->flags_.is_selected != kFlutterTristateNone) {
      return true;
    }
  }
  return false;
}

std::vector<size_t> AccessibleNode::GetSelectedChildren() {
  std::vector<size_t> selected;
  for (size_t i = 0; i < children_.size(); i++) {
    AccessibleNode* child = view_->FindNode(children_[i]);
    if (child != nullptr && child->flags_.is_selected == kFlutterTristateTrue) {
      selected.push_back(i);
    }
  }
  return selected;
}

bool AccessibleNode::SelectChild(size_t index) {
  AccessibleNode* child = static_cast<AccessibleNode*>(GetChild(index));
  if (child == nullptr || child->flags_.is_selected == kFlutterTristateNone) {
    return false;
  }
  if (child->flags_.is_selected == kFlutterTristateTrue) {
    return true;
  }
  // Selectable items, e.g. tabs and chips, are selected by tapping them.
  if (!child->HasAction(kFlutterSemanticsActionTap)) {
    return false;
  }
  child->SendAction(kFlutterSemanticsActionTap);
  return true;
}

void AccessibleNode::SendAction(FlutterSemanticsAction action,
                                std::vector<uint8_t> data) {
  view_->host_->DispatchSemanticsAction(id_, action, std::move(data));
}

AccessibleView::AccessibleView(Host* host,
                               AtspiEventSink* sink,
                               AtspiAccessible* parent,
                               std::string path)
    : host_(host), sink_(sink), parent_(parent), path_(std::move(path)) {
  title_ = host_->GetAccessibleTitle();
  window_states_ = GetStates();
}

AccessibleView::~AccessibleView() = default;

AccessibleNode* AccessibleView::FindNode(int32_t id) {
  auto it = nodes_.find(id);
  return it != nodes_.end() ? it->second.get() : nullptr;
}

AtspiAccessible* AccessibleView::FindByPath(std::string_view path) {
  if (path == path_) {
    return this;
  }
  if (path.size() <= path_.size() + 1 ||
      path.substr(0, path_.size()) != path_ || path[path_.size()] != '/') {
    return nullptr;
  }
  std::string_view id_text = path.substr(path_.size() + 1);
  int32_t id = 0;
  for (char c : id_text) {
    if (c < '0' || c > '9' || id > 100000000) {
      return nullptr;
    }
    id = id * 10 + (c - '0');
  }
  return FindNode(id);
}

size_t AccessibleView::GetChildCount() {
  return FindNode(0) != nullptr ? 1 : 0;
}

AtspiAccessible* AccessibleView::GetChild(size_t index) {
  return index == 0 ? FindNode(0) : nullptr;
}

AtspiStateSet AccessibleView::GetStates() {
  AtspiStateSet states = host_->GetWindowStates();
  states.Add(AtspiState::kEnabled);
  states.Add(AtspiState::kSensitive);
  states.Add(AtspiState::kShowing);
  states.Add(AtspiState::kVisible);
  states.Add(AtspiState::kFocusable);
  return states;
}

AccessibleView::Snapshot AccessibleView::TakeSnapshot(AccessibleNode* node) {
  Snapshot snapshot;
  snapshot.role = node->GetRole();
  snapshot.name = node->GetName();
  snapshot.description = node->GetDescription();
  snapshot.states = node->GetStates();
  snapshot.value = node->GetValue();
  snapshot.text = node->GetText();
  snapshot.caret = node->GetCaretOffset();
  snapshot.selection_start = snapshot.selection_end = snapshot.caret;
  node->GetSelection(&snapshot.selection_start, &snapshot.selection_end);
  snapshot.children = node->children_;
  return snapshot;
}

void AccessibleView::Update(const FlutterSemanticsUpdate2& update) {
  for (size_t i = 0; i < update.custom_action_count; i++) {
    const FlutterSemanticsCustomAction2* action = update.custom_actions[i];
    custom_actions_[action->id] = {ToString(action->label),
                                   action->override_action};
  }

  // Apply the new properties, remembering the old ones.
  std::vector<std::pair<AccessibleNode*, std::optional<Snapshot>>> updated;
  bool root_added = false;
  for (size_t i = 0; i < update.node_count; i++) {
    const FlutterSemanticsNode2* data = update.nodes[i];
    std::unique_ptr<AccessibleNode>& node = nodes_[data->id];
    std::optional<Snapshot> snapshot;
    if (node) {
      snapshot = TakeSnapshot(node.get());
    } else {
      node = std::make_unique<AccessibleNode>(this, data->id);
      root_added |= data->id == 0;
    }
    node->Update(*data);
    updated.emplace_back(node.get(), std::move(snapshot));
  }

  // Link children to their parents. Nodes that are still children are
  // kept, even if they moved.
  std::map<int32_t, bool> kept;
  for (auto& [node, snapshot] : updated) {
    for (int32_t child_id : node->children_) {
      kept[child_id] = true;
      AccessibleNode* child = FindNode(child_id);
      if (child != nullptr) {
        child->parent_ = node;
      }
    }
  }

  bool active = sink_->IsActive();

  // Report and remove children that are no longer present. The objects are
  // removed after reporting, so the events can refer to them.
  std::vector<int32_t> removed;
  for (auto& [node, snapshot] : updated) {
    if (!snapshot.has_value()) {
      continue;
    }
    std::set<int32_t> new_children(node->children_.begin(),
                                   node->children_.end());
    for (size_t i = snapshot->children.size(); i > 0; i--) {
      int32_t child_id = snapshot->children[i - 1];
      if (new_children.count(child_id) != 0) {
        continue;
      }
      AccessibleNode* child = FindNode(child_id);
      if (active && child != nullptr) {
        Emit({.source = node,
              .member = "ChildrenChanged",
              .detail = "remove",
              .detail1 = static_cast<int32_t>(i - 1),
              .data = child});
      }
      if (kept.count(child_id) == 0) {
        removed.push_back(child_id);
      }
    }
  }
  for (int32_t id : removed) {
    RemoveSubtree(id, kept);
  }

  if (root_added && active) {
    Emit({.source = this,
          .member = "ChildrenChanged",
          .detail = "add",
          .detail1 = 0,
          .data = FindNode(0)});
  }

  std::set<AccessibleNode*> selection_changed;
  for (auto& [node, snapshot] : updated) {
    if (!snapshot.has_value()) {
      continue;
    }
    if (active) {
      EmitChanges(node, *snapshot);
      bool selected = node->GetStates().Contains(AtspiState::kSelected);
      if (selected != snapshot->states.Contains(AtspiState::kSelected) &&
          node->parent_ != nullptr) {
        selection_changed.insert(node->parent_);
      }
    }
    node->ContinueSetValue(snapshot->value);
  }
  for (AccessibleNode* container : selection_changed) {
    Emit({.source = container, .member = "SelectionChanged"});
  }

  // Report focus last, after the focused object has been described.
  int32_t focused_id = focused_id_;
  AccessibleNode* old_focus = FindNode(focused_id_);
  if (old_focus == nullptr || !old_focus->is_focused()) {
    focused_id = -1;
  }
  for (auto& [node, snapshot] : updated) {
    if (node->is_focused() &&
        (focused_id == -1 || !snapshot.has_value() ||
         !snapshot->states.Contains(AtspiState::kFocused))) {
      focused_id = node->id();
    }
  }
  if (focused_id != focused_id_) {
    if (active && old_focus != nullptr) {
      Emit({.source = old_focus,
            .member = "StateChanged",
            .detail = "focused",
            .detail1 = 0});
    }
    focused_id_ = focused_id;
    AccessibleNode* new_focus = FindNode(focused_id_);
    if (active && new_focus != nullptr) {
      Emit({.source = new_focus,
            .member = "StateChanged",
            .detail = "focused",
            .detail1 = 1});
    }
  }
}

void AccessibleView::RemoveSubtree(int32_t id,
                                   const std::map<int32_t, bool>& kept) {
  auto it = nodes_.find(id);
  if (it == nodes_.end()) {
    return;
  }
  std::unique_ptr<AccessibleNode> node = std::move(it->second);
  nodes_.erase(it);
  if (focused_id_ == id) {
    focused_id_ = -1;
  }
  for (int32_t child_id : node->children_) {
    AccessibleNode* child = FindNode(child_id);
    if (child == nullptr) {
      continue;
    }
    // Children that moved to another parent are kept.
    if (kept.count(child_id) != 0) {
      if (child->parent_ == node.get()) {
        child->parent_ = nullptr;
      }
      continue;
    }
    RemoveSubtree(child_id, kept);
  }
}

void AccessibleView::EmitChanges(AccessibleNode* node, const Snapshot& old) {
  std::set<int32_t> old_children(old.children.begin(), old.children.end());
  for (size_t i = 0; i < node->children_.size(); i++) {
    int32_t child_id = node->children_[i];
    AccessibleNode* child = FindNode(child_id);
    if (old_children.count(child_id) == 0 && child != nullptr) {
      Emit({.source = node,
            .member = "ChildrenChanged",
            .detail = "add",
            .detail1 = static_cast<int32_t>(i),
            .data = child});
    }
  }

  std::string name = node->GetName();
  if (name != old.name) {
    Emit({.source = node,
          .member = "PropertyChange",
          .detail = "accessible-name",
          .data = name});
    if (node->is_live_region() && !name.empty()) {
      Emit({.source = node,
            .member = "Announcement",
            .detail1 = 1,
            .data = name});
    }
  }
  std::string description = node->GetDescription();
  if (description != old.description) {
    Emit({.source = node,
          .member = "PropertyChange",
          .detail = "accessible-description",
          .data = description});
  }
  AtspiRole role = node->GetRole();
  if (role != old.role) {
    Emit({.source = node,
          .member = "PropertyChange",
          .detail = "accessible-role",
          .data = static_cast<int32_t>(role)});
  }

  AtspiStateSet states = node->GetStates();
  EmitStateChanges(node, old.states, states);

  std::optional<AtspiValue> value = node->GetValue();
  if (value.has_value() && value != old.value) {
    Emit({.source = node,
          .member = "PropertyChange",
          .detail = "accessible-value",
          .data = static_cast<int32_t>(value->current)});
  }

  if (node->HasText()) {
    std::u16string text = node->GetText();
    if (text != old.text) {
      TextChange change = DiffText(old.text, text);
      if (!change.removed.empty()) {
        Emit({.source = node,
              .member = "TextChanged",
              .detail = "delete",
              .detail1 = change.offset,
              .detail2 = atspi_text::CharacterCount(change.removed),
              .data = atspi_text::ToUtf8(change.removed)});
      }
      if (!change.inserted.empty()) {
        Emit({.source = node,
              .member = "TextChanged",
              .detail = "insert",
              .detail1 = change.offset,
              .detail2 = atspi_text::CharacterCount(change.inserted),
              .data = atspi_text::ToUtf8(change.inserted)});
      }
    }
    int32_t caret = node->GetCaretOffset();
    int32_t selection_start = caret, selection_end = caret;
    node->GetSelection(&selection_start, &selection_end);
    // Moving the caret without a selection isn't a selection change.
    bool had_selection = old.selection_start != old.selection_end;
    bool has_selection = selection_start != selection_end;
    if ((had_selection || has_selection) &&
        (selection_start != old.selection_start ||
         selection_end != old.selection_end)) {
      Emit({.source = node, .member = "TextSelectionChanged"});
    }
    if (caret != old.caret && caret >= 0) {
      Emit({.source = node, .member = "TextCaretMoved", .detail1 = caret});
    }
  }
}

void AccessibleView::EmitStateChanges(AtspiAccessible* source,
                                      AtspiStateSet old_states,
                                      AtspiStateSet new_states) {
  for (AtspiState state : kReportedStates) {
    bool value = new_states.Contains(state);
    if (value != old_states.Contains(state)) {
      Emit({.source = source,
            .member = "StateChanged",
            .detail = AtspiStateName(state),
            .detail1 = value ? 1 : 0});
    }
  }
}

void AccessibleView::Clear() {
  AccessibleNode* root = FindNode(0);
  if (root != nullptr && sink_->IsActive()) {
    Emit({.source = this,
          .member = "ChildrenChanged",
          .detail = "remove",
          .detail1 = 0,
          .data = root});
  }
  nodes_.clear();
  custom_actions_.clear();
  focused_id_ = -1;
}

void AccessibleView::OnWindowStateChanged() {
  std::string title = host_->GetAccessibleTitle();
  AtspiStateSet states = GetStates();
  bool title_changed = title != title_;
  AtspiStateSet old_states = window_states_;
  title_ = title;
  window_states_ = states;
  if (!sink_->IsActive()) {
    return;
  }

  if (title_changed) {
    Emit({.source = this,
          .member = "PropertyChange",
          .detail = "accessible-name",
          .data = title});
  }
  bool active = states.Contains(AtspiState::kActive);
  if (active != old_states.Contains(AtspiState::kActive)) {
    Emit({.source = this,
          .category = "Window",
          .member = active ? "Activate" : "Deactivate"});
  }
  EmitStateChanges(this, old_states, states);
}

void AccessibleView::Announce(const std::string& message, bool assertive) {
  if (sink_->IsActive()) {
    Emit({.source = this,
          .member = "Announcement",
          .detail1 = assertive ? 2 : 1,
          .data = message});
  }
}

void AccessibleView::Emit(AtspiEvent event) {
  sink_->Emit(event);
}

}  // namespace flutter
