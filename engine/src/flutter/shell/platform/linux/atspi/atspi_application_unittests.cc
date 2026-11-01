// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/atspi/atspi_application.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "flutter/shell/platform/linux/dbus/fake_dbus_bus.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr char kRootPath[] = "/org/a11y/atspi/accessible/root";
constexpr char kAccessibleInterface[] = "org.a11y.atspi.Accessible";

class FakeHost : public AccessibleView::Host {
 public:
  std::string GetAccessibleTitle() override { return "Test Window"; }
  AtspiRole GetAccessibleRole() override { return AtspiRole::kFrame; }
  AtspiStateSet GetWindowStates() override { return {}; }
  AtspiRect GetWindowExtents() override { return {0, 0, 640, 480}; }
  double GetPixelRatio() override { return 1.0; }
  std::vector<AtspiRelation> GetWindowRelations() override { return relations; }
  void DispatchSemanticsAction(uint64_t node_id,
                               FlutterSemanticsAction action,
                               std::vector<uint8_t> data) override {}

  std::vector<AtspiRelation> relations;
};

class RecordingObserver : public AtspiApplication::Observer {
 public:
  void OnAccessibilityEnabledChanged(bool value) override { enabled = value; }
  bool enabled = false;
};

// Sets an environment variable for the duration of a test.
class ScopedEnv {
 public:
  ScopedEnv(const char* name, const std::string& value) : name_(name) {
    const char* old = getenv(name);
    if (old != nullptr) {
      old_value_ = old;
    }
    setenv(name, value.c_str(), 1);
  }
  ~ScopedEnv() {
    if (old_value_.has_value()) {
      setenv(name_, old_value_->c_str(), 1);
    } else {
      unsetenv(name_);
    }
  }

 private:
  const char* name_;
  std::optional<std::string> old_value_;
};

class AtspiApplicationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Listen on an abstract socket for the accessibility bus.
    std::string name = "flutter-atspi-test-" + std::to_string(getpid());
    listen_fd_.reset(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    ASSERT_TRUE(listen_fd_.is_valid());
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path + 1, name.data(), name.size());
    socklen_t length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                              1 + name.size());
    ASSERT_EQ(
        bind(listen_fd_.get(), reinterpret_cast<sockaddr*>(&address), length),
        0);
    ASSERT_EQ(listen(listen_fd_.get(), 1), 0);

    session_env_.emplace("DBUS_SESSION_BUS_ADDRESS",
                         "unix:path=/nonexistent/flutter-test-bus");
    atspi_env_.emplace("AT_SPI_BUS_ADDRESS", "unix:abstract=" + name);

    application_ = std::make_unique<AtspiApplication>(&loop_);
    application_->AddObserver(&observer_);
    application_->set_name("test-app");
    view_ = std::make_unique<AccessibleView>(&host_, application_.get(),
                                             application_->root(),
                                             application_->NewWindowPath());
    application_->AddWindow(view_.get());
    application_->Start();

    fml::UniqueFD bus_fd(
        accept4(listen_fd_.get(), nullptr, nullptr, SOCK_CLOEXEC));
    ASSERT_TRUE(bus_fd.is_valid());
    bus_ = std::make_unique<FakeBus>(std::move(bus_fd));
    bus_->Start();
  }

  void TearDown() override {
    if (application_) {
      application_->RemoveWindow(view_.get());
      application_->RemoveObserver(&observer_);
    }
    view_.reset();
    application_.reset();
  }

  // Reads the next message sent by the application.
  std::optional<DBusMessage> Read() {
    if (!RunUntil(loop_, [this]() { return bus_->HasInput(); })) {
      return std::nullopt;
    }
    return bus_->ReadMessage();
  }

  // Answers the registration calls, reporting |events| as registered.
  void Register(const std::vector<std::string>& events) {
    bool embedded = false;
    bool events_sent = false;
    while (!embedded || !events_sent) {
      std::optional<DBusMessage> message = Read();
      ASSERT_TRUE(message.has_value());
      DBusMessage reply = DBusMessage::MethodReturn(*message);
      if (message->member() == "Embed") {
        EXPECT_EQ(message->path(), kRootPath);
        DBusReader reader = message->reader();
        std::string name, path;
        EXPECT_TRUE(reader.OpenStruct());
        EXPECT_TRUE(reader.ReadString(&name));
        EXPECT_TRUE(reader.ReadObjectPath(&path));
        EXPECT_EQ(name, ":1.42");
        EXPECT_EQ(path, kRootPath);
        reply.writer().OpenStruct();
        reply.writer().WriteString(":1.1");
        reply.writer().WriteObjectPath("/org/a11y/atspi/accessible/root");
        reply.writer().CloseStruct();
        embedded = true;
      } else if (message->member() == "GetRegisteredEvents") {
        reply.writer().OpenArray("(ss)");
        for (const std::string& event : events) {
          reply.writer().OpenStruct();
          reply.writer().WriteString(":1.7");
          reply.writer().WriteString(event);
          reply.writer().CloseStruct();
        }
        reply.writer().CloseArray();
        events_sent = true;
      }
      bus_->Send(reply);
    }
    ASSERT_TRUE(RunUntil(loop_, [this]() { return observer_.enabled; }));
  }

  // Calls a method on the application and returns the reply.
  std::optional<DBusMessage> Call(DBusMessage call) {
    bus_->Send(call);
    return Read();
  }

  DBusMessage MethodCall(const std::string& path,
                         const std::string& interface,
                         const std::string& member) {
    return DBusMessage::MethodCall(":1.42", path, interface, member);
  }

  EventLoop loop_;
  fml::UniqueFD listen_fd_;
  std::optional<ScopedEnv> session_env_;
  std::optional<ScopedEnv> atspi_env_;
  RecordingObserver observer_;
  FakeHost host_;
  std::unique_ptr<AtspiApplication> application_;
  std::unique_ptr<AccessibleView> view_;
  std::unique_ptr<FakeBus> bus_;
};

}  // namespace

