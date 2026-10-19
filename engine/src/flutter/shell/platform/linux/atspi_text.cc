// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/atspi_text.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "third_party/icu/source/common/unicode/brkiter.h"
#include "third_party/icu/source/common/unicode/uchar.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/common/unicode/utf16.h"

namespace flutter {
namespace atspi_text {

namespace {

icu::UnicodeString ToUnicodeString(std::u16string_view text) {
  return icu::UnicodeString(false, text.data(),
                            static_cast<int32_t>(text.size()));
}

// The UTF-16 offsets of boundaries found by a break iterator of |type|, or
// an empty list if ICU can't provide one.
enum class BreakType { kWordStart, kWordEnd, kSentence };
std::vector<int32_t> IcuBoundaries(std::u16string_view text, BreakType type) {
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::BreakIterator> iterator(
      type == BreakType::kSentence ? icu::BreakIterator::createSentenceInstance(
                                         icu::Locale::getDefault(), status)
                                   : icu::BreakIterator::createWordInstance(
                                         icu::Locale::getDefault(), status));
  if (U_FAILURE(status) || !iterator) {
    return {};
  }
  icu::UnicodeString string = ToUnicodeString(text);
  iterator->setText(string);

  std::vector<int32_t> boundaries;
  int32_t start = iterator->first();
  if (type == BreakType::kSentence) {
    boundaries.push_back(start);
  }
  for (int32_t end = iterator->next(); end != icu::BreakIterator::DONE;
       start = end, end = iterator->next()) {
    if (type == BreakType::kSentence) {
      boundaries.push_back(end);
    } else if (iterator->getRuleStatus() != UBRK_WORD_NONE) {
      boundaries.push_back(type == BreakType::kWordStart ? start : end);
    }
  }
  return boundaries;
}

// Word boundaries without ICU data: words are runs of non-whitespace.
std::vector<int32_t> SimpleWordBoundaries(std::u16string_view text,
                                          bool starts) {
  std::vector<int32_t> boundaries;
  bool in_word = false;
  int32_t length = static_cast<int32_t>(text.size());
  for (int32_t i = 0; i <= length;) {
    UChar32 c = 0;
    int32_t next = i;
    if (i < length) {
      U16_NEXT(text.data(), next, length, c);
    }
    bool is_word = i < length && !u_isUWhiteSpace(c);
    if (is_word != in_word && (is_word == starts)) {
      boundaries.push_back(i);
    }
    in_word = is_word;
    if (i == length) {
      break;
    }
    i = next;
  }
  return boundaries;
}

// The character offsets of |boundary|s in |text|, sorted, always including
// the start and end of the text.
std::vector<int32_t> GetBoundaries(std::u16string_view text,
                                   Boundary boundary) {
  std::vector<int32_t> utf16;
  int32_t length = static_cast<int32_t>(text.size());
  switch (boundary) {
    case Boundary::kChar:
      for (int32_t i = 0; i < length;) {
        utf16.push_back(i);
        U16_FWD_1(text.data(), i, length);
      }
      break;
    case Boundary::kWordStart:
    case Boundary::kWordEnd: {
      bool starts = boundary == Boundary::kWordStart;
      utf16 = IcuBoundaries(
          text, starts ? BreakType::kWordStart : BreakType::kWordEnd);
      if (utf16.empty()) {
        utf16 = SimpleWordBoundaries(text, starts);
      }
      break;
    }
    case Boundary::kSentenceStart:
    case Boundary::kSentenceEnd:
      utf16 = IcuBoundaries(text, BreakType::kSentence);
      break;
    case Boundary::kLineStart:
    case Boundary::kLineEnd:
      for (int32_t i = 0; i < length; i++) {
        if (text[i] == u'\n') {
          utf16.push_back(boundary == Boundary::kLineStart ? i + 1 : i);
        }
      }
      break;
  }
  utf16.push_back(0);
  utf16.push_back(length);

  std::vector<int32_t> boundaries;
  boundaries.reserve(utf16.size());
  for (int32_t offset : utf16) {
    boundaries.push_back(ToCharacterOffset(text, offset));
  }
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                   boundaries.end());
  return boundaries;
}

}  // namespace

int32_t CharacterCount(std::u16string_view text) {
  return ToUnicodeString(text).countChar32();
}

int32_t ToUtf16Offset(std::u16string_view text, int32_t offset) {
  if (offset <= 0) {
    return 0;
  }
  return ToUnicodeString(text).moveIndex32(0, offset);
}

int32_t ToCharacterOffset(std::u16string_view text, int32_t utf16_offset) {
  utf16_offset = std::clamp(utf16_offset, 0, static_cast<int32_t>(text.size()));
  return ToUnicodeString(text).countChar32(0, utf16_offset);
}

std::string ToUtf8(std::u16string_view text) {
  std::string result;
  ToUnicodeString(text).toUTF8String(result);
  return result;
}

std::u16string FromUtf8(std::string_view text) {
  icu::UnicodeString string = icu::UnicodeString::fromUTF8(
      icu::StringPiece(text.data(), static_cast<int32_t>(text.size())));
  return std::u16string(string.getBuffer(), string.length());
}

std::string Substring(std::u16string_view text, int32_t start, int32_t end) {
  int32_t count = CharacterCount(text);
  if (end < 0 || end > count) {
    end = count;
  }
  start = std::clamp(start, 0, end);
  int32_t utf16_start = ToUtf16Offset(text, start);
  int32_t utf16_end = ToUtf16Offset(text, end);
  return ToUtf8(text.substr(utf16_start, utf16_end - utf16_start));
}

Range GetTextAtOffset(std::u16string_view text,
                      int32_t offset,
                      Boundary boundary,
                      Position position) {
  std::vector<int32_t> boundaries = GetBoundaries(text, boundary);
  int32_t count = boundaries.back();
  if (boundaries.size() < 2) {
    return {0, 0};
  }
  offset = std::clamp(offset, 0, count);

  // The range containing |offset|. At the end of the text this is the last
  // range.
  auto it = std::upper_bound(boundaries.begin(), boundaries.end(), offset);
  if (it == boundaries.end()) {
    it--;
  }
  size_t end_index = std::max<size_t>(it - boundaries.begin(), 1);
  if (boundary == Boundary::kChar && offset == count) {
    return position == Position::kBefore && count > 0 ? Range{count - 1, count}
                                                      : Range{count, count};
  }

  switch (position) {
    case Position::kAt:
      break;
    case Position::kBefore:
      if (end_index < 2) {
        return {0, 0};
      }
      end_index--;
      break;
    case Position::kAfter:
      if (end_index + 1 >= boundaries.size()) {
        return {count, count};
      }
      end_index++;
      break;
  }
  return {boundaries[end_index - 1], boundaries[end_index]};
}

Range GetStringAtOffset(std::u16string_view text,
                        int32_t offset,
                        Granularity granularity) {
  int32_t count = CharacterCount(text);
  offset = std::clamp(offset, 0, count);
  switch (granularity) {
    case Granularity::kChar:
      return offset < count ? Range{offset, offset + 1} : Range{count, count};
    case Granularity::kWord: {
      // The word containing |offset|, or the space between words.
      std::vector<int32_t> starts = GetBoundaries(text, Boundary::kWordStart);
      std::vector<int32_t> ends = GetBoundaries(text, Boundary::kWordEnd);
      std::vector<int32_t> all = starts;
      all.insert(all.end(), ends.begin(), ends.end());
      std::sort(all.begin(), all.end());
      all.erase(std::unique(all.begin(), all.end()), all.end());
      auto it = std::upper_bound(all.begin(), all.end(), offset);
      if (it == all.end()) {
        it--;
      }
      if (it == all.begin()) {
        return {0, 0};
      }
      return {*(it - 1), *it};
    }
    case Granularity::kSentence:
      return GetTextAtOffset(text, offset, Boundary::kSentenceStart,
                             Position::kAt);
    case Granularity::kLine:
    case Granularity::kParagraph: {
      // The line excluding its newline.
      Range range =
          GetTextAtOffset(text, offset, Boundary::kLineStart, Position::kAt);
      std::u16string_view line = text.substr(
          ToUtf16Offset(text, range.start),
          ToUtf16Offset(text, range.end) - ToUtf16Offset(text, range.start));
      if (!line.empty() && line.back() == u'\n') {
        range.end--;
      }
      return range;
    }
  }
  return {offset, offset};
}

}  // namespace atspi_text
}  // namespace flutter
