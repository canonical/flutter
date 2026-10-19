// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/accessible_view.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_message_codec.h"
#include "gtest/gtest.h"

namespace flutter {
namespace {

class FakeHost : public AccessibleView::Host {
 public:
  struct DispatchedAction {
    uint64_t node_id;
    FlutterSemanticsAction action;
    std::vector<uint8_t> data;
  };

  std::string GetAccessibleTitle() override { return title; }
  AtspiRole GetAccessibleRole() override { return AtspiRole::kFrame; }
  AtspiStateSet GetWindowStates() override {
    AtspiStateSet states;
    states.Set(AtspiState::kActive, active);
    return states;
  }
  AtspiRect GetWindowExtents() override { return {0, 0, 400, 300}; }
  double GetPixelRatio() override { return pixel_ratio; }
  void DispatchSemanticsAction(uint64_t node_id,
                               FlutterSemanticsAction action,
                               std::vector<uint8_t> data) override {
    actions.push_back({node_id, action, std::move(data)});
  }

  std::string title = "Window";
  bool active = false;
  double pixel_ratio = 1.0;
  std::vector<DispatchedAction> actions;
};

class FakeSink : public AtspiEventSink {
 public:
  bool IsActive() override { return active; }
  void Emit(const AtspiEvent& event) override { events.push_back(event); }

  // Events formatted as "path member:detail detail1".
  std::vector<std::string> Describe() const {
    std::vector<std::string> result;
    for (const AtspiEvent& event : events) {
      std::string text = event.source->GetPath() + " " + event.member;
      if (!event.detail.empty()) {
        text += ":" + event.detail;
      }
      text += " " + std::to_string(event.detail1);
      result.push_back(text);
    }
    return result;
  }

  bool active = true;
  std::vector<AtspiEvent> events;
};

// A semantics node that can be added to an update.
struct TestNode {
  explicit TestNode(int32_t id, std::vector<int32_t> children = {})
      : children(std::move(children)) {
    flags.struct_size = sizeof(flags);
    node.struct_size = sizeof(node);
    node.id = id;
    node.label = label.c_str();
    node.hint = "";
    node.value = "";
    node.tooltip = "";
    node.identifier = "";
    node.increased_value = "";
    node.decreased_value = "";
    node.text_selection_base = -1;
    node.text_selection_extent = -1;
    node.rect = {0, 0, 100, 50};
    node.transform.scaleX = 1;
    node.transform.scaleY = 1;
    node.transform.pers2 = 1;
    node.flags2 = &flags;
  }

  void SetLabel(std::string value) {
    label = std::move(value);
    node.label = label.c_str();
  }

  void SetValue(std::string text) {
    value = std::move(text);
    node.value = value.c_str();
  }

  FlutterSemanticsNode2* Get() {
    node.child_count = children.size();
    node.children_in_traversal_order = children.data();
    node.children_in_hit_test_order = children.data();
    return &node;
  }

