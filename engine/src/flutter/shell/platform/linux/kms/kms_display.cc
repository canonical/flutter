// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/kms/kms_display.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <xf86drm.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/kms/kms_input.h"
#include "flutter/shell/platform/linux/kms/kms_seat.h"
#include "flutter/shell/platform/linux/kms/kms_session.h"
#include "flutter/shell/platform/linux/kms/kms_window.h"

namespace flutter {

// There is no other application to share the clipboard with, so it is kept
// in the process.
class KmsClipboard : public Clipboard {
 public:
  bool SetText(const std::string& text) override {
    text_ = text;
    return true;
  }
  void GetText(TextCallback callback) override { callback(text_); }
  bool HasText() const override { return text_.has_value(); }

 private:
  std::optional<std::string> text_;
};

namespace {

// The names the kernel gives connector types, indexed by
// DRM_MODE_CONNECTOR_*.
constexpr const char* kConnectorTypeNames[] = {
    "Unknown",   "VGA",  "DVI-I",     "DVI-D",   "DVI-A", "Composite",
    "SVIDEO",    "LVDS", "Component", "DIN",     "DP",    "HDMI-A",
    "HDMI-B",    "TV",   "eDP",       "Virtual", "DSI",   "DPI",
    "Writeback", "SPI",  "USB",
};

std::string ConnectorName(const drmModeConnector* connector) {
  const char* type = connector->connector_type < std::size(kConnectorTypeNames)
                         ? kConnectorTypeNames[connector->connector_type]
                         : "Unknown";
  return std::string(type) + "-" + std::to_string(connector->connector_type_id);
}

// Returns the mode the screen prefers.
const drmModeModeInfo* PreferredMode(const drmModeConnector* connector) {
  for (int i = 0; i < connector->count_modes; i++) {
    if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
      return &connector->modes[i];
    }
  }
  return connector->count_modes > 0 ? &connector->modes[0] : nullptr;
}

// True for screens built into the device, e.g. a laptop's screen.
bool IsInternal(const drmModeConnector* connector) {
  return connector->connector_type == DRM_MODE_CONNECTOR_eDP ||
         connector->connector_type == DRM_MODE_CONNECTOR_LVDS ||
         connector->connector_type == DRM_MODE_CONNECTOR_DSI;
}

// Returns the CRTC currently driving |connector|, or 0 if it is off.
uint32_t CurrentCrtc(int fd, const drmModeConnector* connector) {
  if (connector->encoder_id == 0) {
    return 0;
  }
  drmModeEncoder* encoder = drmModeGetEncoder(fd, connector->encoder_id);
  if (encoder == nullptr) {
    return 0;
  }
  uint32_t crtc = encoder->crtc_id;
  drmModeFreeEncoder(encoder);
  return crtc;
}

bool IsCrtcInUse(int fd, uint32_t crtc_id) {
  drmModeCrtc* crtc = drmModeGetCrtc(fd, crtc_id);
  if (crtc == nullptr) {
    return false;
  }
  bool in_use = crtc->mode_valid != 0;
  drmModeFreeCrtc(crtc);
  return in_use;
}

// Returns a CRTC that can drive |connector|, or 0 if none.
uint32_t FindCrtc(int fd,
                  const drmModeRes* resources,
                  const drmModeConnector* connector) {
  uint32_t current = CurrentCrtc(fd, connector);
  if (current != 0) {
    return current;
  }
  // Prefer a CRTC that isn't showing another screen.
  uint32_t fallback = 0;
  for (int i = 0; i < connector->count_encoders; i++) {
    drmModeEncoder* encoder = drmModeGetEncoder(fd, connector->encoders[i]);
    if (encoder == nullptr) {
      continue;
    }
    uint32_t possible = encoder->possible_crtcs;
    drmModeFreeEncoder(encoder);
    for (int j = 0; j < resources->count_crtcs; j++) {
      if ((possible & (1u << j)) == 0) {
        continue;
      }
      if (!IsCrtcInUse(fd, resources->crtcs[j])) {
        return resources->crtcs[j];
      }
      if (fallback == 0) {
        fallback = resources->crtcs[j];
      }
    }
  }
  return fallback;
}

// The classic arrow pointer, with its tip at the top left. 'X' is the
// outline and '.' the fill.
constexpr const char* kArrow[] = {
    "X",           "XX",         "X.X",        "X..X",      "X...X",
    "X....X",      "X.....X",    "X......X",   "X.......X", "X........X",
    "X.....XXXXX", "X..X..X",    "X.X X..X",   "XX  X..X",  "X    X..X",
    "     X..X",   "      X..X", "      X..X", "       XX",
};

void DrawArrow(uint32_t* pixels,
               uint32_t stride,
               uint32_t width,
               uint32_t height,
               uint32_t scale) {
  for (size_t row = 0; row < std::size(kArrow); row++) {
    for (size_t column = 0; kArrow[row][column] != '\0'; column++) {
      char c = kArrow[row][column];
      if (c != 'X' && c != '.') {
        continue;
      }
      uint32_t color = c == 'X' ? 0xff000000 : 0xffffffff;
      for (uint32_t dy = 0; dy < scale; dy++) {
        for (uint32_t dx = 0; dx < scale; dx++) {
          uint32_t x = column * scale + dx;
          uint32_t y = row * scale + dy;
          if (x < width && y < height) {
            pixels[y * (stride / 4) + x] = color;
          }
        }
      }
    }
  }
}

}  // namespace

