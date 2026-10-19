// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "battery_plugin.h"

#include <flutter/event_stream_handler_functions.h>
#include <flutter/standard_method_codec.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace {

constexpr char kPowerSupplyPath[] = "/sys/class/power_supply";

// How often to check if the charging state has changed.
constexpr std::chrono::seconds kChargingPollInterval(1);

// Returns the first line of a sysfs attribute, or std::nullopt if not present.
std::optional<std::string> ReadAttribute(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::string value;
  if (!std::getline(file, value)) {
    return std::nullopt;
  }
  return value;
}

// Returns the paths of all the batteries in the system.
std::vector<std::filesystem::path> GetBatteries() {
  std::vector<std::filesystem::path> batteries;
  std::error_code error;
  for (const auto& entry :
       std::filesystem::directory_iterator(kPowerSupplyPath, error)) {
    if (ReadAttribute(entry.path() / "type") == "Battery") {
      batteries.push_back(entry.path());
    }
  }
  return batteries;
}

// Returns "charging" if any battery is charging or full, otherwise
// "discharging".
std::string GetChargingState() {
  for (const auto& battery : GetBatteries()) {
    std::optional<std::string> status = ReadAttribute(battery / "status");
    if (status == "Charging" || status == "Full") {
      return "charging";
    }
  }
  return "discharging";
}

}  // namespace

BatteryPlugin::BatteryPlugin(flutter::FlutterDesktopEngine& engine)
    : engine_(engine),
      battery_channel_(engine.messenger(), "samples.flutter.io/battery",
                       &flutter::StandardMethodCodec::GetInstance()),
      charging_channel_(engine.messenger(), "samples.flutter.io/charging",
                        &flutter::StandardMethodCodec::GetInstance()),
      self_(std::make_shared<BatteryPlugin*>(this)) {
  battery_channel_.SetMethodCallHandler(
      [](const flutter::MethodCall<flutter::EncodableValue>& call,
         std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
             result) {
        if (call.method_name() != "getBatteryLevel") {
          result->NotImplemented();
          return;
        }

        // Use the first battery that reports a capacity.
        for (const auto& battery : GetBatteries()) {
          std::optional<std::string> capacity =
              ReadAttribute(battery / "capacity");
          if (capacity) {
            result->Success(
                flutter::EncodableValue(std::atoi(capacity->c_str())));
            return;
          }
        }
        result->Error("NO_BATTERY", "Device does not have a battery.");
      });

  charging_channel_.SetStreamHandler(
      std::make_unique<
          flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
          [this](const flutter::EncodableValue* arguments,
                 std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&&
                     events)
              -> std::unique_ptr<
                  flutter::StreamHandlerError<flutter::EncodableValue>> {
            charging_sink_ = std::move(events);
            last_charge_event_.clear();
            UpdateChargingState();
            return nullptr;
          },
          [this](const flutter::EncodableValue* arguments)
              -> std::unique_ptr<
                  flutter::StreamHandlerError<flutter::EncodableValue>> {
            charging_sink_.reset();
            return nullptr;
          }));
}

BatteryPlugin::~BatteryPlugin() {
  *self_ = nullptr;
  battery_channel_.SetMethodCallHandler(nullptr);
  charging_channel_.SetStreamHandler(nullptr);
}

void BatteryPlugin::UpdateChargingState() {
  if (!charging_sink_) {
    return;
  }

  std::string charge_event = GetChargingState();
  if (charge_event != last_charge_event_) {
    charging_sink_->Success(flutter::EncodableValue(charge_event));
    last_charge_event_ = charge_event;
  }

  if (update_scheduled_) {
    return;
  }
  update_scheduled_ = true;
  engine_.post_task(
      [self = std::weak_ptr<BatteryPlugin*>(self_)]() {
        std::shared_ptr<BatteryPlugin*> plugin = self.lock();
        if (plugin && *plugin) {
          (*plugin)->update_scheduled_ = false;
          (*plugin)->UpdateChargingState();
        }
      },
      kChargingPollInterval);
}
