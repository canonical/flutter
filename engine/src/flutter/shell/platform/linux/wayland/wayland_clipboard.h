// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_CLIPBOARD_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_CLIPBOARD_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/clipboard.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/wayland/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/wayland/wayland_seat.h"

namespace flutter {

// The clipboard of a Wayland seat, using wl_data_device.
//
// The compositor only shares the clipboard with the application while one of
// its windows has keyboard focus, and only allows it to be set in response
// to input.
class WaylandClipboard : public Clipboard {
 public:
  // |manager|, |seat| and |loop| must outlive the clipboard.
  WaylandClipboard(wl_data_device_manager* manager,
                   WaylandSeat* seat,
                   EventLoop* loop);
  ~WaylandClipboard() override;

  // |Clipboard|
  bool SetText(const std::string& text) override;

  // |Clipboard|
  void GetText(TextCallback callback) override;

  // |Clipboard|
  bool HasText() const override;

  // Returns the preferred text MIME type from |mime_types|, or nullptr if
  // there is none.
  static const char* FindTextMimeType(
      const std::vector<std::string>& mime_types);

 private:
  // Data being read from or written to another application.
  struct Transfer {
    fml::UniqueFD fd;
    uint64_t watch_id = 0;

    // The text being written, and how much has been written.
    std::shared_ptr<const std::string> text;
    size_t offset = 0;

    // The text read so far, and the callback to call when complete.
    std::string received;
    TextCallback callback;
  };

  static void DataOffer(void* data,
                        wl_data_device* device,
                        wl_data_offer* offer);
  static void Enter(void* data,
                    wl_data_device* device,
                    uint32_t serial,
                    wl_surface* surface,
                    wl_fixed_t x,
                    wl_fixed_t y,
                    wl_data_offer* offer);
  static void Leave(void* data, wl_data_device* device);
  static void Motion(void* data,
                     wl_data_device* device,
                     uint32_t time,
                     wl_fixed_t x,
                     wl_fixed_t y);
  static void Drop(void* data, wl_data_device* device);
  static void Selection(void* data,
                        wl_data_device* device,
                        wl_data_offer* offer);

  static void OfferMimeType(void* data,
                            wl_data_offer* offer,
                            const char* mime_type);
  static void OfferSourceActions(void* data,
                                 wl_data_offer* offer,
                                 uint32_t actions);
  static void OfferAction(void* data, wl_data_offer* offer, uint32_t action);

  static void SourceTarget(void* data,
                           wl_data_source* source,
                           const char* mime_type);
  static void SourceSend(void* data,
                         wl_data_source* source,
                         const char* mime_type,
                         int32_t fd);
  static void SourceCancelled(void* data, wl_data_source* source);
  static void SourceDndDropPerformed(void* data, wl_data_source* source);
  static void SourceDndFinished(void* data, wl_data_source* source);
  static void SourceAction(void* data, wl_data_source* source, uint32_t action);

  static const wl_data_device_listener kDeviceListener;
  static const wl_data_offer_listener kOfferListener;
  static const wl_data_source_listener kSourceListener;

  // Destroys an offer and forgets its MIME types.
  void DestroyOffer(wl_data_offer* offer);

  // Starts watching |transfer|'s file descriptor for |events|.
  void StartTransfer(std::unique_ptr<Transfer> transfer, uint32_t events);

  // Reads or writes more data when the file descriptor is ready.
  void OnTransferReady(uint64_t id);

  // Ends a transfer, calling its callback with |text| if reading.
  void FinishTransfer(uint64_t id, std::optional<std::string> text);

  wl_data_device_manager* manager_;
  WaylandSeat* seat_;
  EventLoop* loop_;
  wl_data_device* device_ = nullptr;

  // The MIME types of the offers from other applications.
  std::map<wl_data_offer*, std::vector<std::string>> offers_;

  // The current clipboard and drag and drop offers, or nullptr.
  wl_data_offer* selection_offer_ = nullptr;
  wl_data_offer* drag_offer_ = nullptr;

  // The clipboard contents while owned by this application.
  wl_data_source* source_ = nullptr;
  std::shared_ptr<const std::string> text_;

  // Transfers in progress, by ID.
  std::map<uint64_t, std::unique_ptr<Transfer>> transfers_;
  uint64_t next_transfer_id_ = 1;

  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandClipboard);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_CLIPBOARD_H_
