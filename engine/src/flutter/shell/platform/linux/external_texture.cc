// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/external_texture.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cstring>
#include <utility>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

constexpr size_t kBytesPerPixel = 4;

// Runs |release| if set.
void Release(const std::function<void()>& release) {
  if (release) {
    release();
  }
}

// Sets the sampling of the texture bound to |target|. Textures are drawn
// scaled, so they need linear filtering without mipmaps, which is also the
// only filtering external textures support.
void SetTextureParameters(GLenum target) {
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// A dmabuf frame imported into GL, and freed when the engine is done with it.
struct ImportedFrame {
  EGLManager* egl;
  EGLImageKHR image = EGL_NO_IMAGE_KHR;
  GLuint texture = 0;
  std::function<void()> release;

  ~ImportedFrame() {
    if (texture != 0) {
      // The engine may free frames without a context current, e.g. when it
      // shuts down. The contexts share textures, so any free one can be used.
      bool made_current = false;
      if (!egl->IsContextCurrent()) {
        made_current = egl->MakeResourceContextCurrent() ||
                       egl->MakeRenderContextCurrent();
      }
      if (egl->IsContextCurrent()) {
        glDeleteTextures(1, &texture);
      } else {
        FML_LOG(ERROR) << "Leaking external texture, no GL context available";
      }
      if (made_current) {
        egl->ClearCurrent();
      }
    }
    if (image != EGL_NO_IMAGE_KHR) {
      egl->gpu()->DestroyImage(egl->gpu()->display(), image);
    }
    Release(release);
  }
};

// Returns the EGL attributes for the YUV conversion of |frame|.
std::vector<EGLint> GetYuvAttributes(const DmabufFrame& frame) {
  std::vector<EGLint> attributes;
  switch (frame.color_space) {
    case YuvColorSpace::kDefault:
      break;
    case YuvColorSpace::kBt601:
      attributes.insert(attributes.end(),
                        {EGL_YUV_COLOR_SPACE_HINT_EXT, EGL_ITU_REC601_EXT});
      break;
    case YuvColorSpace::kBt709:
      attributes.insert(attributes.end(),
                        {EGL_YUV_COLOR_SPACE_HINT_EXT, EGL_ITU_REC709_EXT});
      break;
    case YuvColorSpace::kBt2020:
      attributes.insert(attributes.end(),
                        {EGL_YUV_COLOR_SPACE_HINT_EXT, EGL_ITU_REC2020_EXT});
      break;
  }
  switch (frame.range) {
    case YuvRange::kDefault:
      break;
    case YuvRange::kNarrow:
      attributes.insert(attributes.end(),
                        {EGL_SAMPLE_RANGE_HINT_EXT, EGL_YUV_NARROW_RANGE_EXT});
      break;
    case YuvRange::kFull:
      attributes.insert(attributes.end(),
                        {EGL_SAMPLE_RANGE_HINT_EXT, EGL_YUV_FULL_RANGE_EXT});
      break;
  }
  attributes.push_back(EGL_NONE);
  return attributes;
}

}  // namespace

PixelBufferTexture::PixelBufferTexture(EGLManager* egl, Callback callback)
    : egl_(egl), callback_(std::move(callback)) {}

PixelBufferTexture::~PixelBufferTexture() {
  // The texture should have been freed on the raster thread.
  if (texture_ != 0 && egl_->IsContextCurrent()) {
    glDeleteTextures(1, &texture_);
  }
}

bool PixelBufferTexture::Populate(size_t width,
                                  size_t height,
                                  FlutterOpenGLTexture* texture) {
  std::optional<PixelBufferFrame> frame = callback_(width, height);
  if (!frame.has_value()) {
    return false;
  }
  bool uploaded = Upload(*frame);
  Release(frame->release);
  if (!uploaded) {
    return false;
  }

  texture->target = GL_TEXTURE_2D;
  texture->name = texture_;
  texture->format = GL_RGBA8_OES;
  texture->width = texture_width_;
  texture->height = texture_height_;
  // The texture is reused for later frames, so the engine doesn't own it.
  texture->destruction_callback = nullptr;
  texture->user_data = nullptr;
  return true;
}

void PixelBufferTexture::ReleaseGLResources() {
  if (texture_ != 0) {
    glDeleteTextures(1, &texture_);
    texture_ = 0;
  }
}

