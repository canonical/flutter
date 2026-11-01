// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_EXTERNAL_TEXTURE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_EXTERNAL_TEXTURE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/egl_manager.h"

namespace flutter {

// A texture whose contents are provided by a plugin, shown in Flutter with a
// Texture widget.
//
// Textures are created and destroyed on the platform thread. The other
// methods are called on the raster thread.
class ExternalTexture {
 public:
  virtual ~ExternalTexture() = default;

  // Gets the latest frame for display at |width|x|height| physical pixels.
  // Called with an engine EGL context current. Returns false if there is no
  // frame.
  virtual bool Populate(size_t width,
                        size_t height,
                        FlutterOpenGLTexture* texture) = 0;

  // Frees the GL objects used by the texture. Called after it has been
  // unregistered from the engine, with an engine EGL context current.
  virtual void ReleaseGLResources() {}

 protected:
  ExternalTexture() = default;

  FML_DISALLOW_COPY_AND_ASSIGN(ExternalTexture);
};

// A frame of pixels in CPU memory.
struct PixelBufferFrame {
  // Premultiplied RGBA pixels, 8 bits per channel.
  const uint8_t* pixels = nullptr;
  size_t width = 0;
  size_t height = 0;

  // The number of bytes from the start of one row to the next, or zero if
  // rows are tightly packed.
  size_t row_bytes = 0;

  // Called once the pixels have been copied. May be empty.
  std::function<void()> release;
};

// A texture that copies frames from CPU memory to the GPU.
class PixelBufferTexture : public ExternalTexture {
 public:
  using Callback =
      std::function<std::optional<PixelBufferFrame>(size_t width,
                                                    size_t height)>;

  PixelBufferTexture(EGLManager* egl, Callback callback);
  ~PixelBufferTexture() override;

  // |ExternalTexture|
  bool Populate(size_t width,
                size_t height,
                FlutterOpenGLTexture* texture) override;

  // |ExternalTexture|
  void ReleaseGLResources() override;

 private:
  // Uploads |frame| into texture_.
  bool Upload(const PixelBufferFrame& frame);

  EGLManager* egl_;
  Callback callback_;
  uint32_t texture_ = 0;
  size_t texture_width_ = 0;
  size_t texture_height_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(PixelBufferTexture);
};

// The YUV to RGB conversion of a frame.
enum class YuvColorSpace {
  kDefault,
  kBt601,
  kBt709,
  kBt2020,
};

// The range of YUV values in a frame.
enum class YuvRange {
  kDefault,
  // Y from 16-235, UV from 16-240 (for 8 bit values).
  kNarrow,
  // The full range of values.
  kFull,
};

// A frame in a GPU buffer, shared as a dmabuf.
struct DmabufFrame {
  struct Plane {
    int fd = -1;
    uint32_t offset = 0;
    uint32_t stride = 0;
  };

  uint32_t width = 0;
  uint32_t height = 0;

  // The DRM fourcc format, e.g. DRM_FORMAT_ARGB8888 or DRM_FORMAT_NV12.
  uint32_t format = 0;

  // The DRM format modifier, or DRM_FORMAT_MOD_INVALID if the layout is
  // implied by the driver.
  uint64_t modifier = 0;

  std::vector<Plane> planes;

  // Used for YUV formats.
  YuvColorSpace color_space = YuvColorSpace::kDefault;
  YuvRange range = YuvRange::kDefault;

  // Called once the GPU is no longer using the buffer. May be empty.
  std::function<void()> release;
};

// Returns the EGL attributes for the YUV conversion of |frame|, terminated by
// EGL_NONE, for importing it with GpuDevice::ImportDmabuf.
std::vector<EGLint> GetYuvAttributes(const DmabufFrame& frame);

// A texture that shows frames from GPU buffers without copying them.
class DmabufTexture : public ExternalTexture {
 public:
  using Callback =
      std::function<std::optional<DmabufFrame>(size_t width, size_t height)>;

  DmabufTexture(EGLManager* egl, Callback callback);
  ~DmabufTexture() override;

  // |ExternalTexture|
  bool Populate(size_t width,
                size_t height,
                FlutterOpenGLTexture* texture) override;

 private:
  EGLManager* egl_;
  Callback callback_;

  FML_DISALLOW_COPY_AND_ASSIGN(DmabufTexture);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_EXTERNAL_TEXTURE_H_
