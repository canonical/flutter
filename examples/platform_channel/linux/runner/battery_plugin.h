// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNNER_BATTERY_PLUGIN_H_
#define RUNNER_BATTERY_PLUGIN_H_

#include <flutter/encodable_value.h>
#include <flutter/event_channel.h>
#include <flutter/event_sink.h>
#include <flutter/method_channel.h>
#include <flutter_linux/flutter_linux.h>

#include <memory>
#include <string>

// Provides battery information to Dart using the batteries reported by the
// kernel in /sys/class/power_supply.
class BatteryPlugin : public flutter::FlutterDesktopPlugin {
 public:
  explicit BatteryPlugin(flutter::FlutterDesktopEngine& engine);
  ~BatteryPlugin() override;

 private:
  // Sends a charging event if the charging state has changed, then checks
  // again later as sysfs does not notify of changes.
  void UpdateChargingState();

  flutter::FlutterDesktopEngine& engine_;

  // Channel for Dart code to request the battery information.
  flutter::MethodChannel<flutter::EncodableValue> battery_channel_;

  // Channel to send updates to Dart code about battery charging state.
  flutter::EventChannel<flutter::EncodableValue> charging_channel_;
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> charging_sink_;
  std::string last_charge_event_;
  bool update_scheduled_ = false;

  // Invalidated when the plugin is destroyed, to cancel scheduled updates.
  std::shared_ptr<BatteryPlugin*> self_;
};

#endif  // RUNNER_BATTERY_PLUGIN_H_
