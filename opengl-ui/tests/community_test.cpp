// Physalis - The community on the About tab and on the card shown once, read back as a phone does.
// Copyright (C) 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "host_platform.hpp"
#include "tv/app.hpp"
#include "tv/i18n.hpp"
#include "tv/settings.hpp"
#include "qrcodegen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace fs = std::filesystem;
using hui::Action;

constexpr float kDt = 1.0f / 60.0f;
// What the code must open, written out here rather than taken from the app.
constexpr std::string_view kInvitation = "https://discord.gg/pSqgRU3YP9";

// ---- a QR code read back from its modules (ISO/IEC 18004) ----
// Enough of a reader for what the app draws: versions 1 to 9, any error correction level and
// mask, byte-mode segments. Every block's error correction is checked, as a reader checks before
// it trusts the data; nothing is repaired.

using Modules = std::vector<std::vector<bool>>; // [row][column], true: dark

// Multiplication in GF(256) modulo x^8 + x^4 + x^3 + x^2 + 1.
std::uint8_t gf_multiply(std::uint8_t a, std::uint8_t b)
{
    unsigned product = 0;
    for (int bit = 7; bit >= 0; --bit)
    {
        product = ((product << 1) ^ ((product >> 7) * 0x11du)) & 0xffu;
        if ((b >> bit) & 1u)
            product ^= a;
    }
    return static_cast<std::uint8_t>(product);
}

// A block (its data, then its error correction) is a code word when its polynomial is zero at the
// first `ecc` powers of 2.
bool whole_block(const std::vector<std::uint8_t> &block, int ecc)
{
    std::uint8_t power = 1;
    for (int i = 0; i < ecc; ++i)
    {
        std::uint8_t value = 0;
        for (const std::uint8_t byte : block)
            value = gf_multiply(value, power) ^ byte;
        if (value != 0)
            return false;
        power = gf_multiply(power, 2);
    }
    return true;
}

// The 15 format bits of a level (as the symbol writes it: M 0, L 1, H 2, Q 3) and a mask.
int format_bits(int level, int mask)
{
    const int data = level << 3 | mask;
    int remainder = data;
    for (int i = 0; i < 10; ++i)
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
    return (data << 10 | remainder) ^ 0x5412;
}

// The standard's tables for versions 1 to 9, by level as the format bits number it (M, L, H, Q).
constexpr int kCodewords[10] = {0, 26, 44, 70, 100, 134, 172, 196, 242, 292};
constexpr int kBlocks[4][10] = {{0, 1, 1, 1, 2, 2, 4, 4, 4, 5},
                                {0, 1, 1, 1, 1, 1, 2, 2, 2, 2},
                                {0, 1, 1, 2, 4, 4, 4, 5, 6, 8},
                                {0, 1, 1, 2, 2, 4, 4, 6, 6, 8}};
constexpr int kBlockEcc[4][10] = {{0, 10, 16, 26, 18, 24, 16, 18, 22, 22},
                                  {0, 7, 10, 15, 20, 26, 18, 20, 24, 30},
                                  {0, 17, 28, 22, 16, 22, 28, 26, 26, 24},
                                  {0, 13, 22, 18, 26, 18, 24, 18, 22, 20}};
// Where the alignment patterns' centres are, row and column alike (Annex E).
const std::vector<int> kAlignment[10] = {{},      {},      {6, 18},     {6, 22},     {6, 26},
                                         {6, 30}, {6, 34}, {6, 22, 38}, {6, 24, 42}, {6, 26, 46}};

bool masked(int mask, int row, int column)
{
    const int i = row, j = column;
    switch (mask)
    {
    case 0:
        return (i + j) % 2 == 0;
    case 1:
        return i % 2 == 0;
    case 2:
        return j % 3 == 0;
    case 3:
        return (i + j) % 3 == 0;
    case 4:
        return (i / 2 + j / 3) % 2 == 0;
    case 5:
        return (i * j) % 2 + (i * j) % 3 == 0;
    case 6:
        return ((i * j) % 2 + (i * j) % 3) % 2 == 0;
    default:
        return ((i * j) % 3 + (i + j) % 2) % 2 == 0;
    }
}

