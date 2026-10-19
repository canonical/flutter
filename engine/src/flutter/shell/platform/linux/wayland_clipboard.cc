// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland_clipboard.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

constexpr uint32_t kMinDeviceReleaseVersion = 2;

// Text MIME types, in order of preference. The legacy X11 names are
// offered for applications running in Xwayland.
constexpr const char* kTextMimeTypes[] = {
    "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "STRING", "TEXT",
};

// How long to wait for another application to send the clipboard.
constexpr std::chrono::seconds kReadTimeout(5);

constexpr size_t kReadSize = 4096;

// Writes to a pipe without raising SIGPIPE if the reader has closed it,
// which would otherwise end the process.
ssize_t WriteWithoutSigpipe(int fd, const void* data, size_t size) {
  sigset_t pipe_set;
  sigset_t old_set;
  sigemptyset(&pipe_set);
  sigaddset(&pipe_set, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &pipe_set, &old_set);
  ssize_t result = write(fd, data, size);
  int error = errno;
  if (result < 0 && error == EPIPE && !sigismember(&old_set, SIGPIPE)) {
    // Discard the signal this write raised before unblocking it.
    timespec zero = {};
    sigtimedwait(&pipe_set, nullptr, &zero);
  }
  pthread_sigmask(SIG_SETMASK, &old_set, nullptr);
  errno = error;
  return result;
}

}  // namespace

const wl_data_device_listener WaylandClipboard::kDeviceListener = {
    .data_offer = DataOffer,
    .enter = Enter,
    .leave = Leave,
    .motion = Motion,
    .drop = Drop,
    .selection = Selection,
};

const wl_data_offer_listener WaylandClipboard::kOfferListener = {
    .offer = OfferMimeType,
    .source_actions = OfferSourceActions,
    .action = OfferAction,
};

const wl_data_source_listener WaylandClipboard::kSourceListener = {
    .target = SourceTarget,
    .send = SourceSend,
    .cancelled = SourceCancelled,
    .dnd_drop_performed = SourceDndDropPerformed,
    .dnd_finished = SourceDndFinished,
    .action = SourceAction,
};

WaylandClipboard::WaylandClipboard(wl_data_device_manager* manager,
                                   WaylandSeat* seat,
                                   EventLoop* loop)
    : manager_(manager), seat_(seat), loop_(loop) {
  device_ = wl_data_device_manager_get_data_device(manager_, seat_->seat());
  wl_data_device_add_listener(device_, &kDeviceListener, this);
}

WaylandClipboard::~WaylandClipboard() {
  for (const auto& [id, transfer] : transfers_) {
    loop_->RemoveWatch(transfer->watch_id);
  }
  transfers_.clear();
  if (source_ != nullptr) {
    wl_data_source_destroy(source_);
  }
  for (const auto& [offer, mime_types] : offers_) {
    wl_data_offer_destroy(offer);
  }
  if (wl_data_device_get_version(device_) >= kMinDeviceReleaseVersion) {
    wl_data_device_release(device_);
  } else {
    wl_data_device_destroy(device_);
  }
}

const char* WaylandClipboard::FindTextMimeType(
    const std::vector<std::string>& mime_types) {
  for (const char* type : kTextMimeTypes) {
    if (std::find(mime_types.begin(), mime_types.end(), type) !=
        mime_types.end()) {
      return type;
    }
  }
  return nullptr;
}

bool WaylandClipboard::SetText(const std::string& text) {
  uint32_t serial = seat_->last_input_serial();
  if (serial == 0) {
    return false;
  }

  wl_data_source* source = wl_data_device_manager_create_data_source(manager_);
  for (const char* type : kTextMimeTypes) {
    wl_data_source_offer(source, type);
  }
  wl_data_source_add_listener(source, &kSourceListener, this);
  wl_data_device_set_selection(device_, source, serial);

  if (source_ != nullptr) {
    wl_data_source_destroy(source_);
  }
  source_ = source;
  text_ = std::make_shared<const std::string>(text);
  return true;
}

void WaylandClipboard::GetText(TextCallback callback) {
  if (source_ != nullptr) {
    callback(*text_);
    return;
  }

  const char* mime_type = nullptr;
  if (selection_offer_ != nullptr) {
    mime_type = FindTextMimeType(offers_[selection_offer_]);
  }
  if (mime_type == nullptr) {
    callback(std::nullopt);
    return;
  }

  int fds[2];
  if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
    FML_LOG(ERROR) << "Failed to create pipe for clipboard: "
                   << strerror(errno);
    callback(std::nullopt);
    return;
  }
  // The display is flushed before the loop next waits.
  wl_data_offer_receive(selection_offer_, mime_type, fds[1]);
  close(fds[1]);

  auto transfer = std::make_unique<Transfer>();
  transfer->fd.reset(fds[0]);
  transfer->callback = std::move(callback);
  uint64_t id = next_transfer_id_;
  StartTransfer(std::move(transfer), EPOLLIN);

  // Applications that don't respond would otherwise leave the read waiting
  // forever.
  loop_->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), id]() {
        if (!alive.expired() && transfers_.count(id) != 0) {
          FML_LOG(ERROR) << "Timed out reading clipboard";
          FinishTransfer(id, std::nullopt);
        }
      },
      EventLoop::Clock::now() + kReadTimeout);
}

bool WaylandClipboard::HasText() const {
  if (source_ != nullptr) {
    return true;
  }
  if (selection_offer_ == nullptr) {
    return false;
  }
  auto it = offers_.find(selection_offer_);
  return it != offers_.end() && FindTextMimeType(it->second) != nullptr;
}