bool PixelBufferTexture::Upload(const PixelBufferFrame& frame) {
  if (frame.pixels == nullptr || frame.width == 0 || frame.height == 0) {
    return false;
  }
  size_t packed_row_bytes = frame.width * kBytesPerPixel;
  size_t row_bytes = frame.row_bytes != 0 ? frame.row_bytes : packed_row_bytes;
  if (row_bytes < packed_row_bytes) {
    FML_LOG(ERROR) << "Pixel buffer row_bytes " << row_bytes
                   << " is less than its width";
    return false;
  }

  // Rows that aren't tightly packed are read in place if possible, otherwise
  // they are packed first.
  const uint8_t* pixels = frame.pixels;
  std::vector<uint8_t> packed;
  GLint row_length = 0;
  if (row_bytes != packed_row_bytes) {
    if (egl_->supports_unpack_row_length() && row_bytes % kBytesPerPixel == 0) {
      row_length = static_cast<GLint>(row_bytes / kBytesPerPixel);
    } else {
      packed.resize(packed_row_bytes * frame.height);
      for (size_t y = 0; y < frame.height; y++) {
        memcpy(packed.data() + y * packed_row_bytes,
               frame.pixels + y * row_bytes, packed_row_bytes);
      }
      pixels = packed.data();
    }
  }

  bool resized =
      frame.width != texture_width_ || frame.height != texture_height_;
  if (texture_ == 0) {
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    SetTextureParameters(GL_TEXTURE_2D);
    resized = true;
  } else {
    glBindTexture(GL_TEXTURE_2D, texture_);
  }
  if (row_length != 0) {
    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, row_length);
  }
  if (resized) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame.width, frame.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    texture_width_ = frame.width;
    texture_height_ = frame.height;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_RGBA,
                    GL_UNSIGNED_BYTE, pixels);
  }
  if (row_length != 0) {
    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

DmabufTexture::DmabufTexture(EGLManager* egl, Callback callback)
    : egl_(egl), callback_(std::move(callback)) {}

DmabufTexture::~DmabufTexture() = default;

bool DmabufTexture::Populate(size_t width,
                             size_t height,
                             FlutterOpenGLTexture* texture) {
  std::optional<DmabufFrame> frame = callback_(width, height);
  if (!frame.has_value()) {
    return false;
  }

  // Released if the frame isn't passed to the engine.
  auto imported = std::make_unique<ImportedFrame>();
  imported->egl = egl_;
  imported->release = std::move(frame->release);

  GpuDevice* gpu = egl_->gpu();
  if (!gpu->supports_dmabuf_import()) {
    FML_LOG(ERROR) << "Unable to show dmabuf texture, the GPU doesn't support "
                      "importing dmabufs";
    return false;
  }
  if (frame->width == 0 || frame->height == 0 || frame->planes.empty() ||
      frame->planes.size() > GpuDevice::kMaxDmabufPlanes) {
    FML_LOG(ERROR) << "Invalid dmabuf texture frame";
    return false;
  }

  // Formats the GPU can't sample directly (e.g. YUV) are sampled as external
  // textures, which convert them to RGB as they are read.
  GLenum target = gpu->IsExternalOnly(frame->format, frame->modifier)
                      ? GL_TEXTURE_EXTERNAL_OES
                      : GL_TEXTURE_2D;
  if (target == GL_TEXTURE_EXTERNAL_OES &&
      !egl_->supports_external_textures()) {
    FML_LOG(ERROR) << "Unable to show dmabuf texture with format 0x" << std::hex
                   << frame->format
                   << ", GL_OES_EGL_image_external is not supported";
    return false;
  }

  std::vector<GpuDevice::DmabufPlane> planes;
  planes.reserve(frame->planes.size());
  for (const DmabufFrame::Plane& plane : frame->planes) {
    planes.push_back({plane.fd, plane.offset, plane.stride});
  }
  std::vector<EGLint> yuv_attributes = GetYuvAttributes(*frame);
  imported->image = gpu->ImportDmabuf(
      frame->width, frame->height, frame->format, frame->modifier,
      planes.data(), planes.size(), yuv_attributes.data());
  if (imported->image == EGL_NO_IMAGE_KHR) {
    FML_LOG(ERROR) << "Failed to import dmabuf texture with format 0x"
                   << std::hex << frame->format << " and modifier 0x"
                   << frame->modifier << ": 0x" << eglGetError();
    return false;
  }

  glGenTextures(1, &imported->texture);
  glBindTexture(target, imported->texture);
  gpu->ImageTargetTexture2D(target, imported->image);
  SetTextureParameters(target);
  glBindTexture(target, 0);

  texture->target = target;
  texture->name = imported->texture;
  texture->format = GL_RGBA8_OES;
  texture->width = frame->width;
  texture->height = frame->height;
  texture->user_data = imported.release();
  texture->destruction_callback = [](void* user_data) {
    delete static_cast<ImportedFrame*>(user_data);
  };
  return true;
}

}  // namespace flutter