// The text a QR code holds, or nothing when it cannot be read.
std::optional<std::string> read_qr(const Modules &m)
{
    const int size = static_cast<int>(m.size());
    const int version = (size - 17) / 4;
    if (size < 21 || (size - 17) % 4 != 0 || version > 9)
        return std::nullopt;
    for (const auto &row : m)
        if (static_cast<int>(row.size()) != size)
            return std::nullopt;

    // The format, from whichever copy is nearer a valid one.
    int first = 0, second = 0;
    for (int i = 0; i <= 5; ++i)
        first |= m[i][8] << i;
    first |= m[7][8] << 6 | m[8][8] << 7 | m[8][7] << 8;
    for (int i = 9; i < 15; ++i)
        first |= m[8][14 - i] << i;
    for (int i = 0; i < 8; ++i)
        second |= m[8][size - 1 - i] << i;
    for (int i = 8; i < 15; ++i)
        second |= m[size - 15 + i][8] << i;
    int level = -1, mask = -1, best = 99;
    for (int l = 0; l < 4; ++l)
        for (int k = 0; k < 8; ++k)
            for (const int read : {first, second})
                if (const int distance = __builtin_popcount(read ^ format_bits(l, k));
                    distance < best)
                {
                    best = distance;
                    level = l;
                    mask = k;
                }
    if (best > 3)
        return std::nullopt;

    // Which modules are the symbol's own patterns, not data.
    std::vector<std::vector<bool>> fixed(size, std::vector<bool>(size, false));
    const auto mark = [&](int top, int left, int rows, int columns)
    {
        for (int r = std::max(0, top); r < std::min(size, top + rows); ++r)
            for (int c = std::max(0, left); c < std::min(size, left + columns); ++c)
                fixed[r][c] = true;
    };
    mark(0, 0, 9, 9);        // finder, separator and format at the top left
    mark(0, size - 8, 9, 8); // at the top right
    mark(size - 8, 0, 8, 9); // at the bottom left, with the dark module
    mark(6, 0, 1, size);     // timing
    mark(0, 6, size, 1);
    const std::vector<int> &centres = kAlignment[version];
    for (const int r : centres)
        for (const int c : centres)
        {
            const int last = centres.empty() ? 0 : centres.back();
            if ((r == 6 && c == 6) || (r == 6 && c == last) || (r == last && c == 6))
                continue;
            mark(r - 2, c - 2, 5, 5);
        }
    if (version >= 7)
    {
        mark(0, size - 11, 6, 3);
        mark(size - 11, 0, 3, 6);
    }

    // The data, two columns at a time from the bottom right, up and down in turn.
    std::vector<std::uint8_t> codewords(static_cast<std::size_t>(kCodewords[version]), 0);
    std::size_t bit = 0;
    for (int right = size - 1; right >= 1; right -= 2)
    {
        if (right == 6)
            right = 5;
        const bool upward = ((right + 1) & 2) == 0;
        for (int step = 0; step < size; ++step)
            for (int j = 0; j < 2; ++j)
            {
                const int column = right - j;
                const int row = upward ? size - 1 - step : step;
                if (fixed[row][column] || bit >= codewords.size() * 8)
                    continue;
                if (m[row][column] != masked(mask, row, column))
                    codewords[bit / 8] |= static_cast<std::uint8_t>(0x80u >> (bit % 8));
                ++bit;
            }
    }

    // The blocks, interleaved: their data first, then their error correction.
    const int blocks = kBlocks[level][version];
    const int ecc = kBlockEcc[level][version];
    const int total = kCodewords[version];
    const int short_blocks = blocks - total % blocks;
    const int short_data = total / blocks - ecc;
    std::vector<std::vector<std::uint8_t>> data(static_cast<std::size_t>(blocks));
    std::vector<std::vector<std::uint8_t>> parity(static_cast<std::size_t>(blocks));
    std::size_t at = 0;
    for (int i = 0; i <= short_data; ++i)
        for (int b = 0; b < blocks; ++b)
            if (i < short_data + (b >= short_blocks ? 1 : 0))
                data[b].push_back(codewords[at++]);
    for (int i = 0; i < ecc; ++i)
        for (int b = 0; b < blocks; ++b)
            parity[b].push_back(codewords[at++]);
    std::vector<std::uint8_t> stream;
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<std::uint8_t> block = data[b];
        block.insert(block.end(), parity[b].begin(), parity[b].end());
        if (!whole_block(block, ecc))
            return std::nullopt;
        stream.insert(stream.end(), data[b].begin(), data[b].end());
    }

    // The segments: byte mode only, as the app writes an address.
    std::size_t read = 0;
    const auto take = [&](int count)
    {
        int value = 0;
        for (int i = 0; i < count; ++i, ++read)
            value = value << 1 | ((stream[read / 8] >> (7 - read % 8)) & 1);
        return value;
    };
    std::string text;
    while (read + 4 <= stream.size() * 8)
    {
        const int mode = take(4);
        if (mode == 0)
            break;
        if (mode != 4 || read + 8 > stream.size() * 8)
            return std::nullopt;
        const int length = take(8);
        if (read + static_cast<std::size_t>(length) * 8 > stream.size() * 8)
            return std::nullopt;
        for (int i = 0; i < length; ++i)
            text += static_cast<char>(take(8));
    }
    return text;
}

