// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland/title_renderer.h"

#include <algorithm>
#include <filesystem>

#include "flutter/fml/icu_util.h"
#include "flutter/fml/paths.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

// Renders |text|, skipping the test if ICU data or system fonts aren't
// available.
#define RENDER_OR_SKIP(renderer, image, text, scale, max_width) \
  std::shared_ptr<const AlphaImage> image =                     \
      (renderer).Render((text), (scale), (max_width));          \
  if (!(image)) {                                               \
    GTEST_SKIP() << "No system fonts available";                \
  }

bool InitializeICU() {
  std::filesystem::path path =
      std::filesystem::path(fml::paths::GetExecutableDirectoryPath().second) /
      "icudtl.dat";
  if (!std::filesystem::exists(path)) {
    return false;
  }
  fml::icu::InitializeICU(path.string());
  return true;
}

bool HasCoverage(const AlphaImage& image) {
  return std::any_of(image.pixels.begin(), image.pixels.end(),
                     [](uint8_t p) { return p != 0; });
}

class TitleRendererTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!InitializeICU()) {
      GTEST_SKIP() << "ICU data not available";
    }
  }
};

}  // namespace

TEST_F(TitleRendererTest, EmptyTitle) {
  TitleRenderer renderer;
  EXPECT_EQ(renderer.Render("", 1.0, 100), nullptr);
  EXPECT_EQ(renderer.Render("Title", 1.0, 0), nullptr);
}

TEST_F(TitleRendererTest, RendersText) {
  TitleRenderer renderer;
  RENDER_OR_SKIP(renderer, image, "Hello World", 1.0, 1000);
  EXPECT_GT(image->width, 0);
  EXPECT_LT(image->width, 1000);
  EXPECT_GE(image->height, TitleRenderer::kFontSize);
  EXPECT_EQ(image->pixels.size(),
            static_cast<size_t>(image->width) * image->height);
  EXPECT_TRUE(HasCoverage(*image));
}

TEST_F(TitleRendererTest, Ellipsizes) {
  TitleRenderer renderer;
  std::string text(200, 'W');
  RENDER_OR_SKIP(renderer, image, text, 1.0, 100);
  EXPECT_LE(image->width, 100);
  EXPECT_GT(image->width, 50);
  EXPECT_TRUE(HasCoverage(*image));
}

TEST_F(TitleRendererTest, Scales) {
  TitleRenderer renderer;
  RENDER_OR_SKIP(renderer, image1, "Hello World", 1.0, 1000);
  RENDER_OR_SKIP(renderer, image2, "Hello World", 2.0, 1000);
  EXPECT_NEAR(image2->width, image1->width * 2, 4);
  EXPECT_NEAR(image2->height, image1->height * 2, 4);
}

}  // namespace testing
}  // namespace flutter