TEST_F(AtspiApplicationTest, Registers) {
  EXPECT_FALSE(application_->enabled());
  Register({});
  EXPECT_TRUE(application_->enabled());
}

TEST_F(AtspiApplicationTest, RootObject) {
  Register({});

  std::optional<DBusMessage> reply =
      Call(MethodCall(kRootPath, kAccessibleInterface, "GetChildren"));
  ASSERT_TRUE(reply.has_value());
  ASSERT_EQ(reply->type(), DBusMessage::Type::kMethodReturn);
  ASSERT_EQ(reply->signature(), "a(so)");
  DBusReader reader = reply->reader();
  std::string name, path;
  ASSERT_TRUE(reader.OpenArray());
  ASSERT_TRUE(reader.OpenStruct());
  EXPECT_TRUE(reader.ReadString(&name));
  EXPECT_TRUE(reader.ReadObjectPath(&path));
  EXPECT_TRUE(reader.CloseStruct());
  EXPECT_FALSE(reader.HasMore());
  EXPECT_EQ(name, ":1.42");
  EXPECT_EQ(path, view_->GetPath());

  DBusMessage get =
      MethodCall(kRootPath, "org.freedesktop.DBus.Properties", "Get");
  get.writer().WriteString(kAccessibleInterface);
  get.writer().WriteString("Name");
  reply = Call(get);
  ASSERT_TRUE(reply.has_value());
  ASSERT_EQ(reply->signature(), "v");
  reader = reply->reader();
  std::string signature, value;
  ASSERT_TRUE(reader.OpenVariant(&signature));
  EXPECT_EQ(signature, "s");
  EXPECT_TRUE(reader.ReadString(&value));
  EXPECT_EQ(value, "test-app");

  reply = Call(MethodCall(kRootPath, kAccessibleInterface, "GetInterfaces"));
  ASSERT_TRUE(reply.has_value());
  reader = reply->reader();
  std::vector<std::string> interfaces;
  ASSERT_TRUE(reader.OpenArray());
  while (reader.HasMore()) {
    std::string interface;
    ASSERT_TRUE(reader.ReadString(&interface));
    interfaces.push_back(interface);
  }
  EXPECT_EQ(interfaces,
            (std::vector<std::string>{kAccessibleInterface,
                                      "org.a11y.atspi.Application"}));
}