// ---- what a frame draws ----

bool colour_is(const float *colour, float value)
{
    return std::abs(colour[0] - value) < 0.01f && std::abs(colour[1] - value) < 0.01f &&
           std::abs(colour[2] - value) < 0.01f && colour[3] > 0.99f;
}

bool square(const hui::gfx::Instance &instance)
{
    return static_cast<int>(instance.params[3] + 0.5f) ==
               static_cast<int>(hui::gfx::Shape::rounded_rect) &&
           std::abs(instance.rect[2] - instance.rect[3]) < 0.01f;
}

// The QR code a list draws, module by module: the white square of its quiet zone and the dark
// squares inside it. Empty when the list draws none.
Modules drawn_code(const hui::gfx::DrawList &list)
{
    const auto &all = list.instances();
    for (const hui::gfx::Instance &quiet : all)
    {
        if (!square(quiet) || !colour_is(quiet.color_top, 1.0f) || quiet.rect[2] < 100.0f)
            continue;
        std::vector<const hui::gfx::Instance *> dark;
        float cell = quiet.rect[2];
        for (const hui::gfx::Instance &module : all)
            if (square(module) && colour_is(module.color_top, 0.0f) &&
                module.rect[0] >= quiet.rect[0] && module.rect[1] >= quiet.rect[1] &&
                module.rect[0] + module.rect[2] <= quiet.rect[0] + quiet.rect[2] + 0.01f &&
                module.rect[1] + module.rect[3] <= quiet.rect[1] + quiet.rect[3] + 0.01f)
            {
                dark.push_back(&module);
                cell = std::min(cell, module.rect[2]);
            }
        if (dark.empty())
            continue;
        const int size = static_cast<int>(std::lround(quiet.rect[2] / cell)) - 8;
        if (size < 21)
            continue;
        Modules modules(size, std::vector<bool>(size, false));
        for (const hui::gfx::Instance *module : dark)
        {
            const long column = std::lround((module->rect[0] - quiet.rect[0]) / cell) - 4;
            const long row = std::lround((module->rect[1] - quiet.rect[1]) / cell) - 4;
            if (column >= 0 && column < size && row >= 0 && row < size)
                modules[row][column] = true;
        }
        return modules;
    }
    return {};
}

// Whether a list draws `text` in one of the faces: the same glyphs one after another, from the
// same places of the same atlas, whatever their size.
bool draws_text(const hui::gfx::DrawList &list, const hui::ui::Fonts &fonts, std::string_view text)
{
    for (const hui::ui::FontRef *face :
         {&fonts.regular, &fonts.semibold, &fonts.display, &fonts.mono})
    {
        hui::gfx::DrawList probe;
        probe.text(*face->font, face->texture, text, 0.0f, 100.0f, 30.0f, hui::gfx::Color{});
        const auto &want = probe.instances();
        const auto &have = list.instances();
        for (std::size_t start = 0; !want.empty() && start + want.size() <= have.size(); ++start)
        {
            bool same = true;
            for (std::size_t k = 0; k < want.size() && same; ++k)
                same = have[start + k].params[3] == want[k].params[3] &&
                       have[start + k].params[2] == want[k].params[2] &&
                       std::equal(std::begin(want[k].extra), std::end(want[k].extra),
                                  std::begin(have[start + k].extra));
            if (same)
                return true;
        }
    }
    return false;
}

// The kit's baked fonts, which the console draws with (KIT_FONTS, set by tools/run-tests.sh).
struct FontSet
{
    hui::gfx::Font regular, semibold, display, mono;
    hui::ui::Fonts fonts;

