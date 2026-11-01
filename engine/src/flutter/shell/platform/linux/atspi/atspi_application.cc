// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/atspi/atspi_application.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/atspi/atspi_text.h"

namespace flutter {

namespace {

constexpr char kStatusName[] = "org.a11y.Bus";
constexpr char kStatusPath[] = "/org/a11y/bus";
constexpr char kStatusInterface[] = "org.a11y.Status";
constexpr char kBusInterface[] = "org.a11y.Bus";
constexpr char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";

constexpr char kRegistryName[] = "org.a11y.atspi.Registry";
constexpr char kRegistryPath[] = "/org/a11y/atspi/registry";
constexpr char kRegistryInterface[] = "org.a11y.atspi.Registry";
constexpr char kSocketInterface[] = "org.a11y.atspi.Socket";

constexpr char kRootPath[] = "/org/a11y/atspi/accessible/root";
constexpr char kNullPath[] = "/org/a11y/atspi/null";
constexpr char kCachePath[] = "/org/a11y/atspi/cache";
constexpr char kWindowPathPrefix[] = "/org/flutter/a11y/";

constexpr char kAccessibleInterface[] = "org.a11y.atspi.Accessible";
constexpr char kApplicationInterface[] = "org.a11y.atspi.Application";
constexpr char kComponentInterface[] = "org.a11y.atspi.Component";
constexpr char kActionInterface[] = "org.a11y.atspi.Action";
constexpr char kValueInterface[] = "org.a11y.atspi.Value";
constexpr char kTextInterface[] = "org.a11y.atspi.Text";
constexpr char kEditableTextInterface[] = "org.a11y.atspi.EditableText";
constexpr char kSelectionInterface[] = "org.a11y.atspi.Selection";
constexpr char kImageInterface[] = "org.a11y.atspi.Image";
constexpr char kCacheInterface[] = "org.a11y.atspi.Cache";
constexpr char kEventInterfacePrefix[] = "org.a11y.atspi.Event.";

constexpr char kInvalidArgs[] = "org.freedesktop.DBus.Error.InvalidArgs";
constexpr char kUnknownObject[] = "org.freedesktop.DBus.Error.UnknownObject";
constexpr char kUnknownProperty[] =
    "org.freedesktop.DBus.Error.UnknownProperty";

// Converts an event name part to the format listeners register with, e.g.
// "state-changed" to "StateChanged".
std::string ToEventFormat(std::string_view name) {
  std::string result;
  bool upper = true;
  for (char c : name) {
    if (c == '-') {
      upper = true;
    } else if (upper) {
      result.push_back(static_cast<char>(toupper(c)));
      upper = false;
    } else {
      result.push_back(c);
    }
  }
  return result;
}

// Splits a registered event into at most three ':'-separated parts.
std::vector<std::string> SplitEvent(std::string_view event) {
  std::vector<std::string> parts;
  while (parts.size() < 2) {
    size_t end = event.find(':');
    if (end == std::string_view::npos) {
      break;
    }
    parts.emplace_back(event.substr(0, end));
    event.remove_prefix(end + 1);
  }
  parts.emplace_back(event);
  return parts;
}

}  // namespace

// The application object, whose children are the windows.
class AtspiApplication::Root : public AtspiAccessible {
 public:
  explicit Root(AtspiApplication* application) : application_(application) {}

  const std::string& GetPath() const override { return path_; }
  AtspiAccessible* GetParent() override { return nullptr; }
  size_t GetChildCount() override { return application_->windows_.size(); }
  AtspiAccessible* GetChild(size_t index) override {
    return index < application_->windows_.size() ? application_->windows_[index]
                                                 : nullptr;
  }
  AtspiRole GetRole() override { return AtspiRole::kApplication; }
  std::string GetName() override {
    if (!application_->name_.empty()) {
      return application_->name_;
    }
    return program_invocation_short_name;
  }
  AtspiStateSet GetStates() override { return {}; }