TEST_F(AtspiApplicationTest, WindowObject) {
  Register({});

  std::optional<DBusMessage> reply =
      Call(MethodCall(view_->GetPath(), kAccessibleInterface, "GetRole"));
  ASSERT_TRUE(reply.has_value());
  uint32_t role;
  ASSERT_TRUE(reply->reader().ReadUInt32(&role));
  EXPECT_EQ(role, static_cast<uint32_t>(AtspiRole::kFrame));

  DBusMessage get_all =
      MethodCall(view_->GetPath(), "org.freedesktop.DBus.Properties", "GetAll");
  get_all.writer().WriteString(kAccessibleInterface);
  reply = Call(get_all);
  ASSERT_TRUE(reply.has_value());
  ASSERT_EQ(reply->signature(), "a{sv}");
  DBusReader reader = reply->reader();
  std::map<std::string, std::string> strings;
  ASSERT_TRUE(reader.OpenArray());
  while (reader.HasMore()) {
    std::string key, signature;
    ASSERT_TRUE(reader.OpenDictEntry());
    ASSERT_TRUE(reader.ReadString(&key));
    ASSERT_TRUE(reader.OpenVariant(&signature));
    if (signature == "s") {
      ASSERT_TRUE(reader.ReadString(&strings[key]));
    } else {
      ASSERT_TRUE(reader.Skip());
    }
    ASSERT_TRUE(reader.CloseVariant());
    ASSERT_TRUE(reader.CloseDictEntry());
  }
  EXPECT_EQ(strings["Name"], "Test Window");

  DBusMessage extents =
      MethodCall(view_->GetPath(), "org.a11y.atspi.Component", "GetExtents");
  extents.writer().WriteUInt32(0);
  reply = Call(extents);
  ASSERT_TRUE(reply.has_value());
  ASSERT_EQ(reply->signature(), "(iiii)");
  reader = reply->reader();
  int32_t x, y, width, height;
  ASSERT_TRUE(reader.OpenStruct());
  ASSERT_TRUE(reader.ReadInt32(&x));
  ASSERT_TRUE(reader.ReadInt32(&y));
  ASSERT_TRUE(reader.ReadInt32(&width));
  ASSERT_TRUE(reader.ReadInt32(&height));
  EXPECT_EQ(width, 640);
  EXPECT_EQ(height, 480);
}

TEST_F(AtspiApplicationTest, RelationSet) {
  Register({});
  host_.relations = {{AtspiRelationType::kSubwindowOf, {view_.get()}}};

  std::optional<DBusMessage> reply = Call(
      MethodCall(view_->GetPath(), kAccessibleInterface, "GetRelationSet"));
  ASSERT_TRUE(reply.has_value());
  ASSERT_EQ(reply->signature(), "a(ua(so))");
  DBusReader reader = reply->reader();
  uint32_t type;
  std::string name, path;
  ASSERT_TRUE(reader.OpenArray());
  ASSERT_TRUE(reader.OpenStruct());
  ASSERT_TRUE(reader.ReadUInt32(&type));
  EXPECT_EQ(type, 12u);
  ASSERT_TRUE(reader.OpenArray());
  ASSERT_TRUE(reader.OpenStruct());
  ASSERT_TRUE(reader.ReadString(&name));
  ASSERT_TRUE(reader.ReadObjectPath(&path));
  EXPECT_EQ(path, view_->GetPath());
}

TEST_F(AtspiApplicationTest, SelectionAndImage) {
  Register({});

  FlutterSemanticsFlags flags[3] = {};
  FlutterSemanticsNode2 nodes[3] = {};
  int32_t children[2] = {1, 2};
  for (int i = 0; i < 3; i++) {
    flags[i].struct_size = sizeof(FlutterSemanticsFlags);
    nodes[i].struct_size = sizeof(FlutterSemanticsNode2);
    nodes[i].id = i;
    nodes[i].flags2 = &flags[i];
    nodes[i].rect = {0, 0, 10, 20};
    nodes[i].transform.scaleX = 1;
    nodes[i].transform.scaleY = 1;
    nodes[i].transform.pers2 = 1;
  }
  nodes[0].child_count = 2;
  nodes[0].children_in_traversal_order = children;
  nodes[0].children_in_hit_test_order = children;
  flags[1].is_selected = kFlutterTristateFalse;
  flags[2].is_selected = kFlutterTristateTrue;
  flags[2].is_image = true;
  nodes[2].label = "Logo";
  FlutterSemanticsNode2* node_pointers[3] = {&nodes[0], &nodes[1], &nodes[2]};
  FlutterSemanticsUpdate2 update = {};
  update.struct_size = sizeof(update);
  update.node_count = 3;
  update.nodes = node_pointers;
  view_->Update(update);
  // Skip the children-changed event.
  ASSERT_TRUE(Read().has_value());

  std::string container = view_->GetPath() + "/0";
  DBusMessage get_selected =
      MethodCall(container, "org.a11y.atspi.Selection", "GetSelectedChild");
  get_selected.writer().WriteInt32(0);
  std::optional<DBusMessage> reply = Call(get_selected);
  ASSERT_TRUE(reply.has_value());
  ASSERT_EQ(reply->signature(), "(so)");
  DBusReader reader = reply->reader();
  std::string name, path;
  ASSERT_TRUE(reader.OpenStruct());
  ASSERT_TRUE(reader.ReadString(&name));
  ASSERT_TRUE(reader.ReadObjectPath(&path));
  EXPECT_EQ(path, view_->GetPath() + "/2");

  DBusMessage get =
      MethodCall(container, "org.freedesktop.DBus.Properties", "Get");
  get.writer().WriteString("org.a11y.atspi.Selection");
  get.writer().WriteString("NSelectedChildren");
  reply = Call(get);
  ASSERT_TRUE(reply.has_value());
  reader = reply->reader();
  std::string signature;
  int32_t count;
  ASSERT_TRUE(reader.OpenVariant(&signature));
  ASSERT_TRUE(reader.ReadInt32(&count));
  EXPECT_EQ(count, 1);

  DBusMessage size = MethodCall(view_->GetPath() + "/2", "org.a11y.atspi.Image",
                                "GetImageSize");
  reply = Call(size);
  ASSERT_TRUE(reply.has_value());
  ASSERT_EQ(reply->signature(), "ii");
  int32_t width, height;
  reader = reply->reader();
  ASSERT_TRUE(reader.ReadInt32(&width));
  ASSERT_TRUE(reader.ReadInt32(&height));
  EXPECT_EQ(width, 10);
  EXPECT_EQ(height, 20);
}