std::unique_ptr<KmsDisplay> KmsDisplay::Create(EventLoop* event_loop) {
  std::unique_ptr<KmsDisplay> display(new KmsDisplay(event_loop));
  if (!display->Initialize()) {
    return nullptr;
  }
  return display;
}

KmsDisplay::KmsDisplay(EventLoop* event_loop) : event_loop_(event_loop) {}

KmsDisplay::~KmsDisplay() {
  input_.reset();
  if (drm_fd_.is_valid()) {
    if (cursor_handle_ != 0) {
      drmModeSetCursor(drm_fd_.get(), crtc_id_, 0, 0, 0);
      drm_mode_destroy_dumb destroy = {.handle = cursor_handle_};
      drmIoctl(drm_fd_.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    RestoreCrtc();
    session_->CloseDevice(drm_fd_.release());
  }
  if (saved_crtc_ != nullptr) {
    drmModeFreeCrtc(saved_crtc_);
  }
}

bool KmsDisplay::Initialize() {
  session_ = KmsSession::Create(event_loop_);
  if (session_ == nullptr) {
    return false;
  }

  const char* device = getenv("FLUTTER_KMS_DEVICE");
  const char* connector = getenv("FLUTTER_KMS_CONNECTOR");
  if (connector != nullptr && connector[0] == '\0') {
    connector = nullptr;
  }
  if (device != nullptr && device[0] != '\0') {
    if (!OpenDevice(device, connector)) {
      return false;
    }
  } else {
    for (int i = 0; i < 16 && !drm_fd_.is_valid(); i++) {
      OpenDevice("/dev/dri/card" + std::to_string(i), connector);
    }
    if (!drm_fd_.is_valid()) {
      FML_LOG(ERROR) << "No connected screen found";
      return false;
    }
  }

  uint64_t value = 0;
  monotonic_timestamps_ =
      drmGetCap(drm_fd_.get(), DRM_CAP_TIMESTAMP_MONOTONIC, &value) == 0 &&
      value != 0;
  supports_modifiers_ =
      drmGetCap(drm_fd_.get(), DRM_CAP_ADDFB2_MODIFIERS, &value) == 0 &&
      value != 0;
  FML_LOG(INFO) << "Using " << connector_name_ << " at " << mode_.hdisplay
                << "x" << mode_.vdisplay << "@" << mode_.vrefresh
                << "Hz, scale " << scale_;

  clipboard_ = std::make_unique<KmsClipboard>();

  // Keyboard layouts are taken from the XKB_DEFAULT_* environment variables.
  std::unique_ptr<XkbKeyboard> keyboard =
      XkbKeyboard::CreateFromNames("", "", "", "", "");
  if (keyboard == nullptr) {
    FML_LOG(WARNING) << "Failed to load keymap, keyboard input is disabled";
  }
  seat_ = std::make_unique<KmsSeat>(event_loop_, std::move(keyboard));
  seat_->SetScreen(mode_.hdisplay, mode_.vdisplay, scale_);
  seat_->SetActive(session_->active());
  seat_->set_switch_vt_callback(
      [this](uint32_t vt) { session_->SwitchVt(vt); });
  seat_->set_pointer_callback([this]() { UpdateCursor(); });
  seat_->set_leds_callback([this](const KeyModifiers& modifiers) {
    if (input_ != nullptr) {
      input_->SetLeds(modifiers.caps_lock, modifiers.num_lock,
                      modifiers.scroll_lock);
    }
  });

  input_ = KmsInput::Create(event_loop_, session_.get(), seat_.get());
  if (input_ == nullptr) {
    FML_LOG(WARNING) << "Input devices are unavailable";
  } else {
    input_->SetScreenSize(mode_.hdisplay, mode_.vdisplay);
  }

  session_->set_active_callback(
      [this](bool active) { OnActiveChanged(active); });

  CreateCursor();
  UpdateCursor();
  return true;
}

bool KmsDisplay::OpenDevice(const std::string& path,
                            const char* connector_name) {
  fml::UniqueFD fd = session_->OpenDevice(path, O_RDWR | O_NONBLOCK);
  if (!fd.is_valid()) {
    if (errno != ENOENT) {
      FML_LOG(WARNING) << "Failed to open " << path << ": " << strerror(errno);
    }
    return false;
  }

  drmModeRes* resources = drmModeGetResources(fd.get());
  if (resources == nullptr) {
    session_->CloseDevice(fd.release());
    return false;
  }
  // Prefer a screen that is on (e.g. showing the console), as other
  // connected screens may be off. A laptop's screen isn't used while the lid
  // is closed, unless there is no other screen.
  drmModeConnector* chosen = nullptr;
  int chosen_score = -1;
  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector =
        drmModeGetConnector(fd.get(), resources->connectors[i]);
    if (connector == nullptr) {
      continue;
    }
    bool matches = connector_name != nullptr
                       ? ConnectorName(connector) == connector_name
                       : connector->connection == DRM_MODE_CONNECTED;
    int score = 0;
    if (!(session_->lid_closed() && IsInternal(connector))) {
      score += 2;
    }
    if (CurrentCrtc(fd.get(), connector) != 0) {
      score += 1;
    }
    if (matches && connector->count_modes > 0 && score > chosen_score) {
      uint32_t crtc = FindCrtc(fd.get(), resources, connector);
      if (crtc != 0) {
        if (chosen != nullptr) {
          drmModeFreeConnector(chosen);
        }
        chosen = connector;
        chosen_score = score;
        crtc_id_ = crtc;
        continue;
      }
    }
    drmModeFreeConnector(connector);
  }
  drmModeFreeResources(resources);
  if (chosen == nullptr) {
    if (connector_name != nullptr) {
      FML_LOG(WARNING) << "No usable connector " << connector_name << " on "
                       << path;
    }
    session_->CloseDevice(fd.release());
    return false;
  }

  // Without logind the device can only be used if no display server is
  // using it.
  if (!session_->uses_logind() && drmIsMaster(fd.get()) == 0) {
    FML_LOG(ERROR) << path << " is in use by another display server";
    drmModeFreeConnector(chosen);
    session_->CloseDevice(fd.release());
    return false;
  }

  connector_id_ = chosen->connector_id;
  connector_name_ = ConnectorName(chosen);
  mode_ = *PreferredMode(chosen);
  ChooseScale(chosen);
  drmModeFreeConnector(chosen);

  struct stat info;
  if (fstat(fd.get(), &info) == 0) {
    device_ = info.st_rdev;
  }
  saved_crtc_ = drmModeGetCrtc(fd.get(), crtc_id_);
  drm_fd_ = std::move(fd);
  return true;
}

void KmsDisplay::ChooseScale(const drmModeConnector* connector) {
  const char* scale = getenv("FLUTTER_KMS_SCALE");
  if (scale != nullptr && scale[0] != '\0') {
    double value = strtod(scale, nullptr);
    if (value > 0) {
      scale_ = value;
      return;
    }
    FML_LOG(WARNING) << "Invalid FLUTTER_KMS_SCALE \"" << scale << "\"";
  }
  // Use a scale of 2 on high density screens, as other environments do.
  scale_ = 1.0;
  if (connector->mmWidth > 0 && mode_.vdisplay >= 1200) {
    double dpi = mode_.hdisplay / (connector->mmWidth / 25.4);
    if (dpi > 192) {
      scale_ = 2.0;
    }
  }
}

void KmsDisplay::RestoreCrtc() {
  if (saved_crtc_ == nullptr || !active()) {
    return;
  }
  if (saved_crtc_->mode_valid) {
    drmModeSetCrtc(drm_fd_.get(), saved_crtc_->crtc_id, saved_crtc_->buffer_id,
                   saved_crtc_->x, saved_crtc_->y, &connector_id_, 1,
                   &saved_crtc_->mode);
  } else {
    drmModeSetCrtc(drm_fd_.get(), saved_crtc_->crtc_id, 0, 0, 0, nullptr, 0,
                   nullptr);
  }
}

bool KmsDisplay::active() const {
  return session_->active();
}

uint64_t KmsDisplay::refresh_ns() const {
  if (mode_.htotal == 0 || mode_.vtotal == 0 || mode_.clock == 0) {
    return 0;
  }
  // The clock is in kHz.
  return static_cast<uint64_t>(mode_.htotal) * mode_.vtotal * 1000000 /
         mode_.clock;
}

uint64_t KmsDisplay::FlipTimeNs(unsigned int sec, unsigned int usec) const {
  if (monotonic_timestamps_) {
    return static_cast<uint64_t>(sec) * 1000000000 +
           static_cast<uint64_t>(usec) * 1000;
  }
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void KmsDisplay::Dispatch(bool readable) {
  if (!readable) {
    return;
  }
  drmEventContext context = {};
  context.version = 2;
  context.page_flip_handler = [](int fd, unsigned int frame, unsigned int sec,
                                 unsigned int usec, void* user_data) {
    auto* self = static_cast<KmsDisplay*>(user_data);
    if (self->window_ != nullptr) {
      self->window_->OnPageFlip(self->FlipTimeNs(sec, usec));
    }
  };
  // The window requests page flips with the display as the user data.
  drmHandleEvent(drm_fd_.get(), &context);
}

Clipboard* KmsDisplay::clipboard() const {
  return clipboard_.get();
}

std::vector<Display::Output> KmsDisplay::GetOutputs() const {
  Output output;
  output.id = connector_id_;
  output.width = mode_.hdisplay;
  output.height = mode_.vdisplay;
  uint64_t refresh = refresh_ns();
  output.refresh_rate = refresh > 0 ? 1e9 / refresh : mode_.vrefresh;
  output.pixel_ratio = scale_;
  return {output};
}

std::unique_ptr<Window> KmsDisplay::CreateWindow(
    DecorationRenderer* decoration_renderer,
    Window::Delegate* delegate,
    const Window::Params& params) {
  if (params.role == Window::Role::kPopup) {
    FML_LOG(ERROR) << "Popup windows are not supported with KMS";
    return nullptr;
  }
  if (window_ != nullptr) {
    FML_LOG(ERROR) << "Only one window is supported with KMS";
    return nullptr;
  }
  auto window = std::make_unique<KmsWindow>(this, delegate, params);
  window_ = window.get();
  return window;
}

void KmsDisplay::RemoveWindow(KmsWindow* window) {
  if (window_ == window) {
    window_ = nullptr;
  }
}

void KmsDisplay::OnActiveChanged(bool active) {
  if (!active) {
    cursor_shown_ = false;
    seat_->SetActive(false);
    if (input_ != nullptr) {
      input_->Suspend();
    }
    if (window_ != nullptr) {
      window_->Suspend();
    }
    return;
  }
  if (window_ != nullptr) {
    window_->Resume();
  }
  if (input_ != nullptr) {
    input_->Resume();
  }
  seat_->SetActive(true);
  UpdateCursor();
}

void KmsDisplay::SetCursor(uint32_t shape) {
  cursor_hidden_ = shape == kHiddenCursorShape;
  UpdateCursor();
}

void KmsDisplay::CreateCursor() {
  uint64_t width = 64;
  uint64_t height = 64;
  drmGetCap(drm_fd_.get(), DRM_CAP_CURSOR_WIDTH, &width);
  drmGetCap(drm_fd_.get(), DRM_CAP_CURSOR_HEIGHT, &height);

  drm_mode_create_dumb create = {};
  create.width = width;
  create.height = height;
  create.bpp = 32;
  if (drmIoctl(drm_fd_.get(), DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
    FML_LOG(WARNING) << "Failed to create cursor buffer";
    return;
  }
  drm_mode_map_dumb map = {};
  map.handle = create.handle;
  void* data = MAP_FAILED;
  if (drmIoctl(drm_fd_.get(), DRM_IOCTL_MODE_MAP_DUMB, &map) == 0) {
    data = mmap(nullptr, create.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                drm_fd_.get(), map.offset);
  }
  if (data == MAP_FAILED) {
    FML_LOG(WARNING) << "Failed to map cursor buffer";
    drm_mode_destroy_dumb destroy = {.handle = create.handle};
    drmIoctl(drm_fd_.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    return;
  }
  memset(data, 0, create.size);
  uint32_t scale = std::max(1, static_cast<int>(std::lround(scale_)));
  DrawArrow(static_cast<uint32_t*>(data), create.pitch, create.width,
            create.height, scale);
  munmap(data, create.size);

  cursor_handle_ = create.handle;
  cursor_width_ = create.width;
  cursor_height_ = create.height;
}

void KmsDisplay::RefreshCursor() {
  cursor_shown_ = false;
  UpdateCursor();
}

void KmsDisplay::UpdateCursor() {
  bool show = cursor_handle_ != 0 && active() && seat_->has_pointer() &&
              !cursor_hidden_;
  if (show != cursor_shown_) {
    // The image is set with its tip at the top left, so it doesn't need a
    // hotspot.
    if (drmModeSetCursor(drm_fd_.get(), crtc_id_, show ? cursor_handle_ : 0,
                         show ? cursor_width_ : 0,
                         show ? cursor_height_ : 0) != 0 &&
        show) {
      return;
    }
    cursor_shown_ = show;
  }
  if (cursor_shown_) {
    drmModeMoveCursor(drm_fd_.get(), crtc_id_,
                      static_cast<int>(seat_->pointer_x()),
                      static_cast<int>(seat_->pointer_y()));
  }
}

}  // namespace flutter
