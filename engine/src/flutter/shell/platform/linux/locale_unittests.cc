// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/locale.h"

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

TEST(LocaleTest, ParsesLocale) {
  EXPECT_EQ(ParseLocale("en_US.UTF-8"), (Locale{"en", "US", ""}));
  EXPECT_EQ(ParseLocale("de"), (Locale{"de", "", ""}));
  EXPECT_EQ(ParseLocale("pt_BR"), (Locale{"pt", "BR", ""}));
  EXPECT_EQ(ParseLocale("sr_RS.UTF-8@latin"), (Locale{"sr", "RS", "Latn"}));
  EXPECT_EQ(ParseLocale("ca_ES@valencia"), (Locale{"ca", "ES", ""}));
}

TEST(LocaleTest, IgnoresCLocale) {
  EXPECT_FALSE(ParseLocale("C").has_value());
  EXPECT_FALSE(ParseLocale("C.UTF-8").has_value());
  EXPECT_FALSE(ParseLocale("POSIX").has_value());
  EXPECT_FALSE(ParseLocale("").has_value());
}

TEST(LocaleTest, UsesLang) {
  EXPECT_EQ(GetPreferredLocales(nullptr, nullptr, nullptr, "fr_FR.UTF-8"),
            (std::vector<Locale>{{"fr", "FR", ""}}));
}

TEST(LocaleTest, PrefersLcAllThenLcMessages) {
  EXPECT_EQ(GetPreferredLocales(nullptr, "de_DE", "fr_FR", "en_US"),
            (std::vector<Locale>{{"de", "DE", ""}}));
  EXPECT_EQ(GetPreferredLocales(nullptr, "", "fr_FR", "en_US"),
            (std::vector<Locale>{{"fr", "FR", ""}}));
}

TEST(LocaleTest, UsesLanguageList) {
  EXPECT_EQ(
      GetPreferredLocales("pt_BR:pt::en", nullptr, nullptr, "en_US"),
      (std::vector<Locale>{
          {"pt", "BR", ""}, {"pt", "", ""}, {"en", "", ""}, {"en", "US", ""}}));
}

TEST(LocaleTest, RemovesDuplicates) {
  EXPECT_EQ(
      GetPreferredLocales("en_US:en_US.UTF-8", nullptr, nullptr, "en_US.UTF-8"),
      (std::vector<Locale>{{"en", "US", ""}}));
}

TEST(LocaleTest, IgnoresLanguageForCLocale) {
  EXPECT_TRUE(GetPreferredLocales("de", nullptr, nullptr, "C").empty());
  EXPECT_TRUE(GetPreferredLocales("de", nullptr, nullptr, nullptr).empty());
}

}  // namespace testing
}  // namespace flutter
