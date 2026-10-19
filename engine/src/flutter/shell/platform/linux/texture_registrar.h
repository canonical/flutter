// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_REGISTRAR_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_REGISTRAR_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/external_texture.h"

namespace flutter {

// The textures plugins have registered with an engine.
//
// Textures are registered and unregistered on the platform thread. The engine
// gets their frames on the raster thread.
class TextureRegistrar {
 public:
  // The engine the textures are shown in.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Returns true if the engine is running. Textures are only registered
    // with the engine while it is running.
    virtual bool IsEngineRunning() const = 0;

    // Calls the engine's texture functions on the platform thread.
    virtual bool RegisterExternalTexture(int64_t texture_id) = 0;
    virtual void UnregisterExternalTexture(int64_t texture_id) = 0;
    virtual void MarkExternalTextureFrameAvailable(int64_t texture_id) = 0;

    // Runs |task| on the raster thread with an EGL context current. Returns
    // false if it can't be posted because the engine isn't running.
    virtual bool PostRasterTask(std::function<void()> task) = 0;

    // Runs |task| on the platform thread. May be called from any thread, and
    // tasks may run after the engine is destroyed.
    virtual void PostPlatformTask(std::function<void()> task) = 0;

    // Runs |task| on the platform thread with an EGL context current.
    virtual void RunWithContext(const std::function<void()>& task) = 0;
  };

  explicit TextureRegistrar(Delegate* delegate);

  // Must be destroyed after the engine has stopped, so textures are no longer
  // used on the raster thread.
  ~TextureRegistrar();

  // Adds |texture| and returns its ID, or -1 on failure.
  int64_t RegisterTexture(std::unique_ptr<ExternalTexture> texture);

  // Removes the texture with |texture_id|. |callback|, which may be empty, is
  // called on the platform thread once the engine will no longer use the
  // texture. The texture is destroyed before |callback| is called.
  void UnregisterTexture(int64_t texture_id, std::function<void()> callback);

  // Tells the engine the texture with |texture_id| has a new frame. May be
  // called from any thread. Returns false if there is no such texture.
  bool MarkTextureFrameAvailable(int64_t texture_id);

  // Registers the textures added before the engine was running. Called when
  // the engine starts.
  void OnEngineStarted();

  // Gets the frame for the texture with |texture_id|. Called on the raster
  // thread.
  bool PopulateTexture(int64_t texture_id,
                       size_t width,
                       size_t height,
                       FlutterOpenGLTexture* texture);

 private:
  Delegate* delegate_;
  int64_t next_texture_id_ = 1;

  // The registered textures, used on the platform and raster threads.
  std::mutex mutex_;
  std::map<int64_t, std::shared_ptr<ExternalTexture>> textures_;

  // Unregistered textures waiting for their GL objects to be freed on the
  // raster thread.
  struct ReleasingTexture {
    std::shared_ptr<ExternalTexture> texture;
    std::function<void()> callback;
  };
  std::map<int64_t, ReleasingTexture> releasing_textures_;

  // Tasks posted to the platform thread hold a weak reference to this, so
  // they don't run after it is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(TextureRegistrar);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_REGISTRAR_H_
