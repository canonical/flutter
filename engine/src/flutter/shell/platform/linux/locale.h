// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_LOCALE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_LOCALE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace flutter {

// A locale, using the codes Flutter expects.
struct Locale {
  std::string language;
  std::string country;
  std::string script;

  bool operator==(const Locale& other) const = default;
};

// Parses a POSIX locale name, "language[_territory][.codeset][@modifier]".
// Returns nullopt for the C and POSIX locales, which have no language.
std::optional<Locale> ParseLocale(std::string_view name);

// Returns the user's preferred locales, most preferred first, using the
// environment variables gettext uses. |language| is a colon separated list,
// and is ignored when the messages locale is C, as gettext does.
std::vector<Locale> GetPreferredLocales(const char* language,
                                        const char* lc_all,
                                        const char* lc_messages,
                                        const char* lang);

// Returns the preferred locales from the environment.
std::vector<Locale> GetPreferredLocales();

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_LOCALE_H_
