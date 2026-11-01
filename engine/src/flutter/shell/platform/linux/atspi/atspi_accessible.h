// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_ATSPI_ATSPI_ACCESSIBLE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_ATSPI_ATSPI_ACCESSIBLE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace flutter {

// AT-SPI roles (AtspiRole), as sent over D-Bus.
enum class AtspiRole : uint32_t {
  kInvalid = 0,
  kCheckBox = 7,
  kDialog = 16,
  kFrame = 23,
  kImage = 27,
  kLabel = 29,
  kList = 31,
  kListItem = 32,
  kMenu = 33,
  kMenuItem = 35,
  kPageTab = 37,
  kPageTabList = 38,
  kPanel = 39,
  kPasswordText = 40,
  kProgressBar = 42,
  kButton = 43,
  kRadioButton = 44,
  kScrollPane = 49,
  kSlider = 51,
  kToggleButton = 62,
  kToolTip = 64,
  kWindow = 69,
  kApplication = 75,
  kEntry = 79,
  kHeading = 83,
  kSection = 85,
  kLink = 88,
  kGrouping = 99,
  kStatic = 116,
  kSwitch = 130,
};

// The non-localized name of |role|, e.g. "toggle button".
const char* AtspiRoleName(AtspiRole role);

// AT-SPI states (AtspiStateType).
enum class AtspiState : uint32_t {
  kActive = 1,
  kChecked = 4,
  kCollapsed = 5,
  kDefunct = 6,
  kEditable = 7,
  kEnabled = 8,
  kExpandable = 9,
  kExpanded = 10,
  kFocusable = 11,
  kFocused = 12,
  kHasTooltip = 13,
  kModal = 16,
  kMultiLine = 17,
  kPressed = 20,
  kResizable = 21,
  kSelectable = 22,
  kSelected = 23,
  kSensitive = 24,
  kShowing = 25,
  kSingleLine = 26,
  kVisible = 30,
  kIndeterminate = 32,
  kRequired = 33,
  kSelectableText = 38,
  kCheckable = 41,
  kReadOnly = 43,
};

// The name of |state| used in StateChanged events, e.g. "read-only".
const char* AtspiStateName(AtspiState state);

// A set of AtspiStates, with bit N set for state N.
class AtspiStateSet {
 public:
  AtspiStateSet() = default;
  explicit AtspiStateSet(uint64_t bits) : bits_(bits) {}

  void Add(AtspiState state) { bits_ |= Bit(state); }
  void Remove(AtspiState state) { bits_ &= ~Bit(state); }
  void Set(AtspiState state, bool value) { value ? Add(state) : Remove(state); }
  bool Contains(AtspiState state) const { return (bits_ & Bit(state)) != 0; }
  uint64_t bits() const { return bits_; }

  bool operator==(const AtspiStateSet& other) const {
    return bits_ == other.bits_;
  }

 private:
  static uint64_t Bit(AtspiState state) {
    return uint64_t{1} << static_cast<uint32_t>(state);
  }

  uint64_t bits_ = 0;
};

// A rectangle in logical pixels relative to the window content.
struct AtspiRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;

  bool Contains(int32_t px, int32_t py) const {
    return px >= x && py >= y && px < x + width && py < y + height;
  }
  bool operator==(const AtspiRect& other) const = default;
};

// AT-SPI component layers.
enum class AtspiLayer : uint32_t {
  kWidget = 3,
  kWindow = 7,
};

struct AtspiAction {
  std::string name;
  std::string description;
};

struct AtspiValue {
  double minimum = 0;
  double maximum = 0;
  double increment = 0;
  double current = 0;
  std::string text;

  bool operator==(const AtspiValue& other) const = default;
};

// AT-SPI relation types.
enum class AtspiRelationType : uint32_t {
  kMemberOf = 5,
  kSubwindowOf = 12,
  kPopupFor = 15,
};

class AtspiAccessible;

struct AtspiRelation {
  AtspiRelationType type;
  std::vector<AtspiAccessible*> targets;
};

// An object exposed to assistive technologies over AT-SPI.
//
// Text offsets are in Unicode code points.
class AtspiAccessible {
 public:
  virtual ~AtspiAccessible() = default;

  // The D-Bus object path of this object.
  virtual const std::string& GetPath() const = 0;

  // The parent, or nullptr for the application root.
  virtual AtspiAccessible* GetParent() = 0;
  virtual size_t GetChildCount() = 0;
  virtual AtspiAccessible* GetChild(size_t index) = 0;

  // The index of this object in its parent's children, or -1.
  int32_t GetIndexInParent();

  virtual AtspiRole GetRole() = 0;
  virtual std::string GetName() = 0;
  virtual std::string GetDescription() { return ""; }
  virtual std::string GetHelpText() { return ""; }
  virtual std::string GetAccessibleId() { return ""; }
  virtual AtspiStateSet GetStates() = 0;
  virtual std::map<std::string, std::string> GetAttributes() { return {}; }
  virtual std::vector<AtspiRelation> GetRelations() { return {}; }

  // The extents of a visible component, or nullopt if this isn't one.
  virtual std::optional<AtspiRect> GetExtents() { return std::nullopt; }
  virtual AtspiLayer GetLayer() { return AtspiLayer::kWidget; }
  virtual bool GrabFocus() { return false; }
  virtual bool ScrollTo() { return false; }

  // The deepest visible descendant at a point, this object if no descendant
  // contains it, or nullptr if this object doesn't contain it.
  AtspiAccessible* GetAccessibleAtPoint(int32_t x, int32_t y);

  virtual std::vector<AtspiAction> GetActions() { return {}; }
  virtual bool DoAction(size_t index) { return false; }

  // The value of a range, or nullopt if this isn't one.
  virtual std::optional<AtspiValue> GetValue() { return std::nullopt; }
  virtual bool SetValue(double value) { return false; }

  // Whether this object has text, and whether it can be edited.
  virtual bool HasText() { return false; }
  virtual bool IsEditableText() { return false; }
  // The text, as UTF-16.
  virtual std::u16string GetText() { return u""; }
  // The caret offset, or -1 if there is no caret.
  virtual int32_t GetCaretOffset() { return -1; }
  // The selected range, if not empty.
  virtual bool GetSelection(int32_t* start, int32_t* end) { return false; }
  virtual bool SetSelection(int32_t start, int32_t end) { return false; }
  virtual bool SetTextContents(const std::u16string& text) { return false; }
  enum class ClipboardAction { kCopy, kCut, kPaste };
  // Performs |action| on the current selection.
  virtual bool DoClipboardAction(ClipboardAction action) { return false; }

  // Whether this object contains selectable children.
  virtual bool IsSelectionContainer() { return false; }
  // The indexes of the selected children, in order.
  virtual std::vector<size_t> GetSelectedChildren() { return {}; }
  virtual bool SelectChild(size_t index) { return false; }

  // Whether this object is an image, described by its name.
  virtual bool IsImage() { return false; }
};

// An event about an object.
struct AtspiEvent {
  AtspiAccessible* source = nullptr;
  // "Object" or "Window".
  std::string category = "Object";
  // The D-Bus signal member, e.g. "StateChanged".
  std::string member;
  std::string detail;
  int32_t detail1 = 0;
  int32_t detail2 = 0;
  std::variant<int32_t, std::string, AtspiAccessible*> data = 0;
};

// Receives events about accessible objects.
class AtspiEventSink {
 public:
  virtual ~AtspiEventSink() = default;

  // True if events should be generated.
  virtual bool IsActive() = 0;

  virtual void Emit(const AtspiEvent& event) = 0;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_ATSPI_ATSPI_ACCESSIBLE_H_