    FontSet()
    {
        const char *dir = std::getenv("KIT_FONTS");
        load(dir, "inter-regular.huifont", &regular, &fonts.regular, 0xf0000001u);
        load(dir, "inter-semibold.huifont", &semibold, &fonts.semibold, 0xf0000002u);
        load(dir, "montserrat-medium.huifont", &display, &fonts.display, 0xf0000003u);
        load(dir, "dejavu-sans-mono.huifont", &mono, &fonts.mono, 0xf0000004u);
    }

    static void load(const char *dir, const char *name, hui::gfx::Font *font, hui::ui::FontRef *ref,
                     std::uint32_t handle)
    {
        std::string data;
        const std::string path = std::string(dir != nullptr ? dir : ".") + "/" + name;
        if (!hui::save::read_file(path, &data, 64u << 20) || !font->load(data))
        {
            ADD_FAILURE() << "cannot load " << path;
            return;
        }
        ref->font = font;
        ref->texture = handle;
    }
};

const FontSet &font_set()
{
    static const FontSet set;
    return set;
}

class CommunityTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        host::reset();
        char pattern[] = "/tmp/physalis-community-XXXXXX";
        dir_ = mkdtemp(pattern);
        model_ = std::make_unique<ptv::Model>(dir_);
        ASSERT_TRUE(model_->open());
    }

    void TearDown() override
    {
        app_.reset();
        model_->close();
        model_.reset();
        ptv::set_interface_language(1);
        std::error_code error;
        fs::remove_all(dir_, error);
    }

    void make_app(const ptv::Settings &settings)
    {
        app_ = std::make_unique<ptv::App>(*model_, font_set().fonts, 7u, settings, "test");
    }
    void frame(std::uint32_t pressed = 0)
    {
        hui::InputFrame input;
        input.connected = true;
        input.pressed = pressed;
        input.held = pressed;
        feedback_.clear();
        app_->update(input, kDt, feedback_);
        app_->draw(frame_);
    }
    void idle(int frames)
    {
        for (int i = 0; i < frames; ++i)
            frame();
    }

    std::string dir_;
    std::unique_ptr<ptv::Model> model_;
    std::unique_ptr<ptv::App> app_;
    ptv::Frame frame_;
    hui::ui::Feedback feedback_;
};

// The reader itself, on symbols the vendored encoder makes: every level and mask, versions 1 to
// 9. A module changed in the data is caught by the error correction check.
TEST(QrReader, ReadsEveryLevelMaskAndVersionAndRefusesADamagedCode)
{
    const std::string texts[] = {"hi", std::string(kInvitation), std::string(90, 'q') + "/end",
                                 "https://example.invalid/" + std::string(150, 'z')};
    std::set<int> versions;
    for (const std::string &text : texts)
        for (const qrcodegen_Ecc level :
             {qrcodegen_Ecc_LOW, qrcodegen_Ecc_MEDIUM, qrcodegen_Ecc_QUARTILE, qrcodegen_Ecc_HIGH})
            for (int mask = 0; mask < 8; ++mask)
            {
                std::uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(9)];
                std::uint8_t scratch[sizeof(qr)];
                if (!qrcodegen_encodeText(text.c_str(), scratch, qr, level, 1, 9,
                                          static_cast<qrcodegen_Mask>(mask), false))
                    continue;
                const int size = qrcodegen_getSize(qr);
                versions.insert((size - 17) / 4);
                Modules modules(size, std::vector<bool>(size, false));
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x)
                        modules[y][x] = qrcodegen_getModule(qr, x, y);
                ASSERT_EQ(read_qr(modules), std::optional<std::string>(text))
                    << text.size() << " bytes, level " << level << ", mask " << mask;
                // The module at the bottom right is always data.
                modules[size - 1][size - 1] = !modules[size - 1][size - 1];
                EXPECT_FALSE(read_qr(modules)) << text.size() << " bytes, mask " << mask;
            }
    EXPECT_GE(versions.size(), 5u);
    EXPECT_EQ(*versions.rbegin(), 9);
    EXPECT_FALSE(read_qr(Modules(20, std::vector<bool>(20, false))));
}

