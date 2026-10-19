// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/locale.h"

#include <algorithm>
#include <cstdlib>

namespace flutter {

namespace {

bool IsSet(const char* value) {
  return value != nullptr && value[0] != '\0';
}

// Returns the script for a locale modifier, e.g. "latin" in "sr_RS@latin".
std::string ScriptForModifier(std::string_view modifier) {
  if (modifier == "latin") {
    return "Latn";
  }
  if (modifier == "cyrillic") {
    return "Cyrl";
  }
  if (modifier == "devanagari") {
    return "Deva";
  }
  return "";
}

}  // namespace

std::optional<Locale> ParseLocale(std::string_view name) {
  std::string_view modifier;
  size_t at = name.find('@');
  if (at != std::string_view::npos) {
    modifier = name.substr(at + 1);
    name = name.substr(0, at);
  }
  size_t dot = name.find('.');
  if (dot != std::string_view::npos) {
    name = name.substr(0, dot);
  }

  Locale locale;
  size_t underscore = name.find('_');
  locale.language = std::string(name.substr(0, underscore));
  if (underscore != std::string_view::npos) {
    locale.country = std::string(name.substr(underscore + 1));
  }
  locale.script = ScriptForModifier(modifier);

  if (locale.language.empty() || locale.language == "C" ||
      locale.language == "POSIX") {
    return std::nullopt;
  }
  return locale;
}

std::vector<Locale> GetPreferredLocales(const char* language,
                                        const char* lc_all,
                                        const char* lc_messages,
                                        const char* lang) {
  const char* messages_locale = IsSet(lc_all)        ? lc_all
                                : IsSet(lc_messages) ? lc_messages
                                : IsSet(lang)        ? lang
                                                     : nullptr;

  std::vector<std::string_view> names;
  std::optional<Locale> base;
  if (messages_locale != nullptr) {
    base = ParseLocale(messages_locale);
  }
  if (base.has_value() && IsSet(language)) {
    std::string_view list(language);
    while (!list.empty()) {
      size_t colon = list.find(':');
      names.push_back(list.substr(0, colon));
      list = colon == std::string_view::npos ? std::string_view()
                                             : list.substr(colon + 1);
    }
  }
  if (messages_locale != nullptr) {
    names.push_back(messages_locale);
  }

  std::vector<Locale> locales;
  for (std::string_view name : names) {
    std::optional<Locale> locale = ParseLocale(name);
    if (locale.has_value() &&
        std::find(locales.begin(), locales.end(), *locale) == locales.end()) {
      locales.push_back(std::move(*locale));
    }
  }
  return locales;
}

std::vector<Locale> GetPreferredLocales() {
  return GetPreferredLocales(getenv("LANGUAGE"), getenv("LC_ALL"),
                             getenv("LC_MESSAGES"), getenv("LANG"));
}

}  // namespace flutter