  FlutterSemanticsNode2 node = {};
  FlutterSemanticsFlags flags = {};
  std::vector<int32_t> children;
  std::string label;
  std::string value;
};

FlutterSemanticsUpdate2 MakeUpdate(std::vector<FlutterSemanticsNode2*>& nodes) {
  FlutterSemanticsUpdate2 update = {};
  update.struct_size = sizeof(update);
  update.node_count = nodes.size();
  update.nodes = nodes.data();
  return update;
}

void Apply(AccessibleView& view, std::vector<TestNode*> test_nodes) {
  std::vector<FlutterSemanticsNode2*> nodes;
  for (TestNode* node : test_nodes) {
    nodes.push_back(node->Get());
  }
  FlutterSemanticsUpdate2 update = MakeUpdate(nodes);
  view.Update(update);
}

class AccessibleViewTest : public ::testing::Test {
 protected:
  FakeHost host;
  FakeSink sink;
  AccessibleView view{&host, &sink, nullptr, "/w"};
};

TEST_F(AccessibleViewTest, Tree) {
  TestNode root(0, {1, 2});
  TestNode button(1);
  button.SetLabel("OK");
  button.flags.is_button = true;
  button.flags.is_enabled = kFlutterTristateTrue;
  TestNode text(2);
  text.SetLabel("Hello");
  Apply(view, {&root, &button, &text});

  EXPECT_EQ(view.GetName(), "Window");
  ASSERT_EQ(view.GetChildCount(), 1u);
  AtspiAccessible* root_object = view.GetChild(0);
  ASSERT_NE(root_object, nullptr);
  EXPECT_EQ(root_object->GetPath(), "/w/0");
  EXPECT_EQ(root_object->GetParent(), &view);
  EXPECT_EQ(root_object->GetRole(), AtspiRole::kPanel);
  ASSERT_EQ(root_object->GetChildCount(), 2u);

  AtspiAccessible* button_object = root_object->GetChild(0);
  EXPECT_EQ(button_object->GetRole(), AtspiRole::kButton);
  EXPECT_EQ(button_object->GetName(), "OK");
  EXPECT_EQ(button_object->GetIndexInParent(), 0);
  EXPECT_EQ(button_object->GetParent(), root_object);
  EXPECT_TRUE(button_object->GetStates().Contains(AtspiState::kEnabled));
  EXPECT_TRUE(button_object->GetStates().Contains(AtspiState::kShowing));

  AtspiAccessible* text_object = root_object->GetChild(1);
  EXPECT_EQ(text_object->GetRole(), AtspiRole::kLabel);
  EXPECT_EQ(text_object->GetIndexInParent(), 1);

  EXPECT_EQ(view.FindByPath("/w/2"), text_object);
  EXPECT_EQ(view.FindByPath("/w"), &view);
  EXPECT_EQ(view.FindByPath("/w/3"), nullptr);
  EXPECT_EQ(view.FindByPath("/w/x"), nullptr);
  EXPECT_EQ(view.FindByPath("/wx/0"), nullptr);

  EXPECT_EQ(sink.Describe(),
            std::vector<std::string>{"/w ChildrenChanged:add 0"});
}

TEST_F(AccessibleViewTest, Roles) {
  TestNode root(0, {1, 2, 3, 4, 5});
  TestNode field(1);
  field.flags.is_text_field = true;
  TestNode password(2);
  password.flags.is_text_field = true;
  password.flags.is_obscured = true;
  TestNode check(3);
  check.flags.is_checked = kFlutterCheckStateTrue;
  TestNode radio(4);
  radio.flags.is_checked = kFlutterCheckStateFalse;
  radio.flags.is_in_mutually_exclusive_group = true;
  TestNode toggle(5);
  toggle.flags.is_toggled = kFlutterTristateTrue;
  Apply(view, {&root, &field, &password, &check, &radio, &toggle});

  EXPECT_EQ(view.FindNode(1)->GetRole(), AtspiRole::kEntry);
  EXPECT_EQ(view.FindNode(2)->GetRole(), AtspiRole::kPasswordText);
  EXPECT_EQ(view.FindNode(3)->GetRole(), AtspiRole::kCheckBox);
  EXPECT_TRUE(view.FindNode(3)->GetStates().Contains(AtspiState::kChecked));
  EXPECT_EQ(view.FindNode(4)->GetRole(), AtspiRole::kRadioButton);
  EXPECT_FALSE(view.FindNode(4)->GetStates().Contains(AtspiState::kChecked));
  EXPECT_EQ(view.FindNode(5)->GetRole(), AtspiRole::kSwitch);
  EXPECT_TRUE(view.FindNode(5)->GetStates().Contains(AtspiState::kChecked));
}

TEST_F(AccessibleViewTest, Extents) {
  host.pixel_ratio = 2.0;
  TestNode root(0, {1});
  root.node.rect = {0, 0, 800, 600};
  root.node.transform.scaleX = 2;
  root.node.transform.scaleY = 2;
  TestNode child(1);
  child.node.rect = {0, 0, 20, 10};
  child.node.transform.transX = 30;
  child.node.transform.transY = 40;
  Apply(view, {&root, &child});

  std::optional<AtspiRect> extents = view.FindNode(1)->GetExtents();
  ASSERT_TRUE(extents.has_value());
  EXPECT_EQ(*extents, (AtspiRect{30, 40, 20, 10}));
  EXPECT_EQ(view.GetAccessibleAtPoint(35, 45), view.FindNode(1));
  EXPECT_EQ(view.GetAccessibleAtPoint(5, 5), view.FindNode(0));
}

TEST_F(AccessibleViewTest, ChangeEvents) {
  TestNode root(0, {1});
  TestNode check(1);
  check.SetLabel("Option");
  check.flags.is_checked = kFlutterCheckStateFalse;
  Apply(view, {&root, &check});
  sink.events.clear();

  check.SetLabel("Choice");
  check.flags.is_checked = kFlutterCheckStateTrue;
  Apply(view, {&check});
  EXPECT_EQ(sink.Describe(),
            (std::vector<std::string>{"/w/1 PropertyChange:accessible-name 0",
                                      "/w/1 StateChanged:checked 1"}));
  EXPECT_EQ(std::get<std::string>(sink.events[0].data), "Choice");
}

TEST_F(AccessibleViewTest, AddAndRemoveChildren) {
  TestNode root(0, {1, 2});
  TestNode a(1, {3});
  TestNode b(2);
  TestNode c(3);
  Apply(view, {&root, &a, &b, &c});
  sink.events.clear();

  // Remove 1 (and its child 3), add 4.
  root.children = {2, 4};
  TestNode d(4);
  Apply(view, {&root, &d});
  EXPECT_EQ(sink.Describe(),
            (std::vector<std::string>{"/w/0 ChildrenChanged:remove 0",
                                      "/w/0 ChildrenChanged:add 1"}));
  EXPECT_EQ(view.FindNode(1), nullptr);
  EXPECT_EQ(view.FindNode(3), nullptr);
  EXPECT_NE(view.FindNode(4), nullptr);
  EXPECT_EQ(view.FindNode(4)->GetIndexInParent(), 1);
}

TEST_F(AccessibleViewTest, MoveChild) {
  TestNode root(0, {1, 2});
  TestNode a(1, {3});
  TestNode b(2);
  TestNode c(3);
  Apply(view, {&root, &a, &b, &c});

  // Move 3 from 1 to 2, and remove 1.
  root.children = {2};
  b.children = {3};
  Apply(view, {&root, &b});
  EXPECT_EQ(view.FindNode(1), nullptr);
  ASSERT_NE(view.FindNode(3), nullptr);
  EXPECT_EQ(view.FindNode(3)->GetParent(), view.FindNode(2));
}

TEST_F(AccessibleViewTest, Focus) {
  TestNode root(0, {1, 2});
  TestNode a(1);
  a.flags.is_focused = kFlutterTristateTrue;
  TestNode b(2);
  b.flags.is_focused = kFlutterTristateFalse;
  Apply(view, {&root, &a, &b});
  EXPECT_EQ(sink.Describe(),
            (std::vector<std::string>{"/w ChildrenChanged:add 0",
                                      "/w/1 StateChanged:focused 1"}));
  sink.events.clear();

  a.flags.is_focused = kFlutterTristateFalse;
  b.flags.is_focused = kFlutterTristateTrue;
  Apply(view, {&a, &b});
  EXPECT_EQ(sink.Describe(),
            (std::vector<std::string>{"/w/1 StateChanged:focused 0",
                                      "/w/2 StateChanged:focused 1"}));
}

TEST_F(AccessibleViewTest, TextChanges) {
  TestNode root(0, {1});
  TestNode field(1);
  field.flags.is_text_field = true;
  field.SetValue("hello");
  field.node.text_selection_base = 5;
  field.node.text_selection_extent = 5;
  Apply(view, {&root, &field});
  AtspiAccessible* object = view.FindNode(1);
  EXPECT_TRUE(object->HasText());
  EXPECT_EQ(object->GetText(), u"hello");
  EXPECT_EQ(object->GetCaretOffset(), 5);
  EXPECT_TRUE(object->GetStates().Contains(AtspiState::kEditable));
  sink.events.clear();

  field.SetValue("help me");
  field.node.text_selection_base = 7;
  field.node.text_selection_extent = 7;
  Apply(view, {&field});
  EXPECT_EQ(sink.Describe(),
            (std::vector<std::string>{"/w/1 TextChanged:delete 3",
                                      "/w/1 TextChanged:insert 3",
                                      "/w/1 TextCaretMoved 7"}));
  EXPECT_EQ(std::get<std::string>(sink.events[0].data), "lo");
  EXPECT_EQ(std::get<std::string>(sink.events[1].data), "p me");
  sink.events.clear();

  field.node.text_selection_base = 0;
  Apply(view, {&field});
  EXPECT_EQ(sink.Describe(),
            std::vector<std::string>{"/w/1 TextSelectionChanged 0"});
  int32_t start, end;
  ASSERT_TRUE(object->GetSelection(&start, &end));
  EXPECT_EQ(start, 0);
  EXPECT_EQ(end, 7);
}

TEST_F(AccessibleViewTest, Actions) {
  TestNode root(0, {1});
  TestNode button(1);
  button.node.actions = static_cast<FlutterSemanticsAction>(
      kFlutterSemanticsActionTap | kFlutterSemanticsActionLongPress |
      kFlutterSemanticsActionFocus);
  Apply(view, {&root, &button});

  AtspiAccessible* object = view.FindNode(1);
  std::vector<AtspiAction> actions = object->GetActions();
  ASSERT_EQ(actions.size(), 2u);
  EXPECT_EQ(actions[0].name, "click");
  EXPECT_EQ(actions[1].name, "long-press");
  EXPECT_TRUE(object->GetStates().Contains(AtspiState::kFocusable));

  EXPECT_TRUE(object->DoAction(1));
  EXPECT_FALSE(object->DoAction(2));
  EXPECT_TRUE(object->GrabFocus());
  ASSERT_EQ(host.actions.size(), 2u);
  EXPECT_EQ(host.actions[0].node_id, 1u);
  EXPECT_EQ(host.actions[0].action, kFlutterSemanticsActionLongPress);
  EXPECT_EQ(host.actions[1].action, kFlutterSemanticsActionFocus);
}

TEST_F(AccessibleViewTest, CustomActions) {
  TestNode root(0, {1});
  TestNode node(1);
  node.node.actions = kFlutterSemanticsActionCustomAction;
  int32_t custom_ids[] = {7};
  node.node.custom_accessibility_actions = custom_ids;
  node.node.custom_accessibility_actions_count = 1;
  FlutterSemanticsCustomAction2 custom = {};
  custom.struct_size = sizeof(custom);
  custom.id = 7;
  custom.label = "Archive";
  FlutterSemanticsCustomAction2* custom_actions[] = {&custom};

  std::vector<FlutterSemanticsNode2*> nodes = {root.Get(), node.Get()};
  FlutterSemanticsUpdate2 update = MakeUpdate(nodes);
  update.custom_action_count = 1;
  update.custom_actions = custom_actions;
  view.Update(update);

  std::vector<AtspiAction> actions = view.FindNode(1)->GetActions();
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].name, "Archive");
  EXPECT_TRUE(view.FindNode(1)->DoAction(0));
  ASSERT_EQ(host.actions.size(), 1u);
  EXPECT_EQ(host.actions[0].action, kFlutterSemanticsActionCustomAction);
  std::unique_ptr<EncodableValue> data =
      StandardMessageCodec::GetInstance().DecodeMessage(host.actions[0].data);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(*data, EncodableValue(7));
}

