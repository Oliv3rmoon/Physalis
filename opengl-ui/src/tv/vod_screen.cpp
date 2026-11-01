// ProsperoTV - A virtual list: only the visible movies or episodes are drawn.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/i18n.hpp"
#include "tv/vod_screen.hpp"
#include "tv/category_path.hpp"
#include "tv/draw.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <cstdlib>
#include <iterator>

namespace ptv
{
namespace
{
constexpr int kRows = 7;
// The first is the default: the provider's newest additions, or a show's episodes in order.
constexpr const char *kSorts[] = {"Recently added", "Title A-Z", "Title Z-A", "Newest year",
                                  "Highest rating"};
constexpr int kSortCount = static_cast<int>(std::size(kSorts));
constexpr int kTitleOrder = 1; // "Title A-Z"
// The letters stand in a column between the list and the artwork, as on Live TV.
constexpr float kLetterX = kMargin + 1060.0f + 52.0f;
constexpr float kLetterTop = 318.0f; // the centre of '#'
constexpr float kLetterPitch = 18.5f;
constexpr float kLetterText = 15.0f;
} // namespace
bool VodScreen::has_letters() const
{
    const auto kind = shared_.model.view.vod_kind;
    return (kind == static_cast<int>(VodKind::movies) ||
            kind == static_cast<int>(VodKind::series)) &&
           std::any_of(letter_starts_.begin(), letter_starts_.end(),
                       [](int start) { return start >= 0; });
}
int VodScreen::current_letter() const
{
    const auto *row = focused();
    if (row && !row->key.empty() && letter_starts_[letter_of_key(row->key)] >= 0)
        return letter_of_key(row->key);
    for (int letter = 0; letter < kLetterCount; ++letter)
        if (letter_starts_[letter] >= 0)
            return letter;
    return 0;
}
void VodScreen::enter_letters()
{
    auto &view = shared_.model.view;
    const auto row = *focused();
    // A letter leads to one run of titles only in title order: from the newest first (or
    // any other order) the list turns to Title A-Z, and the focus stays where it was.
    if (view.vod_sort != kTitleOrder)
    {
        view.vod_sort = kTitleOrder;
        sync();
        const auto kept = std::find_if(rows_.begin(), rows_.end(),
                                       [&](const Row &candidate)
                                       {
                                           return candidate.kind == row.kind &&
                                                  candidate.item == row.item &&
                                                  candidate.category == row.category;
                                       });
        if (kept != rows_.end())
            view.vod_focus = static_cast<int>(kept - rows_.begin());
    }
    if (focused()->key.empty() ||
        (focused()->kind == Kind::category && shared_.model.category_hidden(focused()->category)))
        view.vod_focus = letter_starts_[current_letter()];
    top_ = std::clamp(view.vod_focus, 0, std::max(0, static_cast<int>(rows_.size()) - kRows));
    pages_.stop();
    letters_focused_ = true;
}
void VodScreen::enter()
{
    letters_focused_ = false;
    const auto &view = shared_.model.view;
    if (view.vod_kind >= 0)
        shared_.model.vod().select(static_cast<VodKind>(view.vod_kind), view.vod_series);
    sync();
}
bool VodScreen::shows_a_title(const Row &row)
{
    return row.kind == Kind::item || row.kind == Kind::resume;
}
const VodScreen::Row *VodScreen::focused() const
{
    const int focus = shared_.model.view.vod_focus;
    return focus >= 0 && static_cast<std::size_t>(focus) < rows_.size() ? &rows_[focus] : nullptr;
}
void VodScreen::sync()
{
    auto &model = shared_.model;
    auto &view = model.view;
    const auto &vod = model.vod();
    model.set_vod_overview(view.vod_kind < 0);
    rows_.clear();
    if (view.vod_kind < 0)
        rows_ = {{Kind::movies, tr("Movies"), {}, 0, model.vod_count(VodKind::movies)},
                 {Kind::shows, tr("TV shows"), {}, 0, model.vod_count(VodKind::series)}};
    else
    {
        const auto &catalog = vod.catalog();
        std::map<std::string, std::string> categories;
        std::vector<Row> items;
        for (unsigned i = 0; i < catalog.size(); ++i)
        {
            const auto item = catalog[i];
            if (!model.vod_allowed(i))
                continue;
            if (!view.vod_category.empty() &&
                !category_belongs(item.group_title, view.vod_category))
                continue;
            const auto &details = vod.details(item.id);
            const auto genres = vod_genres(details.genre);
            if ((!view.vod_genre.empty() &&
                 std::find(genres.begin(), genres.end(), view.vod_genre) == genres.end()) ||
                (!view.vod_year.empty() && details.year != view.vod_year))
                continue;
            const bool searching =
                !view.vod_query.empty() || !view.vod_genre.empty() || !view.vod_year.empty();
            if (!searching && !view.vod_all)
            {
                auto child = category_trim(item.group_title);
                while (!child.empty() && category_parent(child) != view.vod_category)
                    child = category_parent(child);
                if (!child.empty() && child != view.vod_category)
                {
                    categories.emplace(std::string(child), std::string(category_leaf(child)));
                    continue;
                }
            }
            if (!model.category_hidden(item.group_title) &&
                contains_nocase(item.name, view.vod_query))
                items.push_back({Kind::item, std::string(item.name), {}, i});
        }
        const bool plain = !view.vod_all && view.vod_query.empty() && view.vod_genre.empty() &&
                           view.vod_year.empty();
        // Where the show was left, above its seasons.
        const auto resume = plain && view.vod_category.empty() &&
                                    view.vod_kind == static_cast<int>(VodKind::episodes)
                                ? model.series_resume()
                                : std::nullopt;
        if (resume)
        {
            auto label =
                std::string(tr("Continue")) + " · " + std::string(catalog[resume->index].name);
            if (resume->position_us)
                label += " (" + std::to_string(resume->position_us / 60000000) +
                         tr(" min watched") + ")";
            rows_.push_back({Kind::resume, std::move(label), {}, resume->index});
        }
        if (plain && !catalog.empty())
            rows_.push_back({Kind::all, tr("Browse all"), {}, 0});
        std::vector<std::pair<std::string, std::string>> ordered(categories.begin(),
                                                                 categories.end());
        if (view.vod_kind == static_cast<int>(VodKind::episodes))
            std::sort(ordered.begin(), ordered.end(),
                      [](const auto &left, const auto &right)
                      {
                          return left.first.size() != right.first.size()
                                     ? left.first.size() < right.first.size()
                                     : left.first < right.first;
                      });
        for (const auto &[path, name] : ordered)
            rows_.push_back({Kind::category, name, path, 0});
        const bool episodes = view.vod_kind == static_cast<int>(VodKind::episodes);
        std::stable_sort(items.begin(), items.end(),
                         [&](const Row &left, const Row &right)
                         {
                             const auto &a = vod.details(catalog[left.item].id);
                             const auto &b = vod.details(catalog[right.item].id);
                             // Newest first, and titles added together keep the provider's
                             // order. Titles without a date (0) follow by title, so a playlist,
                             // which gives no dates, is listed by title as before.
                             if (view.vod_sort == 0 && !episodes)
                             {
                                 const auto older = vod.added(left.item);
                                 const auto newer = vod.added(right.item);
                                 if (older != newer)
                                     return older > newer;
                                 if (older)
                                     return false;
                             }
                             if (view.vod_sort == 2)
                                 return left.text > right.text;
                             if (view.vod_sort == 3 && a.year != b.year)
                                 return a.year > b.year;
                             if (view.vod_sort == 4 && std::strtof(a.rating.c_str(), nullptr) !=
                                                           std::strtof(b.rating.c_str(), nullptr))
                                 return std::strtof(a.rating.c_str(), nullptr) >
                                        std::strtof(b.rating.c_str(), nullptr);
                             if (episodes && view.vod_sort != 1)
                                 return catalog[left.item].source_line <
                                        catalog[right.item].source_line;
                             return left.text < right.text;
                         });
        rows_.insert(rows_.end(), std::make_move_iterator(items.begin()),
                     std::make_move_iterator(items.end()));
    }
    // The letters of Live TV (sort_key): accents put aside, digits and other scripts under '#'.
    // In title order the categories and titles are one alphabet, so that each letter is one
    // run; Browse all and Continue stay above it.
    letter_starts_.fill(-1);
    if (view.vod_kind == static_cast<int>(VodKind::movies) ||
        view.vod_kind == static_cast<int>(VodKind::series))
    {
        for (auto &row : rows_)
            if (row.kind == Kind::item || row.kind == Kind::category)
            {
                iptv::ChannelView named{};
                named.name = row.text;
                row.key = sort_key(named);
            }
        if (view.vod_sort == kTitleOrder)
            std::stable_sort(rows_.begin(), rows_.end(),
                             [](const Row &left, const Row &right)
                             {
                                 if (left.key.empty() || right.key.empty())
                                     return left.key.empty() && !right.key.empty();
                                 return left.key < right.key;
                             });
        // A hidden category is listed, to be shown again, but no letter leads to it.
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
            if (!rows_[i].key.empty() &&
                (rows_[i].kind != Kind::category || !model.category_hidden(rows_[i].category)))
            {
                auto &start = letter_starts_[letter_of_key(rows_[i].key)];
                if (start < 0)
                    start = i;
            }
    }
    if (!has_letters())
        letters_focused_ = false;
    if (details_open_)
    {
        const auto found = std::find_if(
            rows_.begin(), rows_.end(), [&](const Row &row)
            { return row.kind == Kind::item && vod.catalog()[row.item].id == detail_item_id_; });
        if (found == rows_.end())
            details_open_ = false;
        else
            view.vod_focus = static_cast<int>(found - rows_.begin());
    }
    view.vod_focus = std::clamp(view.vod_focus, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
    top_ = std::max(0, view.vod_focus - kRows + 1);
    revision_ = vod.revision();
    model_revision_ = model.revision();
    counts_revision_ = model.search_revision();
}
void VodScreen::update()
{
    shared_.model.set_vod_overview(shared_.model.view.vod_kind < 0);
    if (revision_ != shared_.model.vod().revision() ||
        model_revision_ != shared_.model.revision() ||
        counts_revision_ != shared_.model.search_revision())
        sync();
}
void VodScreen::refresh()
{
    if (shared_.model.active_source() == iptv::SourceKind::Custom)
    {
        shared_.model.refresh();
        return;
    }
    const auto &view = shared_.model.view;
    if (view.vod_kind < 0)
    {
        shared_.model.set_vod_overview(true, true);
        sync();
        return;
    }
    if (view.vod_kind >= 0)
        shared_.model.vod().select(static_cast<VodKind>(view.vod_kind), view.vod_series, true);
}
bool VodScreen::back()
{
    auto &view = shared_.model.view;
    if (details_open_)
    {
        details_open_ = false;
        return true;
    }
    if (filters_open_)
    {
        filters_open_ = false;
        return true;
    }
    if (letters_focused_)
    {
        letters_focused_ = false;
        return true;
    }
    if (!view.vod_query.empty())
        view.vod_query.clear();
    else if (view.vod_all)
        view.vod_all = false;
    else if (!view.vod_category.empty())
        view.vod_category = category_parent(view.vod_category);
    else if (view.vod_kind == static_cast<int>(VodKind::episodes))
    {
        view.vod_kind = static_cast<int>(VodKind::series);
        view.vod_series.clear();
        view.vod_series_name.clear();
        view.vod_series_cover.clear();
        shared_.model.vod().select(VodKind::series);
    }
    else if (view.vod_kind >= 0)
    {
        view.vod_kind = -1;
        shared_.model.vod().stop();
    }
    else
        return false;
    view.vod_focus = 0;
    sync();
    return true;
}
void VodScreen::handle(const InputFrame &input, ui::Feedback &feedback)
{
    auto &model = shared_.model;
    auto &view = model.view;
    if (filters_open_)
    {
        handle_filters(input);
        return;
    }
    if (details_open_)
    {
        if (input.nav == Direction::down)
            detail_scroll_ = std::min(2400.0f, detail_scroll_ + 80);
        if (input.nav == Direction::up)
            detail_scroll_ = std::max(0.0f, detail_scroll_ - 80);
        if (input.is_pressed(Action::west))
        {
            details_open_ = false;
            return;
        }
        if (!input.is_pressed(Action::confirm))
            return;
        details_open_ = false;
    }
    if (input.is_pressed(Action::r3) && view.vod_kind >= 0)
    {
        letters_focused_ = false;
        filters_open_ = true;
        filter_focus_ = 0;
        return;
    }
    // On the letters: up and down go to the next letter that has titles, Left or Cross back
    // to the list (Circle too, in back()).
    if (letters_focused_)
    {
        if (input.nav == Direction::left || input.is_pressed(Action::confirm))
        {
            letters_focused_ = false;
            feedback.play(audio::Cue::focus, 0.97f);
        }
        else if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int step = input.nav == Direction::down ? 1 : -1;
            int letter = current_letter() + step;
            while (letter >= 0 && letter < kLetterCount && letter_starts_[letter] < 0)
                letter += step;
            if (letter >= 0 && letter < kLetterCount)
            {
                view.vod_focus = letter_starts_[letter];
                // The letter's first title goes to the top, so that its others follow it.
                top_ = std::clamp(view.vod_focus, 0,
                                  std::max(0, static_cast<int>(rows_.size()) - kRows));
                feedback.play(audio::Cue::focus, 1.1f - 0.008f * static_cast<float>(letter));
            }
            else if (!input.nav_repeat)
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        }
        return;
    }
    if (input.is_pressed(Action::west) && focused() && focused()->kind == Kind::item)
    {
        details_open_ = true;
        detail_item_id_ = model.vod().catalog()[focused()->item].id;
        detail_scroll_ = 0;
        model.vod().request_details(focused()->item);
        return;
    }
    if (input.is_pressed(Action::touch) && focused() && focused()->kind == Kind::category &&
        view.vod_kind != static_cast<int>(VodKind::episodes))
    {
        const auto category = focused()->category;
        if (!model.parental().enabled())
            model.announce(Level::warning, tr("Set a parent PIN in Settings first"), "", 3);
        else if (!model.set_category_rule(category, (model.category_rule(category, false) + 1) % 3))
            feedback.play(audio::Cue::error);
        sync();
        return;
    }
    if (input.is_pressed(Action::west) && focused() && focused()->kind == Kind::category &&
        view.vod_kind != static_cast<int>(VodKind::episodes))
    {
        const auto category = focused()->category;
        if (!model.hide_category(category, !model.category_hidden(category)))
            feedback.play(audio::Cue::error);
        sync();
        return;
    }
    if (input.is_pressed(Action::north) && view.vod_kind >= 0)
    {
        model.ask_vod_query();
        return;
    }
    // Only a press of its own leads onto the letters, as on Live TV.
    if (input.nav == Direction::right && !input.nav_repeat && has_letters())
    {
        enter_letters();
        feedback.play(audio::Cue::focus, 1.03f);
        return;
    }
    const auto old = view.vod_focus;
    // Held, Up and Down go faster the longer they are held (HeldSteps).
    if (input.nav == Direction::up)
        view.vod_focus -= steps_.rows(input);
    if (input.nav == Direction::down)
        view.vod_focus += steps_.rows(input);
    // L2 and R2 turn a page, and keep turning while they are held (PageTurns).
    const double now =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    view.vod_focus += pages_.turn(input, now) * kRows;
    view.vod_focus = std::clamp(view.vod_focus, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
    if (old != view.vod_focus)
        feedback.play(audio::Cue::focus);
    if (view.vod_focus < top_)
        top_ = view.vod_focus;
    if (view.vod_focus >= top_ + kRows)
        top_ = view.vod_focus - kRows + 1;
    if (!input.is_pressed(Action::confirm) || !focused() || !model.vod().available())
        return;
    letters_focused_ = false;
    const auto row = *focused();
    if (row.count == 0)
        return;
    if (row.kind == Kind::movies || row.kind == Kind::shows)
    {
        view.vod_genre.clear();
        view.vod_year.clear();
        view.vod_sort = 0;
        view.vod_kind =
            static_cast<int>(row.kind == Kind::movies ? VodKind::movies : VodKind::series);
        model.vod().select(static_cast<VodKind>(view.vod_kind));
    }
    else if (row.kind == Kind::resume)
    {
        if (model.play_series_resume())
            feedback.play(audio::Cue::select);
        return;
    }
    else if (row.kind == Kind::category)
        view.vod_category = row.category;
    else if (row.kind == Kind::all)
        view.vod_all = true;
    else if (view.vod_kind == static_cast<int>(VodKind::series))
    {
        if (!model.vod_allowed(row.item))
            return;
        const auto show = model.vod().catalog()[row.item];
        view.vod_genre.clear();
        view.vod_year.clear();
        view.vod_sort = 0;
        view.vod_series = show.tvg_id;
        view.vod_series_name = show.name;
        view.vod_series_cover = show.tvg_logo;
        view.vod_series_group = show.group_title;
        view.vod_series_adult = show.adult;
        view.vod_kind = static_cast<int>(VodKind::episodes);
        view.vod_category.clear();
        view.vod_query.clear();
        view.vod_all = false;
        model.vod().select(VodKind::episodes, view.vod_series);
    }
    else
    {
        if (model.play_vod(row.item))
            feedback.play(audio::Cue::select);
        return;
    }
    feedback.play(audio::Cue::select);
    view.vod_focus = 0;
    sync();
}
std::vector<std::string> VodScreen::image_urls() const
{
    const auto *row = focused();
    if (row && shows_a_title(*row))
    {
        const auto item = shared_.model.vod().catalog()[row->item];
        const auto &details = shared_.model.vod().details(item.id);
        const auto image =
            details.poster.empty() ? item.tvg_logo : std::string_view(details.poster);
        return {image.empty() ? shared_.model.view.vod_series_cover : std::string(image)};
    }
    return {};
}
void VodScreen::draw(ui::Canvas &canvas) const
{
    const auto &model = shared_.model;
    const auto &view = model.view;
    const auto &theme = shared_.theme;
    const auto &fonts = canvas.fonts;
    auto &draw = canvas.list;
    const std::string heading = view.vod_kind < 0    ? tr("On demand")
                                : view.vod_kind == 0 ? tr("Movies")
                                : view.vod_kind == 1 ? tr("TV shows")
                                                     : view.vod_series_name;
    ui::text(draw, fonts.semibold, tr("YOUR PROVIDER'S LIBRARY"), kMargin, 162, 18, tone::accent,
             gfx::Align::left, 4);
    const auto &face = title_face(fonts, heading);
    ui::text(draw, face, face.font->fit(readable(face, heading), 54, kWidth - 2 * kMargin), kMargin,
             224, 54, theme.text);
    const std::string location =
        view.vod_category.empty() ? tr("All categories") : view.vod_category;
    const std::string breadcrumb =
        location + (view.vod_query.empty() ? "" : tr(" / Search: ") + view.vod_query) +
        (view.vod_genre.empty() ? "" : " / " + view.vod_genre) +
        (view.vod_year.empty() ? "" : " / " + view.vod_year);
    const auto &breadcrumb_face = face_for(fonts, fonts.regular, breadcrumb);
    ui::text(draw, breadcrumb_face,
             breadcrumb_face.font->fit(readable(breadcrumb_face, breadcrumb), 23, 1100), kMargin,
             274, 23, theme.text_muted);
    for (int i = top_; i < static_cast<int>(rows_.size()) && i < top_ + kRows; ++i)
    {
        const auto &row = rows_[i];
        const float y = 305 + static_cast<float>(i - top_) * 85;
        const bool selected = i == view.vod_focus;
        const bool disabled = row.count == 0;
        draw.bordered_rect({kMargin, y, 1060, 76}, 14,
                           kWhite.with_alpha(disabled   ? 0.02f
                                             : selected ? 0.16f
                                                        : 0.04f),
                           selected ? 2 : 0, tone::cream);
        auto label = row.text + (row.kind == Kind::category && model.category_hidden(row.category)
                                     ? tr(" (Hidden)")
                                     : "");
        if (row.kind == Kind::movies || row.kind == Kind::shows)
            label += " (" +
                     (row.count                ? group_digits(*row.count)
                      : model.search_loading() ? std::string(tr("Loading…"))
                                               : std::string("—")) +
                     ")";
        if (row.kind == Kind::category)
        {
            const int rule = model.category_rule(row.category);
            if (rule == 1)
                label += tr(" (PIN required)");
            if (rule == 2)
                label += tr(" (Approved for kids)");
        }
        const auto &font = face_for(fonts, fonts.semibold, label);
        const auto shown = readable(font, label);
        ui::text(draw, font, font.font->fit(shown.empty() ? tr("Untitled") : shown, 27, 960),
                 kMargin + 24, y + 47, 27, disabled ? theme.text_muted : theme.text);
        const bool opens = row.kind == Kind::item ? view.vod_kind == 1 : row.kind != Kind::resume;
        if (!disabled && opens)
            ui::text(draw, fonts.regular, ">", kMargin + 1020, y + 47, 27, tone::accent);
    }
    if (rows_.empty())
        ui::text(draw, fonts.regular,
                 model.vod().busy() || model.vod().requested() ? tr("Loading…")
                                                               : tr("No matching titles"),
                 kMargin, 370, 30, theme.text_muted);
    const auto *row = focused();
    if (row && shows_a_title(*row))
    {
        const auto item = model.vod().catalog()[row->item];
        const Rect cover{1260, 310, 350, 480};
        draw.rounded_rect(cover, 20, kWhite.with_alpha(0.05f));
        const auto &details = model.vod().details(item.id);
        const auto poster =
            details.poster.empty() ? item.tvg_logo : std::string_view(details.poster);
        const auto image =
            shared_.images.find(poster.empty() ? std::string_view(view.vod_series_cover) : poster);
        if (image.id)
        {
            const float scale = std::min(cover.w / image.width, cover.h / image.height);
            const float width = image.width * scale, height = image.height * scale;
            draw.image(image.id, {cover.cx() - width / 2, cover.cy() - height / 2, width, height},
                       {0, 0, 1, 1}, kWhite);
        }
        else
        {
            iptv::Channel named;
            named.name = view.vod_kind == static_cast<int>(VodKind::episodes)
                             ? view.vod_series_name
                             : std::string(item.name);
            const auto letters = monogram(named);
            const auto &letter_face = face_for(fonts, fonts.display, letters);
            ui::text(draw, letter_face, readable(letter_face, letters), cover.cx(), cover.cy() + 25,
                     76, tone::accent, gfx::Align::center);
        }
        const auto &category_face = face_for(fonts, fonts.regular, item.group_title);
        // Right of the letters when they are there.
        ui::paragraph(draw, category_face, readable(category_face, item.group_title),
                      has_letters() ? 1260.0f : 1180.0f, 840, 24, has_letters() ? 540.0f : 600.0f,
                      34, theme.text_muted, 3);
    }
    else
        ui::paragraph(draw, fonts.regular,
                      tr("Browse movies or choose a show, then its season and episode."),
                      has_letters() ? 1260.0f : 1200.0f, 365, 28, has_letters() ? 510.0f : 570.0f,
                      42, theme.text_muted, 7);
    ui::text(draw, fonts.regular,
             fonts.regular.font->fit(model.vod().status(), 22, kWidth - 2 * kMargin), kMargin, 949,
             22, theme.text_muted);
    if (has_letters())
        draw_letters(canvas);
    if (details_open_)
        draw_details(canvas);
    if (filters_open_)
        draw_filters(canvas);
}
void VodScreen::draw_letters(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &fonts = canvas.fonts;
    const int selected = current_letter();
    const float y = kLetterTop + static_cast<float>(selected) * kLetterPitch;
    // The plate of the letter in use, as on Live TV: orange while the list has the focus;
    // cream, larger and lit once the letters have it.
    const float radius = letters_focused_ ? 18.0f : 11.5f;
    if (letters_focused_)
        list.glow({kLetterX - radius, y - radius, 2.0f * radius, 2.0f * radius}, radius, 24.0f,
                  tone::accent.with_alpha(0.7f));
    list.circle(kLetterX, y, radius, letters_focused_ ? tone::cream : tone::accent);
    for (int letter = 0; letter < kLetterCount; ++letter)
    {
        const char text[2] = {letter_char(letter), '\0'};
        const bool here = letter == selected;
        // With the focus on them the letters make room around the one in use, which is
        // written larger.
        const float away = static_cast<float>(letter - selected);
        const float push = letters_focused_ && !here
                               ? std::copysign(std::exp(-(std::abs(away) - 1.0f) * 0.9f), away)
                               : 0.0f;
        const float cy = kLetterTop + static_cast<float>(letter) * kLetterPitch + 7.0f * push;
        const float size = kLetterText + (letters_focused_ && here ? 7.0f : 0.0f);
        // Letters with nothing under them stay, dimmed: the column keeps its shape.
        const Color color = here                          ? tone::ink
                            : letter_starts_[letter] >= 0 ? shared_.theme.text.with_alpha(0.88f)
                                                          : kWhite.with_alpha(0.2f);
        ui::text(list, fonts.semibold, text, kLetterX, baseline_for(cy, size), size, color,
                 gfx::Align::center);
    }
}
int VodScreen::hints(ui::Hint *out, int capacity) const
{
    if (letters_focused_)
    {
        const ui::Hint letters[] = {{ui::Button::dpad, tr("Jump to a letter")},
                                    {ui::Button::cross, tr("List")},
                                    {ui::Button::circle, tr("Back")}};
        const int count = std::min(capacity, static_cast<int>(std::size(letters)));
        std::copy_n(letters, count, out);
        return count;
    }
    const ui::Hint hints[] = {{ui::Button::cross, tr("Open / Watch")},
                              {ui::Button::right_stick, tr("Filter / Sort")},
                              {ui::Button::triangle, tr("Search")},
                              {ui::Button::options, tr("Update")},
                              {ui::Button::circle, tr("Back")}};
    const int first = focused() && focused()->count == 0 ? 1 : 0;
    int count = std::min(capacity, 5 - first);
    std::copy_n(hints + first, count, out);
    if (count < capacity && focused() && focused()->kind == Kind::item)
        out[count++] = {ui::Button::square, tr("Details")};
    if (count < capacity && focused() && focused()->kind == Kind::category &&
        shared_.model.view.vod_kind != static_cast<int>(VodKind::episodes))
    {
        out[count++] = {ui::Button::square, tr("Show / Hide")};
        if (count < capacity)
            out[count++] = {ui::Button::touchpad, tr("Normal / PIN / Kids")};
    }
    if (count<capacity &&static_cast<int>(rows_.size())> kRows)
        out[count++] = {ui::Button::l2, tr("Page"), ui::Button::r2};
    return count;
}
unsigned VodScreen::result_count() const
{
    return static_cast<unsigned>(std::count_if(rows_.begin(), rows_.end(), [](const Row &row)
                                               { return row.kind == Kind::item; }));
}
std::vector<std::string> VodScreen::result_titles() const
{
    std::vector<std::string> titles;
    for (const auto &row : rows_)
        if (row.kind == Kind::item)
            titles.push_back(row.text);
    return titles;
}
void VodScreen::handle_filters(const InputFrame &input)
{
    auto &view = shared_.model.view;
    if (input.is_pressed(Action::confirm) || input.is_pressed(Action::r3))
    {
        filters_open_ = false;
        return;
    }
    if (input.nav == Direction::up)
        filter_focus_ = std::max(0, filter_focus_ - 1);
    if (input.nav == Direction::down)
        filter_focus_ = std::min(2, filter_focus_ + 1);
    if (input.is_pressed(Action::west))
    {
        view.vod_genre.clear();
        view.vod_year.clear();
        view.vod_sort = 0;
        view.vod_focus = 0;
        sync();
        return;
    }
    const int direction = input.nav == Direction::right ? 1 : input.nav == Direction::left ? -1 : 0;
    if (!direction)
        return;
    if (filter_focus_ == 2)
        view.vod_sort = (view.vod_sort + direction + kSortCount) % kSortCount;
    else
    {
        std::set<std::string> facets;
        const auto &vod = shared_.model.vod();
        for (unsigned i = 0; i < vod.catalog().size(); ++i)
        {
            if (!shared_.model.vod_allowed(i))
                continue;
            const auto &details = vod.details(vod.catalog()[i].id);
            if (filter_focus_ == 0)
            {
                for (auto &genre : vod_genres(details.genre))
                    if (facets.size() < 256)
                        facets.insert(std::move(genre));
            }
            else if (!details.year.empty())
                facets.insert(details.year);
        }
        std::vector<std::string> choices{std::string()};
        choices.insert(choices.end(), facets.begin(), facets.end());
        auto &value = filter_focus_ == 0 ? view.vod_genre : view.vod_year;
        const auto found = std::find(choices.begin(), choices.end(), value);
        int at = found == choices.end() ? 0 : static_cast<int>(found - choices.begin());
        at = (at + direction + static_cast<int>(choices.size())) % static_cast<int>(choices.size());
        value = choices[at];
    }
    view.vod_focus = 0;
    sync();
}
void VodScreen::draw_filters(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &view = shared_.model.view;
    list.rounded_rect({220, 190, 1480, 650}, 24, tone::panel);
    ui::text(list, canvas.fonts.semibold, tr("Filter and sort"), 280, 265, 38, kWhite);
    const int sort = std::clamp(view.vod_sort, 0, kSortCount - 1);
    const bool episodes = view.vod_kind == static_cast<int>(VodKind::episodes);
    const std::string values[] = {
        std::string(tr("Genre")) + ": " + (view.vod_genre.empty() ? tr("All") : view.vod_genre),
        std::string(tr("Year")) + ": " + (view.vod_year.empty() ? tr("All") : view.vod_year),
        std::string(tr("Sort")) + ": " +
            tr(sort == 0 && episodes ? "Episode order" : kSorts[sort])};
    for (int i = 0; i < 3; ++i)
    {
        const float y = 310 + i * 105.0f;
        list.bordered_rect({270, y, 1380, 86}, 12, kWhite.with_alpha(0.05f),
                           filter_focus_ == i ? 2 : 0, tone::accent);
        const auto &face = face_for(canvas.fonts, canvas.fonts.regular, values[i]);
        ui::text(list, face, face.font->fit(readable(face, values[i]), 28, 1280), 300, y + 54, 28,
                 kWhite);
    }
    const ui::Hint hints[] = {{ui::Button::dpad, tr("Change")},
                              {ui::Button::square, tr("Reset")},
                              {ui::Button::cross, tr("Close")}};
    ui::HintLayout layout;
    layout.size = 32.0f;
    layout.text_size = 24.0f;
    layout.item_gap = 34.0f;
    layout.cy = 702.0f;
    draw_hint_row(list, canvas.fonts, hints, 3, 280, 1360, layout);
    ui::text(list, canvas.fonts.regular,
             tr("Filters use loaded metadata. Open a title to fetch missing details."), 280, 770,
             22, shared_.theme.text_muted);
}
void VodScreen::draw_details(ui::Canvas &canvas) const
{
    const auto *row = focused();
    if (!row || row->kind != Kind::item)
        return;
    const auto &model = shared_.model;
    const auto item = model.vod().catalog()[row->item];
    const auto &details = model.vod().details(item.id);
    auto &list = canvas.list;
    list.rounded_rect({90, 120, 1740, 815}, 24, tone::panel);
    const auto &title = face_for(canvas.fonts, canvas.fonts.semibold, item.name);
    ui::text(list, title, title.font->fit(readable(title, item.name), 36, 1610), 140, 183, 36,
             kWhite);
    const std::string summary =
        details.year + (details.genre.empty() ? "" : "  /  " + details.genre) +
        (details.rating.empty()
             ? ""
             : std::string("  /  ") + tr("Rating") + " " + details.rating + " / 10");
    const auto &summary_face = face_for(canvas.fonts, canvas.fonts.regular, summary);
    ui::text(list, summary_face, summary_face.font->fit(readable(summary_face, summary), 24, 1560),
             140, 230, 24, tone::accent);
    const auto urls = image_urls();
    const auto image = urls.empty() ? ImageTexture{} : shared_.images.find(urls.front());
    const Rect cover{140, 270, 320, 480};
    if (image.id)
    {
        const float scale = std::min(cover.w / image.width, cover.h / image.height);
        const float w = image.width * scale, h = image.height * scale;
        list.image(image.id, {cover.cx() - w / 2, cover.cy() - h / 2, w, h}, {0, 0, 1, 1}, kWhite);
    }
    list.push_clip({510, 270, 1240, 540});
    float y = 302 - detail_scroll_;
    const std::string plot = !details.plot.empty() ? details.plot
                             : model.vod().details_loading(item.id)
                                 ? tr("Loading title details...")
                                 : tr("No synopsis supplied by the provider.");
    const auto &plot_face = face_for(canvas.fonts, canvas.fonts.regular, plot);
    y = ui::paragraph(list, plot_face, readable(plot_face, plot), 510, y, 27, 1210, 39, kWhite,
                      200) +
        35;
    if (!details.cast.empty())
    {
        const std::string cast = std::string(tr("Cast")) + ": " + details.cast;
        const auto &cast_face = face_for(canvas.fonts, canvas.fonts.regular, cast);
        ui::paragraph(list, cast_face, readable(cast_face, cast), 510, y, 24, 1210, 36,
                      shared_.theme.text_muted, 100);
    }
    list.pop_clip();
    const ui::Hint hints[] = {{ui::Button::dpad, tr("Scroll")},
                              {ui::Button::cross, tr("Open / Watch")},
                              {ui::Button::circle, tr("Back")}};
    ui::HintLayout layout;
    layout.size = 32.0f;
    layout.text_size = 23.0f;
    layout.item_gap = 34.0f;
    layout.cy = 858.0f;
    draw_hint_row(list, canvas.fonts, hints, 3, 140, 1640, layout);
    ui::text(list, canvas.fonts.regular,
             canvas.fonts.regular.font->fit(model.vod().status(), 20, 1570), 140, 905, 20,
             shared_.theme.text_muted);
}
} // namespace ptv
