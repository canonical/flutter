// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/texture_registrar.h"

#include <deque>
#include <set>
#include <vector>

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

class FakeDelegate : public TextureRegistrar::Delegate {
 public:
  bool IsEngineRunning() const override { return running; }

  bool RegisterExternalTexture(int64_t texture_id) override {
    if (fail_register) {
      return false;
    }
    registered.insert(texture_id);
    return true;
  }

  void UnregisterExternalTexture(int64_t texture_id) override {
    registered.erase(texture_id);
  }

  void MarkExternalTextureFrameAvailable(int64_t texture_id) override {
    marked.push_back(texture_id);
  }

  bool PostRasterTask(std::function<void()> task) override {
    if (!running) {
      return false;
    }
    raster_tasks.push_back(std::move(task));
    return true;
  }

  void PostPlatformTask(std::function<void()> task) override {
    platform_tasks.push_back(std::move(task));
  }

  void RunWithContext(const std::function<void()>& task) override {
    in_context = true;
    task();
    in_context = false;
  }

  void RunRasterTasks() {
    while (!raster_tasks.empty()) {
      std::function<void()> task = std::move(raster_tasks.front());
      raster_tasks.pop_front();
      in_context = true;
      task();
      in_context = false;
    }
  }

  void RunPlatformTasks() {
    while (!platform_tasks.empty()) {
      std::function<void()> task = std::move(platform_tasks.front());
      platform_tasks.pop_front();
      task();
    }
  }

  bool running = false;
  bool fail_register = false;
  bool in_context = false;
  std::set<int64_t> registered;
  std::vector<int64_t> marked;
  std::deque<std::function<void()>> raster_tasks;
  std::deque<std::function<void()>> platform_tasks;
};

// Records what happens to a texture.
struct TextureLog {
  int populated = 0;
  int released = 0;
  bool released_in_context = false;
  bool destroyed = false;
};

class FakeTexture : public ExternalTexture {
 public:
  FakeTexture(FakeDelegate* delegate, TextureLog* log)
      : delegate_(delegate), log_(log) {}
  ~FakeTexture() override { log_->destroyed = true; }

  bool Populate(size_t width,
                size_t height,
                FlutterOpenGLTexture* texture) override {
    log_->populated++;
    texture->width = width;
    texture->height = height;
    return true;
  }

  void ReleaseGLResources() override {
    log_->released++;
    log_->released_in_context = delegate_->in_context;
  }

 private:
  FakeDelegate* delegate_;
  TextureLog* log_;
};

}  // namespace

TEST(TextureRegistrarTest, RegistersWithRunningEngine) {
  FakeDelegate delegate;
  delegate.running = true;
  TextureRegistrar registrar(&delegate);
  TextureLog log;
  int64_t id =
      registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));
  EXPECT_GT(id, 0);
  EXPECT_EQ(delegate.registered.count(id), 1u);

  int64_t id2 =
      registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));
  EXPECT_NE(id, id2);
}

TEST(TextureRegistrarTest, RegistersPendingTexturesWhenEngineStarts) {
  FakeDelegate delegate;
  TextureRegistrar registrar(&delegate);
  TextureLog log;
  int64_t id =
      registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));
  EXPECT_GT(id, 0);
  EXPECT_TRUE(delegate.registered.empty());

  delegate.running = true;
  registrar.OnEngineStarted();
  EXPECT_EQ(delegate.registered.count(id), 1u);
}

TEST(TextureRegistrarTest, ReturnsErrorIfEngineFailsToRegister) {
  FakeDelegate delegate;
  delegate.running = true;
  delegate.fail_register = true;
  TextureRegistrar registrar(&delegate);
  TextureLog log;
  int64_t id =
      registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));
  EXPECT_EQ(id, -1);
  EXPECT_TRUE(log.destroyed);
}

TEST(TextureRegistrarTest, PopulatesTexture) {
  FakeDelegate delegate;
  delegate.running = true;
  TextureRegistrar registrar(&delegate);
  TextureLog log;
  int64_t id =
      registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));

  FlutterOpenGLTexture texture = {};
  EXPECT_TRUE(registrar.PopulateTexture(id, 10, 20, &texture));
  EXPECT_EQ(log.populated, 1);
  EXPECT_EQ(texture.width, 10u);
  EXPECT_EQ(texture.height, 20u);

  EXPECT_FALSE(registrar.PopulateTexture(id + 1, 10, 20, &texture));
}