TEST_F(AccessibleViewTest, Value) {
  TestNode root(0, {1});
  TestNode slider(1);
  slider.flags.is_slider = true;
  slider.SetValue("40%");
  slider.node.actions = static_cast<FlutterSemanticsAction>(
      kFlutterSemanticsActionIncrease | kFlutterSemanticsActionDecrease);
  Apply(view, {&root, &slider});

  AtspiAccessible* object = view.FindNode(1);
  std::optional<AtspiValue> value = object->GetValue();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->current, 40);
  EXPECT_EQ(value->maximum, 100);
  EXPECT_TRUE(object->SetValue(50));
  EXPECT_TRUE(object->SetValue(10));
  ASSERT_EQ(host.actions.size(), 2u);
  EXPECT_EQ(host.actions[0].action, kFlutterSemanticsActionIncrease);
  EXPECT_EQ(host.actions[1].action, kFlutterSemanticsActionDecrease);
}

TEST_F(AccessibleViewTest, SetValueSteps) {
  TestNode root(0, {1});
  TestNode slider(1);
  slider.flags.is_slider = true;
  slider.SetValue("40%");
  slider.node.actions = static_cast<FlutterSemanticsAction>(
      kFlutterSemanticsActionIncrease | kFlutterSemanticsActionDecrease);
  Apply(view, {&root, &slider});

  AtspiAccessible* object = view.FindNode(1);
  EXPECT_TRUE(object->SetValue(65));
  ASSERT_EQ(host.actions.size(), 1u);

  // Steps until the target is reached or passed.
  slider.SetValue("50%");
  Apply(view, {&slider});
  ASSERT_EQ(host.actions.size(), 2u);
  EXPECT_EQ(host.actions[1].action, kFlutterSemanticsActionIncrease);
  slider.SetValue("60%");
  Apply(view, {&slider});
  ASSERT_EQ(host.actions.size(), 3u);
  slider.SetValue("70%");
  Apply(view, {&slider});
  slider.SetValue("80%");
  Apply(view, {&slider});
  EXPECT_EQ(host.actions.size(), 3u);

  // Stops when the value doesn't change, e.g. at a limit.
  EXPECT_TRUE(object->SetValue(0));
  ASSERT_EQ(host.actions.size(), 4u);
  EXPECT_EQ(host.actions[3].action, kFlutterSemanticsActionDecrease);
  Apply(view, {&slider});
  EXPECT_EQ(host.actions.size(), 4u);
  slider.SetValue("70%");
  Apply(view, {&slider});
  EXPECT_EQ(host.actions.size(), 4u);
}