TEST_F(AtspiApplicationTest, UnknownObject) {
  Register({});
  std::optional<DBusMessage> reply =
      Call(MethodCall("/org/flutter/a11y/99", kAccessibleInterface, "GetRole"));
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->type(), DBusMessage::Type::kError);
  EXPECT_EQ(reply->error_name(), "org.freedesktop.DBus.Error.UnknownObject");
}

TEST_F(AtspiApplicationTest, FiltersEvents) {
  Register({"Object:Announcement"});

  // Not registered for, so not sent.
  application_->Emit(
      {.source = view_.get(), .member = "TextCaretMoved", .detail1 = 1});
  view_->Announce("Hello", false);

  std::optional<DBusMessage> signal = Read();
  ASSERT_TRUE(signal.has_value());
  EXPECT_EQ(signal->type(), DBusMessage::Type::kSignal);
  EXPECT_EQ(signal->path(), view_->GetPath());
  EXPECT_EQ(signal->interface(), "org.a11y.atspi.Event.Object");
  EXPECT_EQ(signal->member(), "Announcement");
  EXPECT_EQ(signal->signature(), "siiva{sv}");
  DBusReader reader = signal->reader();
  std::string detail, signature, message;
  int32_t detail1, detail2;
  EXPECT_TRUE(reader.ReadString(&detail));
  EXPECT_TRUE(reader.ReadInt32(&detail1));
  EXPECT_TRUE(reader.ReadInt32(&detail2));
  EXPECT_TRUE(reader.OpenVariant(&signature));
  EXPECT_EQ(signature, "s");
  EXPECT_TRUE(reader.ReadString(&message));
  EXPECT_EQ(detail1, 1);
  EXPECT_EQ(message, "Hello");
}

TEST_F(AtspiApplicationTest, ListenerRegistered) {
  Register({});
  DBusMessage registered =
      DBusMessage::Signal("/org/a11y/atspi/registry", "org.a11y.atspi.Registry",
                          "EventListenerRegistered");
  registered.writer().WriteString(":1.9");
  registered.writer().WriteString("Window:");
  registered.writer().OpenArray("s");
  registered.writer().CloseArray();
  bus_->Send(registered);

  // Window events are now sent, Object events other than cache updates
  // aren't.
  // A round trip ensures the signal has been handled.
  ASSERT_TRUE(
      Call(MethodCall(kRootPath, kAccessibleInterface, "GetRole")).has_value());
  view_->Announce("Ignored", false);
  application_->Emit(
      {.source = view_.get(), .category = "Window", .member = "Activate"});
  std::optional<DBusMessage> signal = Read();
  ASSERT_TRUE(signal.has_value());
  EXPECT_EQ(signal->interface(), "org.a11y.atspi.Event.Window");
  EXPECT_EQ(signal->member(), "Activate");
}

}  // namespace testing
}  // namespace flutter