void WaylandClipboard::StartTransfer(std::unique_ptr<Transfer> transfer,
                                     uint32_t events) {
  uint64_t id = next_transfer_id_++;
  transfer->watch_id =
      loop_->AddWatch(transfer->fd.get(), events,
                      [this, id](uint32_t events) { OnTransferReady(id); });
  if (transfer->watch_id == 0) {
    if (transfer->callback) {
      transfer->callback(std::nullopt);
    }
    return;
  }
  transfers_[id] = std::move(transfer);
}

void WaylandClipboard::OnTransferReady(uint64_t id) {
  auto it = transfers_.find(id);
  if (it == transfers_.end()) {
    return;
  }
  Transfer* transfer = it->second.get();

  if (transfer->text) {
    while (transfer->offset < transfer->text->size()) {
      ssize_t written = WriteWithoutSigpipe(
          transfer->fd.get(), transfer->text->data() + transfer->offset,
          transfer->text->size() - transfer->offset);
      if (written < 0) {
        if (errno == EAGAIN || errno == EINTR) {
          return;
        }
        break;
      }
      transfer->offset += written;
    }
    FinishTransfer(id, std::nullopt);
    return;
  }

  char buffer[kReadSize];
  while (true) {
    ssize_t count = read(transfer->fd.get(), buffer, sizeof(buffer));
    if (count > 0) {
      transfer->received.append(buffer, count);
    } else if (count == 0) {
      FinishTransfer(id, std::move(transfer->received));
      return;
    } else if (errno == EAGAIN) {
      return;
    } else if (errno != EINTR) {
      FinishTransfer(id, std::nullopt);
      return;
    }
  }
}

void WaylandClipboard::FinishTransfer(uint64_t id,
                                      std::optional<std::string> text) {
  auto it = transfers_.find(id);
  if (it == transfers_.end()) {
    return;
  }
  std::unique_ptr<Transfer> transfer = std::move(it->second);
  transfers_.erase(it);
  loop_->RemoveWatch(transfer->watch_id);
  if (transfer->callback) {
    transfer->callback(std::move(text));
  }
}

void WaylandClipboard::DestroyOffer(wl_data_offer* offer) {
  if (offer == nullptr) {
    return;
  }
  offers_.erase(offer);
  wl_data_offer_destroy(offer);
}

void WaylandClipboard::DataOffer(void* data,
                                 wl_data_device* device,
                                 wl_data_offer* offer) {
  auto* self = static_cast<WaylandClipboard*>(data);
  self->offers_[offer] = {};
  wl_data_offer_add_listener(offer, &kOfferListener, self);
}

void WaylandClipboard::Enter(void* data,
                             wl_data_device* device,
                             uint32_t serial,
                             wl_surface* surface,
                             wl_fixed_t x,
                             wl_fixed_t y,
                             wl_data_offer* offer) {
  // Drag and drop isn't supported, so drags are never accepted.
  auto* self = static_cast<WaylandClipboard*>(data);
  self->DestroyOffer(self->drag_offer_);
  self->drag_offer_ = offer;
}

void WaylandClipboard::Leave(void* data, wl_data_device* device) {
  auto* self = static_cast<WaylandClipboard*>(data);
  self->DestroyOffer(self->drag_offer_);
  self->drag_offer_ = nullptr;
}

void WaylandClipboard::Motion(void* data,
                              wl_data_device* device,
                              uint32_t time,
                              wl_fixed_t x,
                              wl_fixed_t y) {}

void WaylandClipboard::Drop(void* data, wl_data_device* device) {
  Leave(data, device);
}

void WaylandClipboard::Selection(void* data,
                                 wl_data_device* device,
                                 wl_data_offer* offer) {
  auto* self = static_cast<WaylandClipboard*>(data);
  if (self->selection_offer_ != offer) {
    self->DestroyOffer(self->selection_offer_);
  }
  self->selection_offer_ = offer;
}

void WaylandClipboard::OfferMimeType(void* data,
                                     wl_data_offer* offer,
                                     const char* mime_type) {
  auto* self = static_cast<WaylandClipboard*>(data);
  auto it = self->offers_.find(offer);
  if (it != self->offers_.end()) {
    it->second.emplace_back(mime_type);
  }
}

void WaylandClipboard::OfferSourceActions(void* data,
                                          wl_data_offer* offer,
                                          uint32_t actions) {}

void WaylandClipboard::OfferAction(void* data,
                                   wl_data_offer* offer,
                                   uint32_t action) {}

void WaylandClipboard::SourceTarget(void* data,
                                    wl_data_source* source,
                                    const char* mime_type) {}

void WaylandClipboard::SourceSend(void* data,
                                  wl_data_source* source,
                                  const char* mime_type,
                                  int32_t fd) {
  auto* self = static_cast<WaylandClipboard*>(data);
  fml::UniqueFD owned_fd(fd);
  if (source != self->source_) {
    return;
  }
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return;
  }
  auto transfer = std::make_unique<Transfer>();
  transfer->fd = std::move(owned_fd);
  transfer->text = self->text_;
  self->StartTransfer(std::move(transfer), EPOLLOUT);
}

void WaylandClipboard::SourceCancelled(void* data, wl_data_source* source) {
  // Another application has set the clipboard.
  auto* self = static_cast<WaylandClipboard*>(data);
  if (source == self->source_) {
    self->source_ = nullptr;
    self->text_.reset();
  }
  wl_data_source_destroy(source);
}

void WaylandClipboard::SourceDndDropPerformed(void* data,
                                              wl_data_source* source) {}

void WaylandClipboard::SourceDndFinished(void* data, wl_data_source* source) {}

void WaylandClipboard::SourceAction(void* data,
                                    wl_data_source* source,
                                    uint32_t action) {}

}  // namespace flutter