TEST_F(AccessibleViewTest, Routes) {
  TestNode root(0, {1});
  TestNode page(1, {2, 3});
  page.flags.scopes_route = true;
  TestNode title(2);
  title.flags.names_route = true;
  title.SetLabel("Settings");
  TestNode dialog(3, {4});
  dialog.flags.scopes_route = true;
  dialog.flags.names_route = true;
  dialog.SetLabel("Confirm");
  TestNode dialog_title(4);
  dialog_title.flags.names_route = true;
  dialog_title.SetLabel("Inner");
  Apply(view, {&root, &page, &title, &dialog, &dialog_title});

  EXPECT_EQ(view.FindNode(1)->GetRole(), AtspiRole::kPanel);
  EXPECT_EQ(view.FindNode(1)->GetName(), "Settings");
  EXPECT_EQ(view.FindNode(3)->GetRole(), AtspiRole::kDialog);
  EXPECT_EQ(view.FindNode(3)->GetName(), "Confirm");
}

TEST_F(AccessibleViewTest, RadioGroupRelations) {
  TestNode root(0, {1, 2, 3});
  TestNode radio1(1);
  radio1.flags.is_checked = kFlutterCheckStateTrue;
  radio1.flags.is_in_mutually_exclusive_group = true;
  TestNode radio2(2);
  radio2.flags.is_checked = kFlutterCheckStateFalse;
  radio2.flags.is_in_mutually_exclusive_group = true;
  TestNode other(3);
  other.flags.is_checked = kFlutterCheckStateFalse;
  Apply(view, {&root, &radio1, &radio2, &other});

  std::vector<AtspiRelation> relations = view.FindNode(2)->GetRelations();
  ASSERT_EQ(relations.size(), 1u);
  EXPECT_EQ(relations[0].type, AtspiRelationType::kMemberOf);
  ASSERT_EQ(relations[0].targets.size(), 2u);
  EXPECT_EQ(relations[0].targets[0], view.FindNode(1));
  EXPECT_EQ(relations[0].targets[1], view.FindNode(2));
  EXPECT_TRUE(view.FindNode(3)->GetRelations().empty());
}

