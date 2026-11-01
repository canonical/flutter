// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland/title_renderer.h"

#include <algorithm>
#include <cmath>

#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkFontStyle.h"
#include "third_party/skia/modules/skparagraph/include/Paragraph.h"
#include "third_party/skia/modules/skparagraph/include/ParagraphBuilder.h"
#include "third_party/skia/modules/skparagraph/include/ParagraphStyle.h"
#include "third_party/skia/modules/skparagraph/include/TextStyle.h"
#include "third_party/skia/modules/skunicode/include/SkUnicode_icu.h"
#include "txt/font_collection.h"

namespace skt = skia::textlayout;

namespace flutter {

TitleRenderer::TitleRenderer()
    : font_collection_(std::make_shared<txt::FontCollection>()) {
  // The same fonts Flutter uses when no font family is specified.
  font_collection_->SetupDefaultFontManager(0);
}

TitleRenderer::~TitleRenderer() = default;

std::shared_ptr<const AlphaImage> TitleRenderer::Render(const std::string& text,
                                                        double scale,
                                                        int max_width) {
  if (text.empty() || max_width <= 0) {
    return nullptr;
  }

  skt::TextStyle text_style;
  text_style.setColor(SK_ColorBLACK);
  text_style.setFontSize(kFontSize * scale);
  text_style.setLetterSpacing(kLetterSpacing * scale);
  text_style.setFontStyle(SkFontStyle(kFontWeight, SkFontStyle::kNormal_Width,
                                      SkFontStyle::kUpright_Slant));

  skt::ParagraphStyle paragraph_style;
  paragraph_style.setTextStyle(text_style);
  paragraph_style.setMaxLines(1);
  paragraph_style.setEllipsis(u"\u2026");

  std::unique_ptr<skt::ParagraphBuilder> builder = skt::ParagraphBuilder::make(
      paragraph_style, font_collection_->CreateSktFontCollection(),
      SkUnicodes::ICU::Make());
  if (!builder) {
    return nullptr;
  }
  builder->addText(text.data(), text.size());
  std::unique_ptr<skt::Paragraph> paragraph = builder->Build();
  paragraph->layout(max_width);
  // Lay out again at the natural width so the image is tight.
  double width = std::min(
      std::ceil(static_cast<double>(paragraph->getMaxIntrinsicWidth())),
      static_cast<double>(max_width));
  paragraph->layout(width);

  auto image = std::make_shared<AlphaImage>();
  image->width = static_cast<int>(width);
  image->height = static_cast<int>(std::ceil(paragraph->getHeight()));
  if (image->width <= 0 || image->height <= 0) {
    return nullptr;
  }
  image->pixels.resize(static_cast<size_t>(image->width) * image->height);

  SkBitmap bitmap;
  if (!bitmap.installPixels(SkImageInfo::MakeA8(image->width, image->height),
                            image->pixels.data(), image->width)) {
    return nullptr;
  }
  SkCanvas canvas(bitmap);
  paragraph->paint(&canvas, 0, 0);
  return image;
}

}  // namespace flutter
