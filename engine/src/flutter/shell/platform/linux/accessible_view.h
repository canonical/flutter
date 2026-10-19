// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_ACCESSIBLE_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_ACCESSIBLE_VIEW_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/atspi_accessible.h"

namespace flutter {

class AccessibleView;

// A Flutter semantics node exposed over AT-SPI.
class AccessibleNode : public AtspiAccessible {
 public:
  AccessibleNode(AccessibleView* view, int32_t id);
  ~AccessibleNode() override;

  int32_t id() const { return id_; }
  const std::vector<int32_t>& children() const { return children_; }
  bool is_focused() const;
  bool is_live_region() const { return flags_.is_live_region; }

  // Replaces the node's properties with those in |node|.
  void Update(const FlutterSemanticsNode2& node);

  // |AtspiAccessible|
  const std::string& GetPath() const override { return path_; }
  AtspiAccessible* GetParent() override;
  size_t GetChildCount() override { return children_.size(); }
  AtspiAccessible* GetChild(size_t index) override;
  AtspiRole GetRole() override;
  std::string GetName() override;
  std::string GetDescription() override;
  std::string GetHelpText() override { return tooltip_; }
  std::string GetAccessibleId() override { return identifier_; }
  AtspiStateSet GetStates() override;
  std::map<std::string, std::string> GetAttributes() override;
  std::vector<AtspiRelation> GetRelations() override;
  std::optional<AtspiRect> GetExtents() override;
  bool GrabFocus() override;
  bool ScrollTo() override;
  std::vector<AtspiAction> GetActions() override;
  bool DoAction(size_t index) override;
  std::optional<AtspiValue> GetValue() override;
  bool SetValue(double value) override;
  bool HasText() override { return flags_.is_text_field; }
  bool IsEditableText() override {
    return flags_.is_text_field && !flags_.is_read_only;
  }
  std::u16string GetText() override;
  int32_t GetCaretOffset() override;
  bool GetSelection(int32_t* start, int32_t* end) override;
  bool SetSelection(int32_t start, int32_t end) override;
  bool SetTextContents(const std::u16string& text) override;
  bool DoClipboardAction(ClipboardAction action) override;
  bool IsSelectionContainer() override;
  std::vector<size_t> GetSelectedChildren() override;
  bool SelectChild(size_t index) override;
  bool IsImage() override { return flags_.is_image; }

 private:
  friend class AccessibleView;

  struct Action {
    std::string name;
    std::string description;
    FlutterSemanticsAction action;
    // For custom actions.
    int32_t custom_action_id = 0;
  };

  std::vector<Action> GetActionList();
  bool HasAction(FlutterSemanticsAction action) const {
    return (actions_ & action) != 0;
  }
  void SendAction(FlutterSemanticsAction action,
                  std::vector<uint8_t> data = {});

  // Whether this node is a route without its own name.
  bool is_unnamed_route() const {
    return flags_.scopes_route && !flags_.names_route && id_ != 0;
  }
  // The name of the route this node scopes, from the first descendant that
  // names it.
  std::string GetRouteName();

  // Steps the value towards |target_value_| after it changed from |old|.
  void ContinueSetValue(const std::optional<AtspiValue>& old);

  AccessibleView* view_;
  int32_t id_;
  std::string path_;
  AccessibleNode* parent_ = nullptr;

  FlutterSemanticsFlags flags_ = {};
  int64_t actions_ = 0;
  std::string label_;
  std::string hint_;
  std::u16string value_;
  std::string tooltip_;
  std::string identifier_;
  int32_t text_selection_base_ = -1;
  int32_t text_selection_extent_ = -1;
  FlutterRect rect_ = {};
  FlutterTransformation transform_ = {};
  std::vector<int32_t> children_;
  std::vector<int32_t> custom_actions_;
  int32_t heading_level_ = 0;

  // The value being set by repeatedly increasing or decreasing it.
  std::optional<double> target_value_;
  int32_t remaining_steps_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(AccessibleNode);
};

// The accessible object for a Flutter view's window, containing the view's
// semantics tree.
class AccessibleView : public AtspiAccessible {
 public:
  // The window and engine the view is shown with.
  class Host {
   public:
    virtual ~Host() = default;

