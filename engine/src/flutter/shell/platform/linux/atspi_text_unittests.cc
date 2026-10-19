// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/atspi_text.h"

#include <filesystem>

#include "flutter/fml/icu_util.h"
#include "flutter/fml/paths.h"
#include "gtest/gtest.h"

namespace flutter {
namespace atspi_text {
namespace {

class AtspiTextTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    std::filesystem::path path =
        std::filesystem::path(fml::paths::GetExecutableDirectoryPath().second) /
        "icudtl.dat";
    if (std::filesystem::exists(path)) {
      fml::icu::InitializeICU(path.string());
    }
  }
};

TEST_F(AtspiTextTest, Offsets) {
  // "a😀b": the emoji is two UTF-16 code units.
  std::u16string text = u"a\U0001F600b";
  EXPECT_EQ(CharacterCount(text), 3);
  EXPECT_EQ(ToUtf16Offset(text, 0), 0);
  EXPECT_EQ(ToUtf16Offset(text, 2), 3);
  EXPECT_EQ(ToUtf16Offset(text, 3), 4);
  EXPECT_EQ(ToCharacterOffset(text, 3), 2);
  EXPECT_EQ(ToCharacterOffset(text, 100), 3);
  EXPECT_EQ(Substring(text, 1, 2), "\xF0\x9F\x98\x80");
  EXPECT_EQ(Substring(text, 1, -1),
            "\xF0\x9F\x98\x80"
            "b");
  EXPECT_EQ(Substring(text, 5, 1), "");
  EXPECT_EQ(FromUtf8(ToUtf8(text)), text);
}

TEST_F(AtspiTextTest, Characters) {
  std::u16string text = u"ab";
  EXPECT_EQ(GetTextAtOffset(text, 0, Boundary::kChar, Position::kAt),
            (Range{0, 1}));
  EXPECT_EQ(GetTextAtOffset(text, 1, Boundary::kChar, Position::kBefore),
            (Range{0, 1}));
  EXPECT_EQ(GetTextAtOffset(text, 0, Boundary::kChar, Position::kAfter),
            (Range{1, 2}));
  EXPECT_EQ(GetTextAtOffset(text, 2, Boundary::kChar, Position::kAt),
            (Range{2, 2}));
  EXPECT_EQ(GetStringAtOffset(text, 1, Granularity::kChar), (Range{1, 2}));
  EXPECT_EQ(GetStringAtOffset(text, 2, Granularity::kChar), (Range{2, 2}));
}

TEST_F(AtspiTextTest, Words) {
  std::u16string text = u"one two  three";
  EXPECT_EQ(GetTextAtOffset(text, 5, Boundary::kWordStart, Position::kAt),
            (Range{4, 9}));
  EXPECT_EQ(GetTextAtOffset(text, 5, Boundary::kWordStart, Position::kBefore),
            (Range{0, 4}));
  EXPECT_EQ(GetTextAtOffset(text, 5, Boundary::kWordStart, Position::kAfter),
            (Range{9, 14}));
  EXPECT_EQ(GetTextAtOffset(text, 5, Boundary::kWordEnd, Position::kAt),
            (Range{3, 7}));
  EXPECT_EQ(GetStringAtOffset(text, 5, Granularity::kWord), (Range{4, 7}));
  EXPECT_EQ(GetStringAtOffset(text, 7, Granularity::kWord), (Range{7, 9}));
}

TEST_F(AtspiTextTest, Lines) {
  std::u16string text = u"first\nsecond\nthird";
  EXPECT_EQ(GetTextAtOffset(text, 8, Boundary::kLineStart, Position::kAt),
            (Range{6, 13}));
  EXPECT_EQ(GetTextAtOffset(text, 8, Boundary::kLineStart, Position::kBefore),
            (Range{0, 6}));
  EXPECT_EQ(GetTextAtOffset(text, 8, Boundary::kLineStart, Position::kAfter),
            (Range{13, 18}));
  EXPECT_EQ(GetTextAtOffset(text, 8, Boundary::kLineEnd, Position::kAt),
            (Range{5, 12}));
  EXPECT_EQ(GetStringAtOffset(text, 8, Granularity::kLine), (Range{6, 12}));
  EXPECT_EQ(GetStringAtOffset(text, 18, Granularity::kLine), (Range{13, 18}));
}

TEST_F(AtspiTextTest, Sentences) {
  std::u16string text = u"Hello there. How are you?";
  Range range = GetStringAtOffset(text, 15, Granularity::kSentence);
  EXPECT_EQ(range, (Range{13, 25}));
  EXPECT_EQ(Substring(text, range.start, range.end), "How are you?");
}

TEST_F(AtspiTextTest, Empty) {
  std::u16string text;
  EXPECT_EQ(GetTextAtOffset(text, 0, Boundary::kWordStart, Position::kAt),
            (Range{0, 0}));
  EXPECT_EQ(GetStringAtOffset(text, 0, Granularity::kLine), (Range{0, 0}));
}

}  // namespace
}  // namespace atspi_text
}  // namespace flutter