TEST_F(AccessibleViewTest, Selection) {
  TestNode root(0, {1});
  TestNode tabs(1, {2, 3});
  TestNode tab1(2);
  tab1.flags.is_selected = kFlutterTristateTrue;
  tab1.node.actions = kFlutterSemanticsActionTap;
  TestNode tab2(3);
  tab2.flags.is_selected = kFlutterTristateFalse;
  tab2.node.actions = kFlutterSemanticsActionTap;
  Apply(view, {&root, &tabs, &tab1, &tab2});

  AtspiAccessible* container = view.FindNode(1);
  EXPECT_FALSE(view.FindNode(0)->IsSelectionContainer());
  EXPECT_TRUE(container->IsSelectionContainer());
  EXPECT_EQ(container->GetSelectedChildren(), std::vector<size_t>{0});
  EXPECT_TRUE(container->SelectChild(0));
  EXPECT_TRUE(host.actions.empty());
  EXPECT_TRUE(container->SelectChild(1));
  ASSERT_EQ(host.actions.size(), 1u);
  EXPECT_EQ(host.actions[0].node_id, 3u);
  EXPECT_EQ(host.actions[0].action, kFlutterSemanticsActionTap);
  EXPECT_FALSE(container->SelectChild(2));

  sink.events.clear();
  tab1.flags.is_selected = kFlutterTristateFalse;
  tab2.flags.is_selected = kFlutterTristateTrue;
  Apply(view, {&tab1, &tab2});
  EXPECT_EQ(container->GetSelectedChildren(), std::vector<size_t>{1});
  std::vector<std::string> events = sink.Describe();
  EXPECT_EQ(std::count(events.begin(), events.end(), "/w/1 SelectionChanged 0"),
            1);
}

