// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_TITLE_RENDERER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_TITLE_RENDERER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"

namespace txt {
class FontCollection;
}

namespace flutter {

// An 8-bit coverage image, with rows packed without padding.
struct AlphaImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;
};

// Rasterizes window titles with the engine's text stack and system fonts, so
// they match the default text in Flutter.
//
// ICU must have been initialized before titles are rendered.
class TitleRenderer {
 public:
  // Title text style in logical pixels, following Material 3 title small.
  static constexpr double kFontSize = 14;
  static constexpr int kFontWeight = 500;
  static constexpr double kLetterSpacing = 0.1;

  TitleRenderer();
  ~TitleRenderer();

  // Renders |text| on a single line at |scale|, ellipsized to fit within
  // |max_width| pixels. Returns nullptr if there is nothing to draw.
  std::shared_ptr<const AlphaImage> Render(const std::string& text,
                                           double scale,
                                           int max_width);

 private:
  std::shared_ptr<txt::FontCollection> font_collection_;

  FML_DISALLOW_COPY_AND_ASSIGN(TitleRenderer);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_TITLE_RENDERER_H_
