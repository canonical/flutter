// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_ATSPI_APPLICATION_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_ATSPI_APPLICATION_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/accessible_view.h"
#include "flutter/shell/platform/linux/atspi_accessible.h"
#include "flutter/shell/platform/linux/dbus_connection.h"
#include "flutter/shell/platform/linux/event_loop.h"

namespace flutter {

// Exposes the application's windows and their semantics trees to assistive
// technologies, such as screen readers, using AT-SPI.
//
// The application connects to the accessibility bus while the session's
// accessibility status (org.a11y.Status) is enabled, and registers its root
// object with the AT-SPI registry.
class AtspiApplication : public AtspiEventSink {
 public:
  class Observer {
   public:
    virtual ~Observer() = default;

    // Called when assistive technologies start or stop using the
    // application. Semantics only need to be generated when enabled.
    virtual void OnAccessibilityEnabledChanged(bool enabled) = 0;
  };

  explicit AtspiApplication(EventLoop* loop);
  ~AtspiApplication() override;

  // Starts watching the session's accessibility status.
  void Start();

  // True if the application is registered with the AT-SPI registry.
  bool enabled() const { return registered_; }

  // The name of the application, shown by assistive technologies.
  void set_name(std::string name) { name_ = std::move(name); }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // The application's root object, the parent of all windows.
  AtspiAccessible* root() const;

  // Returns the D-Bus object path to use for a new window.
  std::string NewWindowPath();

  // Adds and removes windows from the application's children.
  void AddWindow(AccessibleView* window);
  void RemoveWindow(AccessibleView* window);

  // |AtspiEventSink|
  bool IsActive() override;
  void Emit(const AtspiEvent& event) override;

 private:
  class Root;

  void OnSessionReady();
  void OnSessionSignal(const DBusMessage& signal);
  void ReadStatus(DBusReader& reader);
  void UpdateStatus();
  void ConnectAccessibilityBus(const std::string& address);
  void DisconnectAccessibilityBus();
  void Register();
  void RequestRegisteredEvents();
  void OnAccessibilitySignal(const DBusMessage& signal);
  void SetRegistered(bool registered);

  // Responds to a method call on the accessibility bus. Returns false if the
  // method isn't known.
  bool HandleMethodCall(const DBusMessage& call);
  AtspiAccessible* FindObject(const std::string& path);
  bool HandleAccessible(AtspiAccessible* object,
                        const DBusMessage& call,
                        DBusMessage* reply);
  bool HandleApplication(const DBusMessage& call, DBusMessage* reply);
  bool HandleComponent(AtspiAccessible* object,
                       const DBusMessage& call,
                       DBusMessage* reply);
  bool HandleAction(AtspiAccessible* object,
                    const DBusMessage& call,
                    DBusMessage* reply);
  bool HandleText(AtspiAccessible* object,
                  const DBusMessage& call,
                  DBusMessage* reply);
  bool HandleEditableText(AtspiAccessible* object,
                          const DBusMessage& call,
                          DBusMessage* reply);
  bool HandleProperties(AtspiAccessible* object,
                        const DBusMessage& call,
                        DBusMessage* reply);

  // The interfaces |object| implements.
  std::vector<std::string> GetInterfaces(AtspiAccessible* object);

  // Writes the property |name| of |interface| on |object| as a variant.
  // Returns false if there is no such property.
  bool WriteProperty(AtspiAccessible* object,
                     const std::string& interface,
                     const std::string& name,
                     DBusWriter& writer);

  // Writes a reference to |object|, a (so) struct.
  void WriteReference(DBusWriter& writer, AtspiAccessible* object);

  // True if a listener wants events of this type.
  bool IsEventNeeded(const AtspiEvent& event);

  EventLoop* loop_;
  std::string name_;
  std::unique_ptr<Root> root_;
  std::vector<AccessibleView*> windows_;
  uint64_t next_window_serial_ = 1;
  std::vector<Observer*> observers_;

  std::unique_ptr<DBusConnection> session_;
  bool status_enabled_ = false;
  bool screen_reader_enabled_ = false;

  std::unique_ptr<DBusConnection> accessibility_bus_;
  bool registered_ = false;
  std::string desktop_name_;
  std::string desktop_path_;
  int32_t application_id_ = 0;

  // The events listeners have registered for, as (bus name, event) pairs.
  // Until the registry reports them, all events are sent.
  bool events_known_ = false;
  std::vector<std::pair<std::string, std::string>> registered_events_;

  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(AtspiApplication);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_ATSPI_APPLICATION_H_
