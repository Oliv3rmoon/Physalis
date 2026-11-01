// ProsperoTV - Updates on the console: the catalog check and the app replacing itself.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Both are the PS5 Native App Boilerplate's self-update kit (update_kit/, see
// its README.md). Physalis: the check asks GitHub for the newest release of
// this build's repository (TV_UPDATE_REPOSITORY, Oliv3rmoon/Physalis;
// update_kit/github_release.h), not the catalog, which lists ProsperoTV's own
// releases: a release that is no pre-release, carrying PPSA99003.zip and the
// SHA256SUMS that gives its SHA-256, and tagged with a content version higher
// than this build's. The update downloads that ZIP from GitHub over HTTPS and streams it to
// the helper (self-updater.elf in the app's folder), which the app sends to
// the console's payload loader: the helper checks and unpacks the release
// beside the app, and once the app has closed it replaces the app's files and
// posts a notification.
//
// The check runs at launch and then every few hours of uptime, begun only from
// the menu and never during playback, and a version is offered once a launch
// (update_kit/update_schedule.h has the rules and why).

#include "tv_update.hpp"

#include "platform/ps5/system.hpp"
#include "tv/platform.hpp"
#include "tv_build_options.h"
#include "tv_paths.h"
#include "tv_storage.hpp"
#include "tv_update_paths.h"
#include "update_kit/github_release.h"
#include "update_kit/self_update.h"
#include "update_kit/update_check.h"
#include "update_kit/update_schedule.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>

extern "C" const char *tv_self_update_path(int which)
{
    static const std::string helper = tv::storage::app_file("self-updater.elf");
    static const std::string param = tv::storage::app_file("sce_sys/param.json");
    return which == 0 ? helper.c_str() : which == 1 ? param.c_str()
                                                    : tv_data_file("self-update-sequence");
}

namespace
{

using hui::sys::log;

// libcurl and OpenSSL want more stack than a default thread of the console has.
constexpr std::size_t kStackBytes = 1024u * 1024u;
// Where the check asks, and the name the helper's notification gives the app.
constexpr const char *kRepository = TV_UPDATE_REPOSITORY;
constexpr const char *kName = "Physalis";
static_assert(sizeof(TV_UPDATE_REPOSITORY) > 1, "a build asks a repository's releases");

std::mutex g_lock;
update_schedule g_schedule{}; // when a check is due, and the versions offered this launch
bool g_found = false;         // an offer nobody has been told about yet
self_update_check_result g_answer = SELF_UPDATE_UNKNOWN;
self_update_offer g_offer{};
// The offer the menu was last given, the one Update now installs: a later check may find a
// newer release while the dialog is still open.
self_update_check_result g_shown_answer = SELF_UPDATE_UNKNOWN;
self_update_offer g_shown{};
self_update_job g_job{}; // zero until the first begin, as the kit asks
bool g_begun = false;

const char *result_name(self_update_check_result result)
{
    static const char *const names[] = {"available", "up-to-date", "unknown", "untrusted",
                                        "not-installable"};
    return static_cast<unsigned>(result) < 5 ? names[result] : "?";
}

bool read_lines(const std::string &path, std::string *lines, int count)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    char line[700];
    int read = 0;
    while (read < count && std::fgets(line, sizeof(line), file) != nullptr)
    {
        std::size_t length = std::strlen(line);
        while (length != 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' ||
                               line[length - 1] == ' '))
            line[--length] = '\0';
        lines[read++] = line;
    }
    std::fclose(file);
    return read == count;
}

