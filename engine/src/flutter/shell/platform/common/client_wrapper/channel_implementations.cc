// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the implementations of the channel classes in the
// wrapper that are not fully inline. They only depend on BinaryMessenger, so
// they can be used by embedders that provide their own messenger.
//
// This is included by core_implementations.cc, so clients of the full wrapper
// must not also compile this file.

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "include/flutter/engine_method_result.h"
#include "include/flutter/method_channel.h"
#include "include/flutter/standard_method_codec.h"

namespace flutter {

// ========== engine_method_result.h ==========

namespace internal {

ReplyManager::ReplyManager(BinaryReply reply_handler)
    : reply_handler_(std::move(reply_handler)) {
  assert(reply_handler_);
}

ReplyManager::~ReplyManager() {
  if (reply_handler_) {
    // Warn, rather than send a not-implemented response, since the engine may
    // no longer be valid at this point.
    std::cerr
        << "Warning: Failed to respond to a message. This is a memory leak."
        << std::endl;
  }
}

void ReplyManager::SendResponseData(const std::vector<uint8_t>* data) {
  if (!reply_handler_) {
    std::cerr
        << "Error: Only one of Success, Error, or NotImplemented can be "
           "called,"
        << " and it can be called exactly once. Ignoring duplicate result."
        << std::endl;
    return;
  }

  const uint8_t* message = data && !data->empty() ? data->data() : nullptr;
  size_t message_size = data ? data->size() : 0;
  reply_handler_(message, message_size);
  reply_handler_ = nullptr;
}

}  // namespace internal

// ========== method_channel.h ==========

namespace {

constexpr char kControlChannelName[] = "dev.flutter/channel-buffers";
constexpr char kResizeMethod[] = "resize";
constexpr char kOverflowMethod[] = "overflow";

}  // namespace

namespace internal {

void ResizeChannel(BinaryMessenger* messenger, std::string name, int new_size) {
  auto control_channel = std::make_unique<MethodChannel<EncodableValue>>(
      messenger, kControlChannelName, &StandardMethodCodec::GetInstance());

  // The deserialization logic handles only 32 bits values, see
  // https://github.com/flutter/flutter/blob/230240c56880f2c19bf92d2c32203b064054f173/engine/src/flutter/lib/ui/channel_buffers.dart#L523
  control_channel->InvokeMethod(
      kResizeMethod, std::make_unique<EncodableValue>(EncodableList{
                         EncodableValue(name),
                         EncodableValue(static_cast<int32_t>(new_size)),
                     }));
}

void SetChannelWarnsOnOverflow(BinaryMessenger* messenger,
                               std::string name,
                               bool warns) {
  auto control_channel = std::make_unique<MethodChannel<EncodableValue>>(
      messenger, kControlChannelName, &StandardMethodCodec::GetInstance());

  control_channel->InvokeMethod(kOverflowMethod,
                                std::make_unique<EncodableValue>(EncodableList{
                                    EncodableValue(name),
                                    EncodableValue(!warns),
                                }));
}

}  // namespace internal

}  // namespace flutter
