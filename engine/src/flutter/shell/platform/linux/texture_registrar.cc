// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/texture_registrar.h"

#include <utility>
#include <vector>

namespace flutter {

TextureRegistrar::TextureRegistrar(Delegate* delegate) : delegate_(delegate) {}

TextureRegistrar::~TextureRegistrar() {
  std::map<int64_t, std::shared_ptr<ExternalTexture>> textures;
  std::map<int64_t, ReleasingTexture> releasing_textures;
  {
    std::scoped_lock lock(mutex_);
    textures.swap(textures_);
    releasing_textures.swap(releasing_textures_);
  }
  if (!textures.empty() || !releasing_textures.empty()) {
    delegate_->RunWithContext([&]() {
      for (auto& [texture_id, texture] : textures) {
        texture->ReleaseGLResources();
      }
      for (auto& [texture_id, releasing] : releasing_textures) {
        releasing.texture->ReleaseGLResources();
      }
    });
  }
  textures.clear();
  for (auto& [texture_id, releasing] : releasing_textures) {
    releasing.texture.reset();
    if (releasing.callback) {
      releasing.callback();
    }
  }
}

int64_t TextureRegistrar::RegisterTexture(
    std::unique_ptr<ExternalTexture> texture) {
  int64_t texture_id = next_texture_id_++;
  {
    std::scoped_lock lock(mutex_);
    textures_[texture_id] = std::move(texture);
  }
  if (delegate_->IsEngineRunning() &&
      !delegate_->RegisterExternalTexture(texture_id)) {
    std::scoped_lock lock(mutex_);
    textures_.erase(texture_id);
    return -1;
  }
  return texture_id;
}

void TextureRegistrar::UnregisterTexture(int64_t texture_id,
                                         std::function<void()> callback) {
  std::shared_ptr<ExternalTexture> texture;
  {
    std::scoped_lock lock(mutex_);
    auto it = textures_.find(texture_id);
    if (it != textures_.end()) {
      texture = std::move(it->second);
      textures_.erase(it);
    }
  }

  // Textures are destroyed on the platform thread, as they hold plugin
  // callbacks. The callback is always called asynchronously, so callers see
  // the same behavior whether or not the texture exists.
  auto finish = [delegate = delegate_](ReleasingTexture releasing) {
    delegate->PostPlatformTask([releasing = std::move(releasing)]() mutable {
      releasing.texture.reset();
      if (releasing.callback) {
        releasing.callback();
      }
    });
  };

  if (!texture) {
    finish({nullptr, std::move(callback)});
    return;
  }

  // The engine may still be drawing the texture on the raster thread, so its
  // GL objects are freed there once the engine has stopped using it. Raster
  // tasks only run while the engine is running, which is during the lifetime
  // of this object.
  if (delegate_->IsEngineRunning()) {
    delegate_->UnregisterExternalTexture(texture_id);
    {
      std::scoped_lock lock(mutex_);
      releasing_textures_[texture_id] = {texture, std::move(callback)};
    }
    texture.reset();
    if (delegate_->PostRasterTask([this, texture_id, finish]() {
          ReleasingTexture releasing;
          {
            std::scoped_lock lock(mutex_);
            auto it = releasing_textures_.find(texture_id);
            if (it == releasing_textures_.end()) {
              return;
            }
            releasing = std::move(it->second);
            releasing_textures_.erase(it);
          }
          releasing.texture->ReleaseGLResources();
          finish(std::move(releasing));
        })) {
      return;
    }

    std::scoped_lock lock(mutex_);
    auto it = releasing_textures_.find(texture_id);
    texture = std::move(it->second.texture);
    callback = std::move(it->second.callback);
    releasing_textures_.erase(it);
  }

  delegate_->RunWithContext([&texture]() { texture->ReleaseGLResources(); });
  finish({std::move(texture), std::move(callback)});
}

bool TextureRegistrar::MarkTextureFrameAvailable(int64_t texture_id) {
  {
    std::scoped_lock lock(mutex_);
    if (textures_.find(texture_id) == textures_.end()) {
      return false;
    }
  }
  delegate_->PostPlatformTask(
      [this, alive = std::weak_ptr<bool>(alive_), texture_id]() {
        if (alive.expired() || !delegate_->IsEngineRunning()) {
          return;
        }
        {
          std::scoped_lock lock(mutex_);
          if (textures_.find(texture_id) == textures_.end()) {
            return;
          }
        }
        delegate_->MarkExternalTextureFrameAvailable(texture_id);
      });
  return true;
}

void TextureRegistrar::OnEngineStarted() {
  std::vector<int64_t> texture_ids;
  {
    std::scoped_lock lock(mutex_);
    for (const auto& [texture_id, texture] : textures_) {
      texture_ids.push_back(texture_id);
    }
  }
  for (int64_t texture_id : texture_ids) {
    delegate_->RegisterExternalTexture(texture_id);
  }
}

bool TextureRegistrar::PopulateTexture(int64_t texture_id,
                                       size_t width,
                                       size_t height,
                                       FlutterOpenGLTexture* texture) {
  std::shared_ptr<ExternalTexture> external_texture;
  {
    std::scoped_lock lock(mutex_);
    auto it = textures_.find(texture_id);
    if (it == textures_.end()) {
      return false;
    }
    external_texture = it->second;
  }
  return external_texture->Populate(width, height, texture);
}

}  // namespace flutter