TEST(TextureRegistrarTest, MarksFrameAvailableOnPlatformThread) {
  FakeDelegate delegate;
  delegate.running = true;
  TextureRegistrar registrar(&delegate);
  TextureLog log;
  int64_t id =
      registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));

  EXPECT_TRUE(registrar.MarkTextureFrameAvailable(id));
  EXPECT_FALSE(registrar.MarkTextureFrameAvailable(id + 1));
  EXPECT_TRUE(delegate.marked.empty());
  delegate.RunPlatformTasks();
  EXPECT_EQ(delegate.marked, std::vector<int64_t>{id});
}

TEST(TextureRegistrarTest, DoesNotMarkUnregisteredTexture) {
  FakeDelegate delegate;
  delegate.running = true;
  TextureRegistrar registrar(&delegate);
  TextureLog log;
  int64_t id =
      registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));

  EXPECT_TRUE(registrar.MarkTextureFrameAvailable(id));
  registrar.UnregisterTexture(id, nullptr);
  delegate.RunPlatformTasks();
  EXPECT_TRUE(delegate.marked.empty());
}

TEST(TextureRegistrarTest, MarkTaskDoesNotRunAfterRegistrarDestroyed) {
  FakeDelegate delegate;
  delegate.running = true;
  TextureLog log;
  {
    TextureRegistrar registrar(&delegate);
    int64_t id = registrar.RegisterTexture(
        std::make_unique<FakeTexture>(&delegate, &log));
    EXPECT_TRUE(registrar.MarkTextureFrameAvailable(id));
  }
  delegate.RunPlatformTasks();
  EXPECT_TRUE(delegate.marked.empty());
}

TEST(TextureRegistrarTest, UnregisterReleasesOnRasterThread) {
  FakeDelegate delegate;
  delegate.running = true;
  TextureRegistrar registrar(&delegate);
  TextureLog log;
  int64_t id =
      registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));

  bool done = false;
  registrar.UnregisterTexture(id, [&]() {
    EXPECT_TRUE(log.destroyed);
    done = true;
  });
  EXPECT_EQ(delegate.registered.count(id), 0u);
  EXPECT_EQ(log.released, 0);

  FlutterOpenGLTexture texture = {};
  EXPECT_FALSE(registrar.PopulateTexture(id, 10, 20, &texture));

  delegate.RunRasterTasks();
  EXPECT_EQ(log.released, 1);
  EXPECT_TRUE(log.released_in_context);
  EXPECT_FALSE(log.destroyed);
  EXPECT_FALSE(done);

  delegate.RunPlatformTasks();
  EXPECT_TRUE(log.destroyed);
  EXPECT_TRUE(done);
}

TEST(TextureRegistrarTest, UnregisterWhenEngineNotRunning) {
  FakeDelegate delegate;
  TextureRegistrar registrar(&delegate);
  TextureLog log;
  int64_t id =
      registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));

  bool done = false;
  registrar.UnregisterTexture(id, [&]() { done = true; });
  EXPECT_EQ(log.released, 1);
  EXPECT_TRUE(log.released_in_context);
  EXPECT_FALSE(done);

  delegate.RunPlatformTasks();
  EXPECT_TRUE(log.destroyed);
  EXPECT_TRUE(done);
}

TEST(TextureRegistrarTest, UnregisterUnknownTextureCallsCallback) {
  FakeDelegate delegate;
  delegate.running = true;
  TextureRegistrar registrar(&delegate);
  bool done = false;
  registrar.UnregisterTexture(42, [&]() { done = true; });
  EXPECT_FALSE(done);
  delegate.RunPlatformTasks();
  EXPECT_TRUE(done);
}

TEST(TextureRegistrarTest, DestructorReleasesTextures) {
  FakeDelegate delegate;
  delegate.running = true;
  TextureLog log;
  {
    TextureRegistrar registrar(&delegate);
    registrar.RegisterTexture(std::make_unique<FakeTexture>(&delegate, &log));
    delegate.running = false;
  }
  EXPECT_EQ(log.released, 1);
  EXPECT_TRUE(log.released_in_context);
  EXPECT_TRUE(log.destroyed);
}

TEST(TextureRegistrarTest, DestructorReleasesTexturesBeingUnregistered) {
  FakeDelegate delegate;
  delegate.running = true;
  TextureLog log;
  bool done = false;
  {
    TextureRegistrar registrar(&delegate);
    int64_t id = registrar.RegisterTexture(
        std::make_unique<FakeTexture>(&delegate, &log));
    registrar.UnregisterTexture(id, [&]() { done = true; });
    // The engine stops before the raster task runs.
    delegate.running = false;
    delegate.raster_tasks.clear();
  }
  EXPECT_EQ(log.released, 1);
  EXPECT_TRUE(log.released_in_context);
  EXPECT_TRUE(log.destroyed);
  EXPECT_TRUE(done);
}

}  // namespace testing
}  // namespace flutter
