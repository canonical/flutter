// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_ATSPI_TEXT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_ATSPI_TEXT_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace flutter {

// Text helpers for the AT-SPI Text interface, which uses offsets in Unicode
// code points ("characters"). Text is stored as UTF-16, as Flutter uses
// UTF-16 offsets.
namespace atspi_text {

// The number of characters in |text|.
int32_t CharacterCount(std::u16string_view text);

// Converts between character offsets and UTF-16 offsets, clamping to the
// text.
int32_t ToUtf16Offset(std::u16string_view text, int32_t offset);
int32_t ToCharacterOffset(std::u16string_view text, int32_t utf16_offset);

std::string ToUtf8(std::u16string_view text);
std::u16string FromUtf8(std::string_view text);

// The characters from |start| to |end| as UTF-8. An |end| of -1 is the end
// of the text.
std::string Substring(std::u16string_view text, int32_t start, int32_t end);

// AtspiTextGranularity.
enum class Granularity : uint32_t {
  kChar = 0,
  kWord = 1,
  kSentence = 2,
  kLine = 3,
  kParagraph = 4,
};

// AtspiTextBoundaryType.
enum class Boundary : uint32_t {
  kChar = 0,
  kWordStart = 1,
  kWordEnd = 2,
  kSentenceStart = 3,
  kSentenceEnd = 4,
  kLineStart = 5,
  kLineEnd = 6,
};

// A range of characters.
struct Range {
  int32_t start = 0;
  int32_t end = 0;

  bool operator==(const Range& other) const = default;
};

// The unit of |granularity| containing |offset|, for GetStringAtOffset.
Range GetStringAtOffset(std::u16string_view text,
                        int32_t offset,
                        Granularity granularity);

enum class Position { kBefore, kAt, kAfter };

// The range between |boundary|s before, at or after |offset|, for
// GetTextBeforeOffset, GetTextAtOffset and GetTextAfterOffset.
Range GetTextAtOffset(std::u16string_view text,
                      int32_t offset,
                      Boundary boundary,
                      Position position);

}  // namespace atspi_text

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_ATSPI_TEXT_H_