    virtual std::string GetAccessibleTitle() = 0;
    // kFrame, kDialog or kWindow.
    virtual AtspiRole GetAccessibleRole() = 0;
    // The states of the window, e.g. active, modal and resizable.
    virtual AtspiStateSet GetWindowStates() = 0;
    // The size of the window content in logical pixels.
    virtual AtspiRect GetWindowExtents() = 0;
    // The number of semantics units (physical pixels) per logical pixel.
    virtual double GetPixelRatio() = 0;
    // The relations of the window to other windows, e.g. its parent.
    virtual std::vector<AtspiRelation> GetWindowRelations() { return {}; }
    virtual void DispatchSemanticsAction(uint64_t node_id,
                                         FlutterSemanticsAction action,
                                         std::vector<uint8_t> data) = 0;
  };

  // Creates the accessible for a window with D-Bus object |path|. |parent|
  // is the application root, and events are sent to |sink|.
  AccessibleView(Host* host,
                 AtspiEventSink* sink,
                 AtspiAccessible* parent,
                 std::string path);
  ~AccessibleView() override;

  // Applies a (partial) update to the semantics tree.
  void Update(const FlutterSemanticsUpdate2& update);

  // Removes the semantics tree, when semantics are disabled.
  void Clear();

  // Called when the window title or state changes.
  void OnWindowStateChanged();

  // Asks assistive technologies to announce |message|.
  void Announce(const std::string& message, bool assertive);

  // Returns the object for |path|, which is this or one of its nodes.
  AtspiAccessible* FindByPath(std::string_view path);

  AccessibleNode* FindNode(int32_t id);

  // |AtspiAccessible|
  const std::string& GetPath() const override { return path_; }
  AtspiAccessible* GetParent() override { return parent_; }
  size_t GetChildCount() override;
  AtspiAccessible* GetChild(size_t index) override;
  AtspiRole GetRole() override { return host_->GetAccessibleRole(); }
  std::string GetName() override { return host_->GetAccessibleTitle(); }
  AtspiStateSet GetStates() override;
  std::vector<AtspiRelation> GetRelations() override {
    return host_->GetWindowRelations();
  }
  std::optional<AtspiRect> GetExtents() override {
    return host_->GetWindowExtents();
  }
  AtspiLayer GetLayer() override { return AtspiLayer::kWindow; }

 private:
  friend class AccessibleNode;

  // The properties of a node that changes are reported for.
  struct Snapshot {
    AtspiRole role;
    std::string name;
    std::string description;
    AtspiStateSet states;
    std::optional<AtspiValue> value;
    std::u16string text;
    int32_t caret;
    int32_t selection_start;
    int32_t selection_end;
    std::vector<int32_t> children;
  };
  Snapshot TakeSnapshot(AccessibleNode* node);

  // Removes the node with |id| and its descendants that are not in |kept|.
  void RemoveSubtree(int32_t id, const std::map<int32_t, bool>& kept);

  // Emits events for the changes to |node| since |old|.
  void EmitChanges(AccessibleNode* node, const Snapshot& old);
  void EmitStateChanges(AtspiAccessible* source,
                        AtspiStateSet old_states,
                        AtspiStateSet new_states);
  void Emit(AtspiEvent event);

  // The label of custom actions, by ID, and the description of standard
  // actions overridden by custom actions.
  struct CustomAction {
    std::string label;
    FlutterSemanticsAction override_action;
  };

  Host* host_;
  AtspiEventSink* sink_;
  AtspiAccessible* parent_;
  std::string path_;
  std::map<int32_t, std::unique_ptr<AccessibleNode>> nodes_;
  std::map<int32_t, CustomAction> custom_actions_;
  int32_t focused_id_ = -1;

  // The window properties last reported.
  std::string title_;
  AtspiStateSet window_states_;

  FML_DISALLOW_COPY_AND_ASSIGN(AccessibleView);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_ACCESSIBLE_VIEW_H_