TEST_F(CommunityTest, AboutNamesTheServerWithItsAddressAndACodeThatOpensIt)
{
    model_->view.tab = 6;                        // About
    for (const unsigned language : {1u, 4u, 8u}) // English, German, Arabic (right to left)
    {
        ptv::set_interface_language(language);
        make_app(ptv::Settings{});
        idle(60);
        ASSERT_EQ(app_->tab(), 6) << language;
        const Modules modules = drawn_code(frame_.scene);
        ASSERT_FALSE(modules.empty()) << language;
        // Right to left the page is mirrored, but the code is not: a phone reads it as made.
        EXPECT_EQ(read_qr(modules), std::optional<std::string>(kInvitation)) << language;
        EXPECT_TRUE(draws_text(frame_.scene, font_set().fonts, "discord.gg/pSqgRU3YP9"))
            << language;
        EXPECT_TRUE(draws_text(frame_.scene, font_set().fonts, "nightly")) << language;
        EXPECT_FALSE(app_->community_open());
    }
}

TEST_F(CommunityTest, TheCardComesOnceAfterTheOpeningAndAnyButtonClosesItForGood)
{
    make_app(ptv::Settings{});
    const int tab = app_->tab();
    app_->play_intro();
    app_->offer_community();
    // Not over the opening.
    idle(120);
    ASSERT_TRUE(app_->intro_playing());
    EXPECT_FALSE(app_->community_open());
    for (int i = 0; i < 300 && !app_->community_open(); ++i)
        frame();
    ASSERT_TRUE(app_->community_open());
    EXPECT_FALSE(app_->intro_playing());
    idle(30);
    const Modules modules = drawn_code(frame_.overlay);
    ASSERT_FALSE(modules.empty());
    EXPECT_EQ(read_qr(modules), std::optional<std::string>(kInvitation));
    EXPECT_TRUE(draws_text(frame_.overlay, font_set().fonts, "discord.gg/pSqgRU3YP9"));
    EXPECT_TRUE(draws_text(frame_.overlay, font_set().fonts, "nightly"));
    EXPECT_FALSE(app_->take_settings_changed());

    // Any button closes it, Square as well as Cross, and does nothing else.
    frame(hui::action_bit(Action::west));
    EXPECT_FALSE(app_->community_open());
    EXPECT_EQ(app_->tab(), tab);
    EXPECT_TRUE(app_->settings().community_seen);
    EXPECT_TRUE(app_->take_settings_changed());
    EXPECT_TRUE(drawn_code(frame_.overlay).empty());

    // Saved, it is not shown again: not at this start, not at a later one.
    ASSERT_TRUE(ptv::save_settings(dir_, app_->settings()));
    const ptv::Settings saved = ptv::load_settings(dir_);
    EXPECT_TRUE(saved.community_seen);
    app_->offer_community();
    idle(120);
    EXPECT_FALSE(app_->community_open());
    make_app(saved);
    app_->play_intro();
    app_->offer_community();
    idle(400);
    EXPECT_FALSE(app_->community_open());
}

TEST_F(CommunityTest, TheCardIsOnlyShownWhenAskedForAndWaitsForAnUpdateOffer)
{
    // A menu that does not ask for it never shows it.
    make_app(ptv::Settings{});
    idle(200);
    EXPECT_FALSE(app_->community_open());

    // An update offered at the start goes first; the card comes once it is answered.
    ptv::platform::UpdateOffer offer;
    offer.installable = true;
    offer.version = "01.002.004";
    offer.installed = "01.002.003";
    offer.available = "01.002.004";
    host::offer_update(offer);
    app_->offer_community();
    idle(120);
    ASSERT_TRUE(app_->update_sheet().is_open());
    EXPECT_FALSE(app_->community_open());
    frame(hui::action_bit(Action::back)); // Later
    idle(30);
    EXPECT_FALSE(app_->update_sheet().is_open());
    for (int i = 0; i < 120 && !app_->community_open(); ++i)
        frame();
    EXPECT_TRUE(app_->community_open());
}

TEST(CommunitySettings, TheClosedCardIsKeptWithTheOtherSettings)
{
    char pattern[] = "/tmp/physalis-community-settings-XXXXXX";
    const std::string dir = mkdtemp(pattern);
    EXPECT_FALSE(ptv::load_settings(dir).community_seen); // no file yet
    ptv::Settings settings;
    settings.volume = 35;
    ASSERT_TRUE(ptv::save_settings(dir, settings));
    EXPECT_FALSE(ptv::load_settings(dir).community_seen);
    settings.community_seen = true;
    ASSERT_TRUE(ptv::save_settings(dir, settings));
    EXPECT_EQ(ptv::load_settings(dir), settings);
    std::error_code error;
    fs::remove_all(dir, error);
}

} // namespace
