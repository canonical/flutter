// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_BUFFER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_BUFFER_H_

#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/dmabuf_image.h"
#include "flutter/shell/platform/linux/render_buffer.h"
#include "flutter/shell/platform/linux/vulkan_device.h"

namespace flutter {

// A buffer that Flutter renders into on the GPU, which is not shared with the
// display server. Used by display backends that copy the contents into their
// own buffers when presenting, such as X11.
class TextureBuffer : public RenderBuffer {
 public:
  // Allocates a buffer of the given size in pixels. If |depth_stencil| is
  // true a depth/stencil attachment is added to the framebuffer.
  //
  // If |vulkan| is set, the texture is in a dmabuf that is also imported into
  // Vulkan.
  //
  // Must be called with the render context current.
  static std::unique_ptr<TextureBuffer> Create(EGLManager* egl,
                                               int width,
                                               int height,
                                               bool depth_stencil,
                                               VulkanDevice* vulkan = nullptr);

  ~TextureBuffer() override;

  // The GL texture holding the rendered contents.
  GLuint texture() const { return image_ ? image_->texture() : texture_; }

  // The EGL manager the buffer was allocated with.
  EGLManager* egl_manager() const { return egl(); }

  // |RenderBuffer|
  uint64_t modifier() const override;

  // |RenderBuffer|
  bool PrepareToPresent() override;

  // |RenderBuffer|
  VkImage vulkan_image() const override {
    return image_ ? image_->vulkan_image() : VK_NULL_HANDLE;
  }

  // |RenderBuffer|
  int dmabuf_fd() const override { return image_ ? image_->fd() : -1; }

 private:
  TextureBuffer(EGLManager* egl, int width, int height);

  bool Initialize(bool depth_stencil, VulkanDevice* vulkan);

  GLuint texture_ = 0;
  std::unique_ptr<DmabufImage> image_;

  FML_DISALLOW_COPY_AND_ASSIGN(TextureBuffer);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_BUFFER_H_