// Test builds: dev/update-offer.txt beside the app replaces GitHub's answer
// (and so skips its SHA256SUMS), so the update can be tried before the
// repository has a newer release. Five lines: the new content version, the
// release's name, its ZIP on GitHub, its SHA-256, its size in bytes; any
// further lines are the release notes.
bool test_offer(self_update_offer *out)
{
    std::string lines[5];
    if (TV_DEV_SCRIPTS == 0 || !read_lines(tv::storage::app_file("dev/update-offer.txt"), lines, 5))
        return false;
    self_update_offer filled{};
    if (!update_check_read_param(tv_self_update_path(1), filled.title, filled.installed))
        return false;
    std::snprintf(filled.name, sizeof(filled.name), "%s", kName);
    std::snprintf(filled.available, sizeof(filled.available), "%s", lines[0].c_str());
    std::snprintf(filled.version, sizeof(filled.version), "%s", lines[1].c_str());
    std::snprintf(filled.artifact, sizeof(filled.artifact), "%s", lines[2].c_str());
    std::snprintf(filled.sha256, sizeof(filled.sha256), "%s", lines[3].c_str());
    filled.size = std::strtoull(lines[4].c_str(), nullptr, 10);
    if (std::FILE *file = std::fopen(tv::storage::app_file("dev/update-offer.txt").c_str(), "rb"))
    {
        std::string notes;
        char line[700];
        for (int number = 0; std::fgets(line, sizeof(line), file) != nullptr; ++number)
            if (number >= 5)
                notes += line;
        std::fclose(file);
        while (!notes.empty() && (notes.back() == '\n' || notes.back() == '\r'))
            notes.pop_back();
        std::snprintf(filled.notes, sizeof(filled.notes), "%s", notes.c_str());
    }
    // Like GitHub's check, only a newer version is offered (content versions
    // compare as text).
    if (std::strcmp(filled.available, filled.installed) <= 0)
        return false;
    *out = filled;
    return true;
}

void *check(void *)
{
    self_update_offer result{};
    github_release_report report{};
    self_update_check_result state = SELF_UPDATE_UNKNOWN;
    // A test build can ask as another title and version (dev/update-as.txt:
    // "PPSA99003 01.000.000"), since the repository's releases carry the
    // released title's ZIP. That release is another app's: it is only announced.
    std::string as[1];
    char title[10] = {};
    char installed[12] = {};
    if (TV_DEV_SCRIPTS != 0 && read_lines(tv::storage::app_file("dev/update-as.txt"), as, 1) &&
        std::sscanf(as[0].c_str(), "%9s %11s", title, installed) == 2)
    {
        state = self_update_check_as_from(kRepository, kName, title, installed, &result, &report);
        if (state == SELF_UPDATE_AVAILABLE)
            state = SELF_UPDATE_NOT_INSTALLABLE;
    }
    else
    {
        state = self_update_check_self_from(kRepository, kName, &result, &report);
    }
    log("[TV] update check: github %s releases: %s (HTTP %d, error %d, redirects %d, tag %s)",
        kRepository, github_release_reason_text(report.reason), report.http_status,
        report.platform_error, report.redirects, report.tag[0] != '\0' ? report.tag : "-");
    if (test_offer(&result))
    {
        log("[TV] update check: dev/update-offer.txt replaces GitHub's answer");
        state = SELF_UPDATE_AVAILABLE;
    }
    log("[TV] update check: result=%s installed=%s available=%s version=%s size=%llu",
        result_name(state), result.installed[0] != '\0' ? result.installed : "-",
        result.available[0] != '\0' ? result.available : "-",
        result.version[0] != '\0' ? result.version : "-",
        static_cast<unsigned long long>(result.size));
    const std::lock_guard guard(g_lock);
    update_schedule_end(&g_schedule);
    if (state != SELF_UPDATE_AVAILABLE && state != SELF_UPDATE_NOT_INSTALLABLE)
        return nullptr;
    // An update under way keeps the offer it is downloading.
    if (g_begun)
        log("[TV] update check: %s found while an update is under way",
            result.available[0] != '\0' ? result.available : "-");
    // A version the viewer has been offered (and skipped) is not asked about again.
    else if (!update_schedule_offer(&g_schedule, result.available))
        log("[TV] update check: %s was offered already this launch",
            result.available[0] != '\0' ? result.available : "-");
    else
    {
        g_answer = state;
        g_offer = result;
        g_found = true;
    }
    return nullptr;
}

