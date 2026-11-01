// ProsperoTV - Console-language mapping and stable translation fallback.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/i18n.hpp"
#include "iptv_http.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
struct Translation
{
    const char *english;
    std::array<const char *, 6> texts;
};
const Translation translations[] = {
#include "tv/translations.inc"
#include "tv/translations_share.inc"
};
struct RtlTranslation
{
    const char *english;
    std::array<const char *, 2> texts;
};
const RtlTranslation rtl_translations[] = {
#include "tv/translations_rtl.inc"
};

TEST(InterfaceLanguage, RtlCatalogCoversEveryKeyWithoutChangingFormatOrFragments)
{
    std::set<std::string_view> expected, actual;
    for (const auto &entry : translations)
        expected.emplace(entry.english);
    for (const auto &entry : rtl_translations)
    {
        const std::string_view key(entry.english);
        ASSERT_TRUE(actual.emplace(key).second) << key;
        for (const char *translated : entry.texts)
        {
            ASSERT_NE(translated, nullptr) << key;
            const std::string_view text(translated);
            ASSERT_FALSE(text.empty()) << key;
            EXPECT_EQ(std::count(key.begin(), key.end(), '%'),
                      std::count(text.begin(), text.end(), '%'))
                << key;
            EXPECT_EQ(key.find_first_not_of(' '), text.find_first_not_of(' ')) << key;
            EXPECT_EQ(key.size() - key.find_last_not_of(' '),
                      text.size() - text.find_last_not_of(' '))
                << key;
            // Literal percentages (25%, 125%, 150%) are not printf placeholders.
            // These are the conversion specifiers used by the current catalog.
            for (const std::string_view token : {"%u", "%d", "%s", "%%"})
            {
                const auto count = [token](std::string_view value)
                {
                    std::size_t result = 0, pos = 0;
                    while ((pos = value.find(token, pos)) != std::string_view::npos)
                    {
                        ++result;
                        pos += token.size();
                    }
                    return result;
                };
                EXPECT_EQ(count(key), count(text)) << key << " token " << token;
            }
            // Keep logical Unicode order; do not bake visual order or bidi control
            // overrides into translations. Direction belongs to the renderer.
            for (const std::string_view forbidden :
                 {"\xef\xbf\xbd", "\xe2\x80\xaa", "\xe2\x80\xab", "\xe2\x80\xac", "\xe2\x80\xad",
                  "\xe2\x80\xae"})
                EXPECT_EQ(text.find(forbidden), std::string_view::npos) << key;
        }
    }
    EXPECT_EQ(expected, actual);
}

std::string placeholders(std::string_view text)
{
    std::string result;
    for (std::size_t pos = 0; pos < text.size(); ++pos)
        if (text[pos] == '%')
        {
            result += '%';
            if (++pos < text.size())
                result += text[pos];
        }
    return result;
}

TEST(InterfaceLanguage, RegionalVariantsAndUnknownValues)
{
    for (const int id : {3, 20})
    {
        ptv::set_console_language(id);
        EXPECT_STREQ(ptv::interface_language(), "es");
        EXPECT_STREQ(ptv::tr("Settings"), "Ajustes");
    }
    for (const int id : {7, 17})
    {
        ptv::set_console_language(id);
        EXPECT_STREQ(ptv::interface_language(), "pt");
    }
    for (const int id : {2, 22})
    {
        ptv::set_console_language(id);
        EXPECT_STREQ(ptv::interface_language(), "fr");
    }
    for (const int id : {-1, 1, 18, 999})
    {
        ptv::set_console_language(id);
        EXPECT_STREQ(ptv::interface_language(), "en");
        EXPECT_STREQ(ptv::tr("Settings"), "Settings");
    }
    EXPECT_STREQ(ptv::tr(nullptr), "");
}

TEST(InterfaceLanguage, EveryEntryHasAllTranslationsAndUnknownTextSurvives)
{
    std::set<std::string_view> keys;
    for (const auto &entry : translations)
        EXPECT_TRUE(keys.emplace(entry.english).second) << entry.english;
    const int languages[] = {3, 2, 4, 5, 7, 6};
    for (unsigned language = 0; language < std::size(languages); ++language)
    {
        ptv::set_console_language(languages[language]);
        for (const auto &entry : translations)
        {
            const auto text = entry.texts[language];
            EXPECT_TRUE(text && *text);
            if (text)
            {
                EXPECT_STREQ(ptv::tr(entry.english), text);
                EXPECT_EQ(placeholders(entry.english), placeholders(text)) << entry.english;
            }
        }
        EXPECT_STREQ(ptv::tr("Provider Channel 1"), "Provider Channel 1");
    }
    ptv::set_console_language(1);
}

// Every connection failure the player can describe (src/iptv_http.cpp) reads whole in each
// language: the request it was, what happened, and libcurl's code as it is.
TEST(InterfaceLanguage, EveryConnectionFailureOfThePlayerIsTranslated)
{
    using iptv::http::CurlFailure;
    using iptv::http::CurlStage;
    std::vector<std::pair<int, int>> failures; // native error, libcurl's code
    for (const int code : {1, 3, 6, 7, 8, 27, 35, 42, 47, 52, 55, 56, 60, 77, 99})
        failures.emplace_back(CurlFailure(code), code);
    for (const auto stage : {CurlStage::unknown, CurlStage::lookup, CurlStage::connect,
                             CurlStage::secure, CurlStage::answer})
        failures.emplace_back(CurlFailure(28, stage), 28);
    for (const char *context :
         {"Channel request failed", "Playlist request failed", "Channel segment request failed"})
        for (const auto &[native, code] : failures)
        {
            char reason[160];
            iptv::http::DescribeFailure(iptv::http::Status::request_failed, 0, native, nullptr,
                                        reason, sizeof(reason));
            const std::string detail = std::string(context) + ": " + reason + ".";
            const std::string tail = " (curl " + std::to_string(code) + ").";
            ptv::set_interface_language(1);
            EXPECT_EQ(ptv::tr_reason(detail), detail);
            for (unsigned language = 2; language <= 9; ++language)
            {
                ptv::set_interface_language(language);
                const std::string shown = ptv::tr_reason(detail);
                EXPECT_NE(shown, detail) << language << ' ' << detail;
                EXPECT_EQ(shown.rfind(std::string(ptv::tr(context)) + ": ", 0), 0u) << shown;
                ASSERT_GE(shown.size(), tail.size());
                EXPECT_EQ(shown.substr(shown.size() - tail.size()), tail) << shown;
            }
        }
    // Whatever has no translation is left as the player wrote it.
    ptv::set_interface_language(4);
    for (const char *other : {"Channel request failed: stream is offline or no longer exists "
                              "(HTTP 404).",
                              "The channel stalled without producing video for 15 seconds.",
                              "Unknown request failed: connection failed (curl 99).", "", ":"})
        EXPECT_EQ(ptv::tr_reason(other), other);
    ptv::set_interface_language(0);
}
} // namespace