 private:
  AtspiApplication* application_;
  std::string path_ = kRootPath;
};

AtspiApplication::AtspiApplication(EventLoop* loop)
    : loop_(loop), root_(std::make_unique<Root>(this)) {}

AtspiApplication::~AtspiApplication() {
  alive_.reset();
  accessibility_bus_.reset();
  session_.reset();
}

AtspiAccessible* AtspiApplication::root() const {
  return root_.get();
}

void AtspiApplication::Start() {
  session_ =
      DBusConnection::Connect(loop_, DBusConnection::SessionBusAddress());
  if (!session_) {
    // Without a session bus, the accessibility bus can only be found from
    // the environment.
    const char* address = getenv("AT_SPI_BUS_ADDRESS");
    if (address != nullptr && address[0] != '\0') {
      ConnectAccessibilityBus(address);
    }
    return;
  }
  session_->set_ready_callback([this]() { OnSessionReady(); });
  session_->set_signal_handler(
      [this](const DBusMessage& signal) { OnSessionSignal(signal); });
  session_->set_disconnected_callback([this]() {
    status_enabled_ = screen_reader_enabled_ = false;
    UpdateStatus();
  });
}

void AtspiApplication::OnSessionReady() {
  session_->AddMatch(std::string("type='signal',sender='") + kStatusName +
                     "',path='" + kStatusPath + "',interface='" +
                     kPropertiesInterface + "',member='PropertiesChanged'");

  DBusMessage call = DBusMessage::MethodCall(kStatusName, kStatusPath,
                                             kPropertiesInterface, "GetAll");
  call.writer().WriteString(kStatusInterface);
  std::weak_ptr<bool> alive = alive_;
  session_->Call(call, [this, alive](const DBusMessage* reply) {
    if (alive.expired() || reply == nullptr ||
        reply->type() != DBusMessage::Type::kMethodReturn) {
      return;
    }
    DBusReader reader = reply->reader();
    ReadStatus(reader);
    UpdateStatus();
  });
}

void AtspiApplication::OnSessionSignal(const DBusMessage& signal) {
  if (signal.path() != kStatusPath ||
      signal.interface() != kPropertiesInterface ||
      signal.member() != "PropertiesChanged") {
    return;
  }
  DBusReader reader = signal.reader();
  std::string interface;
  if (!reader.ReadString(&interface) || interface != kStatusInterface) {
    return;
  }
  ReadStatus(reader);
  UpdateStatus();
}

void AtspiApplication::ReadStatus(DBusReader& reader) {
  if (!reader.OpenArray()) {
    return;
  }
  while (reader.HasMore()) {
    std::string name;
    std::string signature;
    if (!reader.OpenDictEntry() || !reader.ReadString(&name) ||
        !reader.OpenVariant(&signature)) {
      return;
    }
    bool value;
    if (signature == "b" && reader.ReadBoolean(&value)) {
      if (name == "IsEnabled") {
        status_enabled_ = value;
      } else if (name == "ScreenReaderEnabled") {
        screen_reader_enabled_ = value;
      }
    }
    if (!reader.CloseVariant() || !reader.CloseDictEntry()) {
      return;
    }
  }
  reader.CloseArray();
}

void AtspiApplication::UpdateStatus() {
  bool enabled = status_enabled_ || screen_reader_enabled_;
  if (!enabled) {
    DisconnectAccessibilityBus();
    return;
  }
  if (accessibility_bus_) {
    return;
  }

  const char* address = getenv("AT_SPI_BUS_ADDRESS");
  if (address != nullptr && address[0] != '\0') {
    ConnectAccessibilityBus(address);
    return;
  }
  if (!session_ || !session_->is_ready()) {
    return;
  }
  std::weak_ptr<bool> alive = alive_;
  session_->Call(DBusMessage::MethodCall(kStatusName, kStatusPath,
                                         kBusInterface, "GetAddress"),
                 [this, alive](const DBusMessage* reply) {
                   std::string address;
                   if (alive.expired() || reply == nullptr ||
                       reply->type() != DBusMessage::Type::kMethodReturn ||
                       !reply->reader().ReadString(&address)) {
                     return;
                   }
                   // The status may have changed while waiting.
                   if ((status_enabled_ || screen_reader_enabled_) &&
                       !accessibility_bus_) {
                     ConnectAccessibilityBus(address);
                   }
                 });
}

void AtspiApplication::ConnectAccessibilityBus(const std::string& address) {
  accessibility_bus_ = DBusConnection::Connect(loop_, address);
  if (!accessibility_bus_) {
    FML_LOG(ERROR) << "Failed to connect to the accessibility bus";
    return;
  }
  accessibility_bus_->set_ready_callback([this]() { Register(); });
  accessibility_bus_->set_method_handler(
      [this](const DBusMessage& call) { return HandleMethodCall(call); });
  accessibility_bus_->set_signal_handler(
      [this](const DBusMessage& signal) { OnAccessibilitySignal(signal); });
  accessibility_bus_->set_disconnected_callback([this]() {
    // Destroy the connection outside its callback.
    std::weak_ptr<bool> alive = alive_;
    loop_->PostTask([this, alive]() {
      if (!alive.expired()) {
        DisconnectAccessibilityBus();
      }
    });
  });
}

void AtspiApplication::DisconnectAccessibilityBus() {
  accessibility_bus_.reset();
  events_known_ = false;
  registered_events_.clear();
  SetRegistered(false);
}

void AtspiApplication::Register() {
  accessibility_bus_->AddMatch(std::string("type='signal',interface='") +
                               kRegistryInterface + "',sender='" +
                               kRegistryName + "'");
  accessibility_bus_->AddMatch(
      std::string("type='signal',interface='org.freedesktop.DBus',"
                  "member='NameOwnerChanged',arg0='") +
      kRegistryName + "'");

  DBusMessage call = DBusMessage::MethodCall(kRegistryName, kRootPath,
                                             kSocketInterface, "Embed");
  call.writer().OpenStruct();
  call.writer().WriteString(accessibility_bus_->unique_name());
  call.writer().WriteObjectPath(kRootPath);
  call.writer().CloseStruct();
  std::weak_ptr<bool> alive = alive_;
  accessibility_bus_->Call(call, [this, alive](const DBusMessage* reply) {
    if (alive.expired()) {
      return;
    }
    if (reply == nullptr || reply->type() != DBusMessage::Type::kMethodReturn) {
      FML_LOG(ERROR) << "Failed to register with the AT-SPI registry";
      return;
    }
    DBusReader reader = reply->reader();
    if (reader.OpenStruct()) {
      reader.ReadString(&desktop_name_);
      reader.ReadObjectPath(&desktop_path_);
    }
    SetRegistered(true);
  });

  RequestRegisteredEvents();
}

void AtspiApplication::RequestRegisteredEvents() {
  std::weak_ptr<bool> alive = alive_;
  accessibility_bus_->Call(
      DBusMessage::MethodCall(kRegistryName, kRegistryPath, kRegistryInterface,
                              "GetRegisteredEvents"),
      [this, alive](const DBusMessage* reply) {
        if (alive.expired() || reply == nullptr ||
            reply->type() != DBusMessage::Type::kMethodReturn ||
            reply->signature() != "a(ss)") {
          return;
        }
        registered_events_.clear();
        DBusReader reader = reply->reader();
        reader.OpenArray();
        while (reader.HasMore()) {
          std::string bus;
          std::string event;
          reader.OpenStruct();
          reader.ReadString(&bus);
          reader.ReadString(&event);
          reader.CloseStruct();
          registered_events_.emplace_back(bus, event);
        }
        events_known_ = reader.CloseArray();
      });
}

void AtspiApplication::OnAccessibilitySignal(const DBusMessage& signal) {
  DBusReader reader = signal.reader();
  if (signal.interface() == "org.freedesktop.DBus" &&
      signal.member() == "NameOwnerChanged") {
    std::string name, old_owner, new_owner;
    if (reader.ReadString(&name) && reader.ReadString(&old_owner) &&
        reader.ReadString(&new_owner) && name == kRegistryName &&
        !new_owner.empty()) {
      // The registry restarted.
      Register();
    }
    return;
  }
  if (signal.interface() != kRegistryInterface) {
    return;
  }
  std::string bus, event;
  if (!reader.ReadString(&bus) || !reader.ReadString(&event)) {
    return;
  }
  if (signal.member() == "EventListenerRegistered") {
    registered_events_.emplace_back(bus, event);
  } else if (signal.member() == "EventListenerDeregistered") {
    auto it = std::find(registered_events_.begin(), registered_events_.end(),
                        std::make_pair(bus, event));
    if (it != registered_events_.end()) {
      registered_events_.erase(it);
    }
  }
}

void AtspiApplication::SetRegistered(bool registered) {
  if (registered == registered_) {
    return;
  }
  registered_ = registered;
  std::vector<Observer*> observers = observers_;
  for (Observer* observer : observers) {
    observer->OnAccessibilityEnabledChanged(registered);
  }
}

void AtspiApplication::AddObserver(Observer* observer) {
  observers_.push_back(observer);
}

void AtspiApplication::RemoveObserver(Observer* observer) {
  observers_.erase(std::remove(observers_.begin(), observers_.end(), observer),
                   observers_.end());
}

std::string AtspiApplication::NewWindowPath() {
  return kWindowPathPrefix + std::to_string(next_window_serial_++);
}

void AtspiApplication::AddWindow(AccessibleView* window) {
  windows_.push_back(window);
  if (IsActive()) {
    Emit({.source = root_.get(),
          .member = "ChildrenChanged",
          .detail = "add",
          .detail1 = static_cast<int32_t>(windows_.size() - 1),
          .data = window});
    Emit({.source = window, .category = "Window", .member = "Create"});
  }
}

void AtspiApplication::RemoveWindow(AccessibleView* window) {
  auto it = std::find(windows_.begin(), windows_.end(), window);
  if (it == windows_.end()) {
    return;
  }
  if (IsActive()) {
    Emit({.source = window, .category = "Window", .member = "Destroy"});
    Emit({.source = root_.get(),
          .member = "ChildrenChanged",
          .detail = "remove",
          .detail1 = static_cast<int32_t>(it - windows_.begin()),
          .data = window});
  }
  windows_.erase(it);
}

bool AtspiApplication::IsActive() {
  return accessibility_bus_ && registered_;
}

bool AtspiApplication::IsEventNeeded(const AtspiEvent& event) {
  if (!events_known_) {
    return true;
  }
  // Events that keep clients' caches up to date are always needed.
  if (event.member == "ChildrenChanged" || event.member == "StateChanged" ||
      (event.member == "PropertyChange" &&
       (event.detail == "accessible-name" ||
        event.detail == "accessible-description" ||
        event.detail == "accessible-parent" ||
        event.detail == "accessible-role"))) {
    return true;
  }
  std::string needle[3] = {event.category, event.member,
                           ToEventFormat(event.detail)};
  for (const auto& [bus, registered] : registered_events_) {
    std::vector<std::string> parts = SplitEvent(registered);
    bool matches = true;
    for (size_t i = 0; i < parts.size() && !parts[i].empty(); i++) {
      if (parts[i] != needle[i]) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return true;
    }
  }
  return false;
}

void AtspiApplication::Emit(const AtspiEvent& event) {
  if (!IsActive() || event.source == nullptr || !IsEventNeeded(event)) {
    return;
  }
  DBusMessage signal =
      DBusMessage::Signal(event.source->GetPath(),
                          kEventInterfacePrefix + event.category, event.member);
  DBusWriter& writer = signal.writer();
  writer.WriteString(event.detail);
  writer.WriteInt32(event.detail1);
  writer.WriteInt32(event.detail2);
  if (const int32_t* value = std::get_if<int32_t>(&event.data)) {
    writer.OpenVariant("i");
    writer.WriteInt32(*value);
  } else if (const std::string* text = std::get_if<std::string>(&event.data)) {
    writer.OpenVariant("s");
    writer.WriteString(*text);
  } else {
    writer.OpenVariant("(so)");
    WriteReference(writer, std::get<AtspiAccessible*>(event.data));
  }
  writer.CloseVariant();
  writer.OpenArray("{sv}");
  writer.CloseArray();
  accessibility_bus_->Send(signal);
}

void AtspiApplication::WriteReference(DBusWriter& writer,
                                      AtspiAccessible* object) {
  writer.OpenStruct();
  writer.WriteString(accessibility_bus_ ? accessibility_bus_->unique_name()
                                        : "");
  writer.WriteObjectPath(object != nullptr ? object->GetPath() : kNullPath);
  writer.CloseStruct();
}

AtspiAccessible* AtspiApplication::FindObject(const std::string& path) {
  if (path == kRootPath) {
    return root_.get();
  }
  if (path.compare(0, sizeof(kWindowPathPrefix) - 1, kWindowPathPrefix) != 0) {
    return nullptr;
  }
  for (AccessibleView* window : windows_) {
    AtspiAccessible* object = window->FindByPath(path);
    if (object != nullptr) {
      return object;
    }
  }
  return nullptr;
}

bool AtspiApplication::HandleMethodCall(const DBusMessage& call) {
  DBusMessage reply = DBusMessage::MethodReturn(call);

  if (call.path() == kCachePath) {
    if (call.interface() != kCacheInterface || call.member() != "GetItems") {
      return false;
    }
    // Clients fall back to querying objects when the cache is empty.
    reply.writer().OpenArray("((so)(so)(so)iiassusau)");
    reply.writer().CloseArray();
    accessibility_bus_->Send(reply);
    return true;
  }

  AtspiAccessible* object = FindObject(call.path());
  if (object == nullptr) {
    if (call.expects_reply()) {
      accessibility_bus_->Send(DBusMessage::Error(
          call, kUnknownObject, "Unknown object " + call.path()));
    }
    return true;
  }

  bool handled = false;
  const std::string& interface = call.interface();
  std::vector<std::string> interfaces = GetInterfaces(object);
  auto implements = [&interfaces](const char* name) {
    return std::find(interfaces.begin(), interfaces.end(), name) !=
           interfaces.end();
  };
  if (interface == kPropertiesInterface) {
    handled = HandleProperties(object, call, &reply);
  } else if (interface == kAccessibleInterface) {
    handled = HandleAccessible(object, call, &reply);
  } else if (interface == kApplicationInterface && object == root_.get()) {
    handled = HandleApplication(call, &reply);
  } else if (interface == kComponentInterface &&
             implements(kComponentInterface)) {
    handled = HandleComponent(object, call, &reply);
  } else if (interface == kActionInterface && implements(kActionInterface)) {
    handled = HandleAction(object, call, &reply);
  } else if (interface == kTextInterface && implements(kTextInterface)) {
    handled = HandleText(object, call, &reply);
  } else if (interface == kEditableTextInterface &&
             implements(kEditableTextInterface)) {
    handled = HandleEditableText(object, call, &reply);
  } else if (interface == kSelectionInterface &&
             implements(kSelectionInterface)) {
    handled = HandleSelection(object, call, &reply);
  } else if (interface == kImageInterface && implements(kImageInterface)) {
    handled = HandleImage(object, call, &reply);
  }
  if (!handled) {
    return false;
  }
  if (call.expects_reply() && accessibility_bus_) {
    accessibility_bus_->Send(reply);
  }
  return true;
}

std::vector<std::string> AtspiApplication::GetInterfaces(
    AtspiAccessible* object) {
  std::vector<std::string> interfaces = {kAccessibleInterface};
  if (object == root_.get()) {
    interfaces.push_back(kApplicationInterface);
    return interfaces;
  }
  if (object->GetExtents().has_value()) {
    interfaces.push_back(kComponentInterface);
  }
  if (!object->GetActions().empty()) {
    interfaces.push_back(kActionInterface);
  }
  if (object->GetValue().has_value()) {
    interfaces.push_back(kValueInterface);
  }
  if (object->HasText()) {
    interfaces.push_back(kTextInterface);
  }
  if (object->IsEditableText()) {
    interfaces.push_back(kEditableTextInterface);
  }
  if (object->IsSelectionContainer()) {
    interfaces.push_back(kSelectionInterface);
  }
  if (object->IsImage()) {
    interfaces.push_back(kImageInterface);
  }
  return interfaces;
}

bool AtspiApplication::HandleAccessible(AtspiAccessible* object,
                                        const DBusMessage& call,
                                        DBusMessage* reply) {
  const std::string& member = call.member();
  DBusReader reader = call.reader();
  DBusWriter& writer = reply->writer();
  if (member == "GetChildAtIndex") {
    int32_t index;
    if (!reader.ReadInt32(&index)) {
      *reply = DBusMessage::Error(call, kInvalidArgs, "Expected (i)");
      return true;
    }
    WriteReference(writer, index >= 0 ? object->GetChild(index) : nullptr);
  } else if (member == "GetChildren") {
    writer.OpenArray("(so)");
    size_t count = object->GetChildCount();
    for (size_t i = 0; i < count; i++) {
      WriteReference(writer, object->GetChild(i));
    }
    writer.CloseArray();
  } else if (member == "GetIndexInParent") {
    writer.WriteInt32(object->GetIndexInParent());
  } else if (member == "GetRelationSet") {
    writer.OpenArray("(ua(so))");
    for (const AtspiRelation& relation : object->GetRelations()) {
      writer.OpenStruct();
      writer.WriteUInt32(static_cast<uint32_t>(relation.type));
      writer.OpenArray("(so)");
      for (AtspiAccessible* target : relation.targets) {
        WriteReference(writer, target);
      }
      writer.CloseArray();
      writer.CloseStruct();
    }
    writer.CloseArray();
  } else if (member == "GetRole") {
    writer.WriteUInt32(static_cast<uint32_t>(object->GetRole()));
  } else if (member == "GetRoleName" || member == "GetLocalizedRoleName") {
    writer.WriteString(AtspiRoleName(object->GetRole()));
  } else if (member == "GetState") {
    uint64_t bits = object->GetStates().bits();
    writer.OpenArray("u");
    writer.WriteUInt32(static_cast<uint32_t>(bits));
    writer.WriteUInt32(static_cast<uint32_t>(bits >> 32));
    writer.CloseArray();
  } else if (member == "GetAttributes") {
    writer.OpenArray("{ss}");
    for (const auto& [key, value] : object->GetAttributes()) {
      writer.OpenDictEntry();
      writer.WriteString(key);
      writer.WriteString(value);
      writer.CloseDictEntry();
    }
    writer.CloseArray();
  } else if (member == "GetApplication") {
    WriteReference(writer, root_.get());
  } else if (member == "GetInterfaces") {
    writer.OpenArray("s");
    for (const std::string& interface : GetInterfaces(object)) {
      writer.WriteString(interface);
    }
    writer.CloseArray();
  } else {
    return false;
  }
  return true;
}

bool AtspiApplication::HandleApplication(const DBusMessage& call,
                                         DBusMessage* reply) {
  if (call.member() == "GetLocale") {
    const char* locale = setlocale(LC_MESSAGES, nullptr);
    reply->writer().WriteString(locale != nullptr ? locale : "");
  } else if (call.member() == "GetApplicationBusAddress") {
    // Clients use the accessibility bus rather than a direct connection.
    reply->writer().WriteString("");
  } else {
    return false;
  }
  return true;
}

bool AtspiApplication::HandleComponent(AtspiAccessible* object,
                                       const DBusMessage& call,
                                       DBusMessage* reply) {
  const std::string& member = call.member();
  DBusReader reader = call.reader();
  DBusWriter& writer = reply->writer();
  // Wayland has no global coordinates, so all coordinates are relative to
  // the window.
  AtspiRect extents = object->GetExtents().value_or(AtspiRect());
  if (member == "Contains" || member == "GetAccessibleAtPoint") {
    int32_t x, y;
    if (!reader.ReadInt32(&x) || !reader.ReadInt32(&y)) {
      *reply = DBusMessage::Error(call, kInvalidArgs, "Expected (iiu)");
      return true;
    }
    if (member == "Contains") {
      writer.WriteBoolean(extents.Contains(x, y));
    } else {
      WriteReference(writer, object->GetAccessibleAtPoint(x, y));
    }
  } else if (member == "GetExtents") {
    writer.OpenStruct();
    writer.WriteInt32(extents.x);
    writer.WriteInt32(extents.y);
    writer.WriteInt32(extents.width);
    writer.WriteInt32(extents.height);
    writer.CloseStruct();
  } else if (member == "GetPosition") {
    writer.WriteInt32(extents.x);
    writer.WriteInt32(extents.y);
  } else if (member == "GetSize") {
    writer.WriteInt32(extents.width);
    writer.WriteInt32(extents.height);
  } else if (member == "GetLayer") {
    writer.WriteUInt32(static_cast<uint32_t>(object->GetLayer()));
  } else if (member == "GetMDIZOrder") {
    writer.WriteInt16(0);
  } else if (member == "GrabFocus") {
    writer.WriteBoolean(object->GrabFocus());
  } else if (member == "GetAlpha") {
    writer.WriteDouble(1.0);
  } else if (member == "SetExtents" || member == "SetPosition" ||
             member == "SetSize" || member == "ScrollToPoint") {
    writer.WriteBoolean(false);
  } else if (member == "ScrollTo") {
    writer.WriteBoolean(object->ScrollTo());
  } else {
    return false;
  }
  return true;
}

bool AtspiApplication::HandleAction(AtspiAccessible* object,
                                    const DBusMessage& call,
                                    DBusMessage* reply) {
  const std::string& member = call.member();
  DBusWriter& writer = reply->writer();
  std::vector<AtspiAction> actions = object->GetActions();
  if (member == "GetActions") {
    writer.OpenArray("(sss)");
    for (const AtspiAction& action : actions) {
      writer.OpenStruct();
      writer.WriteString(action.name);
      writer.WriteString(action.description);
      writer.WriteString("");
      writer.CloseStruct();
    }
    writer.CloseArray();
    return true;
  }

  int32_t index;
  if (!call.reader().ReadInt32(&index)) {
    if (member == "GetDescription" || member == "GetName" ||
        member == "GetLocalizedName" || member == "GetKeyBinding" ||
        member == "DoAction") {
      *reply = DBusMessage::Error(call, kInvalidArgs, "Expected (i)");
      return true;
    }
    return false;
  }
  const AtspiAction* action =
      index >= 0 && static_cast<size_t>(index) < actions.size()
          ? &actions[index]
          : nullptr;
  if (member == "GetDescription") {
    writer.WriteString(action != nullptr ? action->description : "");
  } else if (member == "GetName" || member == "GetLocalizedName") {
    writer.WriteString(action != nullptr ? action->name : "");
  } else if (member == "GetKeyBinding") {
    writer.WriteString("");
  } else if (member == "DoAction") {
    writer.WriteBoolean(action != nullptr && object->DoAction(index));
  } else {
    return false;
  }
  return true;
}

bool AtspiApplication::HandleText(AtspiAccessible* object,
                                  const DBusMessage& call,
                                  DBusMessage* reply) {
  const std::string& member = call.member();
  DBusReader reader = call.reader();
  DBusWriter& writer = reply->writer();
  std::u16string text = object->GetText();
  int32_t count = atspi_text::CharacterCount(text);
  int32_t caret = object->GetCaretOffset();
  int32_t selection_start = 0, selection_end = 0;
  bool has_selection = object->GetSelection(&selection_start, &selection_end);
  AtspiRect extents = object->GetExtents().value_or(AtspiRect());

  auto invalid_args = [&]() {
    *reply = DBusMessage::Error(call, kInvalidArgs,
                                "Invalid arguments to " + member);
    return true;
  };
  auto write_range = [&](atspi_text::Range range) {
    writer.WriteString(atspi_text::Substring(text, range.start, range.end));
    writer.WriteInt32(range.start);
    writer.WriteInt32(range.end);
  };
  auto write_extents = [&]() {
    writer.WriteInt32(extents.x);
    writer.WriteInt32(extents.y);
    writer.WriteInt32(extents.width);
    writer.WriteInt32(extents.height);
  };

  if (member == "GetStringAtOffset") {
    int32_t offset;
    uint32_t granularity;
    if (!reader.ReadInt32(&offset) || !reader.ReadUInt32(&granularity)) {
      return invalid_args();
    }
    write_range(atspi_text::GetStringAtOffset(
        text, offset, static_cast<atspi_text::Granularity>(granularity)));
  } else if (member == "GetTextBeforeOffset" || member == "GetTextAtOffset" ||
             member == "GetTextAfterOffset") {
    int32_t offset;
    uint32_t boundary;
    if (!reader.ReadInt32(&offset) || !reader.ReadUInt32(&boundary) ||
        boundary > static_cast<uint32_t>(atspi_text::Boundary::kLineEnd)) {
      return invalid_args();
    }
    atspi_text::Position position =
        member == "GetTextBeforeOffset" ? atspi_text::Position::kBefore
        : member == "GetTextAtOffset"   ? atspi_text::Position::kAt
                                        : atspi_text::Position::kAfter;
    write_range(atspi_text::GetTextAtOffset(
        text, offset, static_cast<atspi_text::Boundary>(boundary), position));
  } else if (member == "GetText") {
    int32_t start, end;
    if (!reader.ReadInt32(&start) || !reader.ReadInt32(&end)) {
      return invalid_args();
    }
    writer.WriteString(atspi_text::Substring(text, start, end));
  } else if (member == "SetCaretOffset") {
    int32_t offset;
    if (!reader.ReadInt32(&offset)) {
      return invalid_args();
    }
    offset = std::clamp(offset, 0, count);
    writer.WriteBoolean(object->SetSelection(offset, offset));
  } else if (member == "GetCharacterAtOffset") {
    int32_t offset;
    if (!reader.ReadInt32(&offset)) {
      return invalid_args();
    }
    int32_t character = 0;
    if (offset >= 0 && offset < count) {
      std::u16string unit =
          atspi_text::FromUtf8(atspi_text::Substring(text, offset, offset + 1));
      character = unit.size() == 2 ? 0x10000 + ((unit[0] - 0xd800) << 10) +
                                         (unit[1] - 0xdc00)
                                   : unit[0];
    }
    writer.WriteInt32(character);
  } else if (member == "GetAttributeValue") {
    writer.WriteString("");
  } else if (member == "GetAttributes" || member == "GetAttributeRun") {
    writer.OpenArray("{ss}");
    writer.CloseArray();
    writer.WriteInt32(0);
    writer.WriteInt32(count);
  } else if (member == "GetDefaultAttributes" ||
             member == "GetDefaultAttributeSet") {
    writer.OpenArray("{ss}");
    writer.CloseArray();
  } else if (member == "GetCharacterExtents" || member == "GetRangeExtents") {
    // Flutter doesn't describe the layout of the text.
    write_extents();
  } else if (member == "GetOffsetAtPoint") {
    writer.WriteInt32(-1);
  } else if (member == "GetNSelections") {
    writer.WriteInt32(has_selection ? 1 : 0);
  } else if (member == "GetSelection") {
    int32_t index;
    if (!reader.ReadInt32(&index)) {
      return invalid_args();
    }
    bool valid = index == 0 && has_selection;
    writer.WriteInt32(valid ? selection_start : 0);
    writer.WriteInt32(valid ? selection_end : 0);
  } else if (member == "AddSelection") {
    int32_t start, end;
    if (!reader.ReadInt32(&start) || !reader.ReadInt32(&end)) {
      return invalid_args();
    }
    // Only one selection is supported.
    writer.WriteBoolean(!has_selection && object->SetSelection(start, end));
  } else if (member == "RemoveSelection") {
    int32_t index;
    if (!reader.ReadInt32(&index)) {
      return invalid_args();
    }
    writer.WriteBoolean(index == 0 && has_selection &&
                        object->SetSelection(caret, caret));
  } else if (member == "SetSelection") {
    int32_t index, start, end;
    if (!reader.ReadInt32(&index) || !reader.ReadInt32(&start) ||
        !reader.ReadInt32(&end)) {
      return invalid_args();
    }
    writer.WriteBoolean(index == 0 && object->SetSelection(start, end));
  } else if (member == "GetBoundedRanges") {
    writer.OpenArray("(iisv)");
    writer.CloseArray();
  } else if (member == "ScrollSubstringTo") {
    writer.WriteBoolean(object->ScrollTo());
  } else if (member == "ScrollSubstringToPoint") {
    writer.WriteBoolean(false);
  } else {
    return false;
  }
  return true;
}

bool AtspiApplication::HandleEditableText(AtspiAccessible* object,
                                          const DBusMessage& call,
                                          DBusMessage* reply) {
  const std::string& member = call.member();
  DBusReader reader = call.reader();
  DBusWriter& writer = reply->writer();
  std::u16string text = object->GetText();
  auto invalid_args = [&]() {
    *reply = DBusMessage::Error(call, kInvalidArgs,
                                "Invalid arguments to " + member);
    return true;
  };
  // Replaces the characters from |start| to |end| with |replacement|.
  auto replace = [&](int32_t start, int32_t end,
                     const std::u16string& replacement) {
    int32_t count = atspi_text::CharacterCount(text);
    if (end < 0 || end > count) {
      end = count;
    }
    start = std::clamp(start, 0, end);
    int32_t utf16_start = atspi_text::ToUtf16Offset(text, start);
    int32_t utf16_end = atspi_text::ToUtf16Offset(text, end);
    std::u16string new_text =
        text.substr(0, utf16_start) + replacement + text.substr(utf16_end);
    return object->SetTextContents(new_text);
  };

  if (member == "SetTextContents") {
    std::string contents;
    if (!reader.ReadString(&contents)) {
      return invalid_args();
    }
    writer.WriteBoolean(
        object->SetTextContents(atspi_text::FromUtf8(contents)));
  } else if (member == "InsertText") {
    int32_t position, length;
    std::string insert;
    if (!reader.ReadInt32(&position) || !reader.ReadString(&insert) ||
        !reader.ReadInt32(&length)) {
      return invalid_args();
    }
    std::u16string inserted = atspi_text::FromUtf8(insert);
    if (length >= 0) {
      inserted =
          atspi_text::FromUtf8(atspi_text::Substring(inserted, 0, length));
    }
    writer.WriteBoolean(replace(position, position, inserted));
  } else if (member == "DeleteText") {
    int32_t start, end;
    if (!reader.ReadInt32(&start) || !reader.ReadInt32(&end)) {
      return invalid_args();
    }
    writer.WriteBoolean(replace(start, end, u""));
  } else if (member == "CopyText" || member == "CutText") {
    int32_t start, end;
    if (!reader.ReadInt32(&start) || !reader.ReadInt32(&end)) {
      return invalid_args();
    }
    bool result =
        object->SetSelection(start, end) &&
        object->DoClipboardAction(member == "CopyText"
                                      ? AtspiAccessible::ClipboardAction::kCopy
                                      : AtspiAccessible::ClipboardAction::kCut);
    if (member == "CutText") {
      writer.WriteBoolean(result);
    }
  } else if (member == "PasteText") {
    int32_t position;
    if (!reader.ReadInt32(&position)) {
      return invalid_args();
    }
    writer.WriteBoolean(
        object->SetSelection(position, position) &&
        object->DoClipboardAction(AtspiAccessible::ClipboardAction::kPaste));
  } else {
    return false;
  }
  return true;
}

bool AtspiApplication::HandleSelection(AtspiAccessible* object,
                                       const DBusMessage& call,
                                       DBusMessage* reply) {
  const std::string& member = call.member();
  DBusWriter& writer = reply->writer();
  std::vector<size_t> selected = object->GetSelectedChildren();
  if (member == "SelectAll" || member == "ClearSelection") {
    // Flutter has no way to change the selection of several items at once.
    writer.WriteBoolean(false);
    return true;
  }

  int32_t index;
  if (!call.reader().ReadInt32(&index)) {
    if (member == "GetSelectedChild" || member == "SelectChild" ||
        member == "DeselectSelectedChild" || member == "IsChildSelected" ||
        member == "DeselectChild") {
      *reply = DBusMessage::Error(call, kInvalidArgs, "Expected (i)");
      return true;
    }
    return false;
  }
  if (member == "GetSelectedChild") {
    AtspiAccessible* child = nullptr;
    if (index >= 0 && static_cast<size_t>(index) < selected.size()) {
      child = object->GetChild(selected[index]);
    }
    WriteReference(writer, child);
  } else if (member == "SelectChild") {
    writer.WriteBoolean(index >= 0 && object->SelectChild(index));
  } else if (member == "IsChildSelected") {
    writer.WriteBoolean(
        index >= 0 && std::find(selected.begin(), selected.end(),
                                static_cast<size_t>(index)) != selected.end());
  } else if (member == "DeselectSelectedChild" || member == "DeselectChild") {
    // Selected items, e.g. tabs, can't be deselected without selecting
    // another.
    writer.WriteBoolean(false);
  } else {
    return false;
  }
  return true;
}

bool AtspiApplication::HandleImage(AtspiAccessible* object,
                                   const DBusMessage& call,
                                   DBusMessage* reply) {
  const std::string& member = call.member();
  DBusWriter& writer = reply->writer();
  AtspiRect extents = object->GetExtents().value_or(AtspiRect());
  if (member == "GetImageExtents") {
    writer.OpenStruct();
    writer.WriteInt32(extents.x);
    writer.WriteInt32(extents.y);
    writer.WriteInt32(extents.width);
    writer.WriteInt32(extents.height);
    writer.CloseStruct();
  } else if (member == "GetImagePosition") {
    writer.WriteInt32(extents.x);
    writer.WriteInt32(extents.y);
  } else if (member == "GetImageSize") {
    writer.WriteInt32(extents.width);
    writer.WriteInt32(extents.height);
  } else {
    return false;
  }
  return true;
}

bool AtspiApplication::WriteProperty(AtspiAccessible* object,
                                     const std::string& interface,
                                     const std::string& name,
                                     DBusWriter& writer) {
  auto write_string = [&writer](const std::string& value) {
    writer.OpenVariant("s");
    writer.WriteString(value);
    writer.CloseVariant();
  };
  auto write_int = [&writer](int32_t value) {
    writer.OpenVariant("i");
    writer.WriteInt32(value);
    writer.CloseVariant();
  };
  auto write_double = [&writer](double value) {
    writer.OpenVariant("d");
    writer.WriteDouble(value);
    writer.CloseVariant();
  };

  if (interface == kAccessibleInterface) {
    if (name == "Name") {
      write_string(object->GetName());
    } else if (name == "Description") {
      write_string(object->GetDescription());
    } else if (name == "HelpText") {
      write_string(object->GetHelpText());
    } else if (name == "AccessibleId") {
      write_string(object->GetAccessibleId());
    } else if (name == "Locale") {
      const char* locale = setlocale(LC_MESSAGES, nullptr);
      write_string(locale != nullptr ? locale : "");
    } else if (name == "ChildCount") {
      write_int(static_cast<int32_t>(object->GetChildCount()));
    } else if (name == "Parent") {
      writer.OpenVariant("(so)");
      if (object == root_.get()) {
        writer.OpenStruct();
        writer.WriteString(desktop_name_);
        writer.WriteObjectPath(desktop_path_.empty() ? kNullPath
                                                     : desktop_path_);
        writer.CloseStruct();
      } else {
        WriteReference(writer, object->GetParent());
      }
      writer.CloseVariant();
    } else {
      return false;
    }
    return true;
  }

  if (interface == kApplicationInterface && object == root_.get()) {
    if (name == "ToolkitName") {
      write_string("Flutter");
    } else if (name == "Version" || name == "ToolkitVersion") {
      write_string("1.0");
    } else if (name == "AtspiVersion") {
      write_string("2.1");
    } else if (name == "InterfaceVersion") {
      writer.OpenVariant("u");
      writer.WriteUInt32(0);
      writer.CloseVariant();
    } else if (name == "Id") {
      write_int(application_id_);
    } else {
      return false;
    }
    return true;
  }

  if (interface == kValueInterface) {
    std::optional<AtspiValue> value = object->GetValue();
    if (!value.has_value()) {
      return false;
    }
    if (name == "MinimumValue") {
      write_double(value->minimum);
    } else if (name == "MaximumValue") {
      write_double(value->maximum);
    } else if (name == "MinimumIncrement") {
      write_double(value->increment);
    } else if (name == "CurrentValue") {
      write_double(value->current);
    } else if (name == "Text") {
      write_string(value->text);
    } else {
      return false;
    }
    return true;
  }

  if (interface == kTextInterface && object->HasText()) {
    if (name == "CharacterCount") {
      write_int(atspi_text::CharacterCount(object->GetText()));
    } else if (name == "CaretOffset") {
      write_int(object->GetCaretOffset());
    } else {
      return false;
    }
    return true;
  }

  if (interface == kSelectionInterface && name == "NSelectedChildren" &&
      object->IsSelectionContainer()) {
    write_int(static_cast<int32_t>(object->GetSelectedChildren().size()));
    return true;
  }

  if (interface == kImageInterface && object->IsImage()) {
    if (name == "ImageDescription") {
      write_string(object->GetName());
    } else if (name == "ImageLocale") {
      const char* locale = setlocale(LC_MESSAGES, nullptr);
      write_string(locale != nullptr ? locale : "");
    } else {
      return false;
    }
    return true;
  }

  if (interface == kActionInterface && name == "NActions") {
    write_int(static_cast<int32_t>(object->GetActions().size()));
    return true;
  }

  return false;
}

bool AtspiApplication::HandleProperties(AtspiAccessible* object,
                                        const DBusMessage& call,
                                        DBusMessage* reply) {
  const std::string& member = call.member();
  DBusReader reader = call.reader();
  DBusWriter& writer = reply->writer();
  std::string interface;
  if (!reader.ReadString(&interface)) {
    *reply = DBusMessage::Error(call, kInvalidArgs, "Expected interface");
    return true;
  }

  if (member == "Get") {
    std::string name;
    if (!reader.ReadString(&name)) {
      *reply = DBusMessage::Error(call, kInvalidArgs, "Expected (ss)");
      return true;
    }
    if (!WriteProperty(object, interface, name, writer)) {
      *reply = DBusMessage::Error(call, kUnknownProperty,
                                  "Unknown property " + interface + "." + name);
    }
  } else if (member == "GetAll") {
    static const std::map<std::string, std::vector<const char*>> kProperties = {
        {kAccessibleInterface,
         {"Name", "Description", "Parent", "ChildCount", "Locale",
          "AccessibleId", "HelpText"}},
        {kApplicationInterface,
         {"ToolkitName", "Version", "ToolkitVersion", "AtspiVersion",
          "InterfaceVersion", "Id"}},
        {kValueInterface,
         {"MinimumValue", "MaximumValue", "MinimumIncrement", "CurrentValue",
          "Text"}},
        {kTextInterface, {"CharacterCount", "CaretOffset"}},
        {kActionInterface, {"NActions"}},
        {kSelectionInterface, {"NSelectedChildren"}},
        {kImageInterface, {"ImageDescription", "ImageLocale"}},
    };
    writer.OpenArray("{sv}");
    auto it = kProperties.find(interface);
    if (it != kProperties.end()) {
      for (const char* name : it->second) {
        // Check the property exists before starting the entry.
        DBusWriter scratch;
        if (!WriteProperty(object, interface, name, scratch)) {
          continue;
        }
        writer.OpenDictEntry();
        writer.WriteString(name);
        WriteProperty(object, interface, name, writer);
        writer.CloseDictEntry();
      }
    }
    writer.CloseArray();
  } else if (member == "Set") {
    std::string name, signature;
    if (!reader.ReadString(&name) || !reader.OpenVariant(&signature)) {
      *reply = DBusMessage::Error(call, kInvalidArgs, "Expected (ssv)");
      return true;
    }
    if (interface == kApplicationInterface && name == "Id" &&
        object == root_.get() && signature == "i") {
      reader.ReadInt32(&application_id_);
    } else if (interface == kValueInterface && name == "CurrentValue" &&
               signature == "d") {
      double value;
      reader.ReadDouble(&value);
      object->SetValue(value);
    } else {
      *reply = DBusMessage::Error(call, kUnknownProperty,
                                  "Can't set " + interface + "." + name);
    }
  } else {
    return false;
  }
  return true;
}

}  // namespace flutter
