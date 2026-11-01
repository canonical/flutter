// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_VIEW_MANAGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_VIEW_MANAGER_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/encodable_value.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/platform_messenger.h"
#include "flutter/shell/platform/linux/platform_view.h"

namespace flutter {

// Creates and disposes platform views as the framework requests on the
// flutter/platform_views channel, using the factories plugins register for
// each view type.
class PlatformViewManager {
 public:
  // Sets up |view|, created for the framework with |params| from its
  // creationParams, encoded with the widget's codec. Returns false if the
  // view can't be created.
  using Factory = std::function<bool(LinuxPlatformView* view,
                                     const std::vector<uint8_t>& params)>;

  // Creates a view with |id| for the display backend, or returns nullptr if
  // the backend doesn't support platform views.
  using ViewCreator =
      std::function<std::shared_ptr<LinuxPlatformView>(int64_t id)>;

  // |messenger| must outlive the manager.
  PlatformViewManager(PlatformMessenger* messenger, ViewCreator create_view);
  ~PlatformViewManager();

  // Registers the factory for views of |view_type|, replacing any existing
  // one. Called on the platform thread.
  void RegisterViewType(const std::string& view_type, Factory factory);

  // Returns the view with |id|, or nullptr. May be called from any thread.
  std::shared_ptr<LinuxPlatformView> GetView(int64_t id);

  // Disposes all views, e.g. when the framework restarts. Called on the
  // platform thread.
  void DisposeAll();

 private:
  // Handles a method call from the framework, returning the encoded
  // response.
  std::vector<uint8_t> HandleMethodCall(const uint8_t* data, size_t size);

  std::vector<uint8_t> Create(const EncodableValue* arguments);
  std::vector<uint8_t> Dispose(const EncodableValue* arguments);
  std::vector<uint8_t> DispatchPointerEvent(const EncodableValue* arguments);

  PlatformMessenger* messenger_;
  ViewCreator create_view_;

  std::map<std::string, Factory> factories_;

  std::mutex views_mutex_;
  std::map<int64_t, std::shared_ptr<LinuxPlatformView>> views_;

  FML_DISALLOW_COPY_AND_ASSIGN(PlatformViewManager);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_VIEW_MANAGER_H_