TEST_F(AccessibleViewTest, ImageAndPlaceholder) {
  TestNode root(0, {1, 2});
  TestNode image(1);
  image.flags.is_image = true;
  image.SetLabel("Logo");
  TestNode field(2);
  field.flags.is_text_field = true;
  field.node.hint = "Name";
  Apply(view, {&root, &image, &field});

  EXPECT_TRUE(view.FindNode(1)->IsImage());
  EXPECT_FALSE(view.FindNode(2)->IsImage());
  EXPECT_EQ(view.FindNode(2)->GetAttributes()["placeholder-text"], "Name");
}

TEST_F(AccessibleViewTest, WindowState) {
  host.title = "New title";
  host.active = true;
  view.OnWindowStateChanged();
  EXPECT_EQ(sink.Describe(), (std::vector<std::string>{
                                 "/w PropertyChange:accessible-name 0",
                                 "/w Activate 0", "/w StateChanged:active 1"}));
  EXPECT_EQ(sink.events[1].category, "Window");
}

TEST_F(AccessibleViewTest, Announce) {
  view.Announce("Saved", true);
  ASSERT_EQ(sink.events.size(), 1u);
  EXPECT_EQ(sink.events[0].member, "Announcement");
  EXPECT_EQ(sink.events[0].detail1, 2);
  EXPECT_EQ(std::get<std::string>(sink.events[0].data), "Saved");
}

TEST_F(AccessibleViewTest, InactiveSinkGetsNoEvents) {
  sink.active = false;
  TestNode root(0);
  Apply(view, {&root});
  view.Announce("Hello", false);
  EXPECT_TRUE(sink.events.empty());
  EXPECT_EQ(view.GetChildCount(), 1u);
}

TEST_F(AccessibleViewTest, Clear) {
  TestNode root(0, {1});
  TestNode child(1);
  Apply(view, {&root, &child});
  sink.events.clear();
  view.Clear();
  EXPECT_EQ(sink.Describe(),
            std::vector<std::string>{"/w ChildrenChanged:remove 0"});
  EXPECT_EQ(view.GetChildCount(), 0u);
  EXPECT_EQ(view.FindNode(1), nullptr);
}

}  // namespace
}  // namespace flutter