ptv::platform::UpdatePhase from_kit(self_update_phase phase)
{
    using ptv::platform::UpdatePhase;
    switch (phase)
    {
    case SELF_UPDATE_STARTING:
        return UpdatePhase::starting;
    case SELF_UPDATE_DOWNLOADING:
        return UpdatePhase::downloading;
    case SELF_UPDATE_UNPACKING:
        return UpdatePhase::unpacking;
    case SELF_UPDATE_READY:
        return UpdatePhase::ready;
    case SELF_UPDATE_APPLYING:
        return UpdatePhase::applying;
    case SELF_UPDATE_CANCELLED:
        return UpdatePhase::cancelled;
    case SELF_UPDATE_FAILED:
        return UpdatePhase::failed;
    default:
        break;
    }
    return UpdatePhase::idle;
}

} // namespace

namespace tv
{

void start_update_check()
{
    const std::int64_t clock = hui::sys::monotonic_us();
    const std::uint64_t now = clock > 0 ? static_cast<std::uint64_t>(clock) : 0;
    {
        const std::lock_guard guard(g_lock);
        // An offer the menu has not taken yet, or an update under way, is left alone.
        if (g_found || g_begun || !update_schedule_due(&g_schedule, now))
            return;
        update_schedule_begin(&g_schedule, now);
    }
    pthread_attr_t attributes;
    bool started = false;
    if (pthread_attr_init(&attributes) == 0)
    {
        pthread_attr_setstacksize(&attributes, kStackBytes);
        pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
        pthread_t thread;
        started = pthread_create(&thread, &attributes, check, nullptr) == 0;
        pthread_attr_destroy(&attributes);
    }
    if (!started)
    {
        // Not again until the next interval.
        log("[TV] update check: the thread could not start");
        const std::lock_guard guard(g_lock);
        update_schedule_end(&g_schedule);
    }
}

} // namespace tv

namespace ptv::platform
{

bool update_take(UpdateOffer *out)
{
    const std::lock_guard guard(g_lock);
    if (!g_found)
        return false;
    g_found = false;
    g_shown = g_offer;
    g_shown_answer = g_answer;
    if (out != nullptr)
    {
        out->installable = g_shown_answer == SELF_UPDATE_AVAILABLE;
        out->version = g_shown.version[0] != '\0' ? g_shown.version : g_shown.available;
        out->installed = g_shown.installed;
        out->available = g_shown.available;
        out->size = g_shown.size;
        out->notes = g_shown.notes;
        out->notes_truncated = g_shown.notes_truncated != 0;
    }
    return true;
}

bool update_begin()
{
    const std::lock_guard guard(g_lock);
    if (g_shown_answer != SELF_UPDATE_AVAILABLE)
        return false;
    if (g_begun)
        self_update_finish(&g_job);
    g_begun = self_update_start(&g_job, self_update_console(), &g_shown) == 1;
    log("[TV] update: %s %s", g_begun ? "downloading" : "could not begin", g_shown.available);
    return g_begun;
}

UpdateProgress update_poll()
{
    UpdateProgress progress;
    const std::lock_guard guard(g_lock);
    if (!g_begun)
        return progress;
    self_update_status status{};
    self_update_poll(&g_job, &status);
    progress.phase = from_kit(status.phase);
    progress.done = status.done;
    progress.total = status.total;
    progress.time_left = status.time_left;
    progress.error = status.error;
    return progress;
}

void update_cancel()
{
    const std::lock_guard guard(g_lock);
    if (g_begun)
        self_update_cancel(&g_job);
}

bool update_apply()
{
    const std::lock_guard guard(g_lock);
    if (!g_begun)
        return false;
    const bool going = self_update_apply(&g_job) == 1;
    log("[TV] update: %s", going ? "staged; the helper replaces the files once the app has closed"
                                 : "the helper did not take the go-ahead");
    return going;
}

void update_finish()
{
    const std::lock_guard guard(g_lock);
    if (!g_begun)
        return;
    self_update_finish(&g_job);
    g_begun = false;
}

} // namespace ptv::platform
