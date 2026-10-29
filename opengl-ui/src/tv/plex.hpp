// Physalis - Plex as a source: sign-in on the TV, servers, libraries and playback.
// Copyright (C) 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Plex API as PlxNative (https://github.com/GLinnik21/plx-native, GPL-3.0, a native Plex
// client for LG webOS by GLinnik21) documents and uses it: the plex.tv link PIN, the server
// list and how to rank a server's connections, the library listings, the universal
// transcoder's parameters and the timeline. No PlxNative code is used; its docs and code were
// the reference for what Plex expects.
//
// Everything here takes a Transport, so the tests answer for plex.tv and the server and
// nothing reaches a network. Tokens travel in X-Plex-Token headers only, never in a URL, and
// nothing here writes a token (or an account's name) to the diagnostic log.

#pragma once

#include "iptv_http.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ptv::plex
{

inline constexpr char kPlexTv[] = "https://plex.tv";
// Where a person types the code. The QR code on the TV opens it.
inline constexpr char kLinkUrl[] = "https://plex.tv/link";

// What this app says it is, on every request. The client identifier is this console's, made
// once and kept with the app's data; plex.tv lists the console by it among the account's devices.
struct Identity
{
    std::string client_id;
    std::string product = "Physalis";
    std::string version = "0";
    // Plex's transcoder answers 400 without a platform it knows; "Generic" is one (PlxNative).
    std::string platform = "Generic";
    std::string device = "PlayStation 5";
    std::string device_name = "Physalis (PS5)";
};
// "Name: value" lines: Accept JSON, the identity, and the token when there is one.
std::vector<std::string> headers(const Identity &identity, std::string_view token = {});
// A new identifier, in the form of a UUID.
std::string new_client_id();
bool valid_client_id(std::string_view id);
bool valid_token(std::string_view token);

struct Request
{
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headers;
    // "host:port:address": a *.plex.direct name dialled at the address plex.tv gave for it, so
    // https works behind a router that refuses DNS answers naming private addresses.
    std::vector<std::string> resolve;
    unsigned connect_ms = 5000;
    unsigned timeout_ms = 15000;
    // Stops this request (as well as the transport's own control).
    const iptv::http::RequestControl *control = nullptr;
};
struct Reply
{
    int status = 0; // 0: no answer
    std::string body;
};
using Transport = std::function<Reply(const Request &)>;
// platform::api_request, stopped by `control` (which must outlive the transport).
Transport platform_transport(const iptv::http::RequestControl *control = nullptr);

// ---- the account, on plex.tv ----
struct Pin
{
    std::int64_t id = 0;
    std::string code;
    std::int64_t expires_in = 0; // seconds
};
enum class PinState
{
    pending,
    authorized,
    expired, // gone: a new code is needed
    failed,  // no answer this time
};
bool parse_pin(std::string_view json, Pin *pin, std::string *token);
bool create_pin(const Transport &transport, const Identity &identity, Pin *pin);
PinState poll_pin(const Transport &transport, const Identity &identity, std::int64_t id,
                  std::string *token);
// The name the account goes by (its friendly name, title or user name); never its e-mail.
bool parse_account(std::string_view json, std::string *name);
bool account_name(const Transport &transport, const Identity &identity, const std::string &token,
                  std::string *name);

struct Connection
{
    std::string protocol, address, uri;
    int port = 0;
    bool local = false, relay = false, ipv6 = false;
};
// A server the account can reach, its own or shared with it.
struct Resource
{
    std::string name, machine, token;
    bool owned = false, https_required = false, public_address_matches = false;
    std::vector<Connection> connections;
};
// Only servers ("provides" names server); players and the rest are left out.
bool parse_resources(std::string_view json, std::vector<Resource> *servers);
bool resources(const Transport &transport, const Identity &identity, const std::string &token,
               std::vector<Resource> *servers);
// A *.plex.direct https address's pin ("host:port:address"), from the address plex.tv gave or,
// without one, from the name itself (which spells its IPv4 address); empty for anything else.
std::string pin_for(std::string_view url, std::string_view address = {});
struct Candidate
{
    std::string url, pin;
    bool operator==(const Candidate &) const = default;
};
// The addresses worth trying, best first, as PlxNative ranks them: local, then remote, then
// relay; https before http inside each; IPv4 before IPv6; a number before a name. Over plain
// http an answer proves nothing, so http is tried only on our own LAN: a local, non-relay
// address of a server plex.tv says stands behind our router (publicAddressMatches) and that
// does not require https; such an address is also tried at its IP and port.
std::vector<Candidate> candidates(const Resource &server);
bool parse_identity(std::string_view json, std::string *machine);
struct Reached
{
    std::string base, pin; // empty base: none answered
};
// All candidates asked at once (each for at most `budget_ms`); the best-ranked one whose
// /identity names this server.
Reached choose_connection(const Transport &transport, const Identity &identity,
                          const Resource &server, unsigned budget_ms = 4000);

// ---- one server ----
struct Server
{
    std::string base;    // the connection in use: scheme, host and port, no slash
    std::string pin;     // its plex.direct pin, when it has one
    std::string token;   // the server's own access token
    std::string machine; // its machineIdentifier
    std::string name;
    std::uint64_t source_id = 0; // the catalog's source id: from the machine, whatever the address
    bool valid() const
    {
        return !base.empty() && !token.empty() && !machine.empty() && source_id != 0;
    }
};
std::uint64_t source_id(std::string_view machine);
bool valid_machine(std::string_view machine);
std::string url_encode(std::string_view text);

// What a check of the server (once a launch) found.
struct Check
{
    bool ok = false;
    std::string base, pin, token, name, error;
};
// With the account's token, the server list says where the server is now and gives its token;
// without one (or when plex.tv does not answer), the saved address is asked, over https only.
Check check_server(const Transport &transport, const Identity &identity, const Server &server,
                   const std::string &account_token);

struct Section
{
    std::string key, type, title;
};
bool parse_sections(std::string_view json, std::vector<Section> *sections);
bool sections(const Transport &transport, const Identity &identity, const Server &server,
              std::vector<Section> *sections);

struct Media
{
    std::string container, video_codec, video_profile, audio_codec, audio_profile;
    std::string part_key;
    unsigned width = 0, height = 0, parts = 0;
};
struct Item
{
    std::string rating_key, type, title, summary, thumb, year, rating, genres, cast;
    int index = -1, parent_index = -1;
    std::uint64_t duration_ms = 0, view_offset_ms = 0, added_at = 0, updated_at = 0;
    std::vector<Media> media;
};
struct Page
{
    std::vector<Item> items;
    std::size_t total = 0;
    bool has_total = false;
};
// At most `most` items are kept; the rest of the answer is read past.
bool parse_items(std::string_view json, Page *page, std::size_t most = 100000);
// A section's titles, `page_size` at a time (X-Plex-Container-Start/Size), handed to `each`
// in order; at most `limit` of them, and *truncated says when there were more.
bool list_section(const Transport &transport, const Identity &identity, const Server &server,
                  const Section &section, std::size_t limit, std::size_t page_size,
                  const std::function<bool(Item &&)> &each, bool *truncated);
// A show's seasons, or a season's episodes.
bool children(const Transport &transport, const Identity &identity, const Server &server,
              std::string_view rating_key, std::vector<Item> *items);
bool metadata(const Transport &transport, const Identity &identity, const Server &server,
              std::string_view rating_key, Item *item);
// A poster resized by the server to fit width x height, without the token.
std::string poster_url(const Server &server, std::string_view thumb, unsigned width = 256,
                       unsigned height = 256);

// ---- playback ----
// What the PS5 player plays as it is: MP4, MOV, Matroska, WebM or MPEG-TS; H.264 (8-bit), HEVC
// or VP9 profile 0 up to 3840 x 2160; AAC, AC-3, E-AC-3, MP2, MP3 or DTS with a core; one part.
bool direct_play(const Media &media);
// The media version to play, and whether as it is.
struct Choice
{
    int media_index = -1;
    bool direct = false;
};
Choice choose_media(const Item &item);

struct Playback
{
    Server server;
    Identity identity;
    std::string rating_key, session;
    int media_index = 0;
    std::uint64_t offset_ms = 0; // where the transcoder starts (offset=): the resume point
    bool transcode = false;
    bool active() const
    {
        return server.valid() && !rating_key.empty() && !session.empty();
    }
};
std::string new_session_id();
std::string direct_url(const Server &server, const Media &media);
// The universal transcoder over HLS (MPEG-TS segments, H.264 or HEVC video, AAC, AC-3 or
// E-AC-3 sound), copying what the player decodes and converting the rest.
std::string transcode_query(const Playback &playback);
std::string transcode_url(const Playback &playback);
std::string decision_url(const Playback &playback);
std::string stop_url(const Playback &playback);
// The player's extra headers (LF-separated): the token and the identity, for this server only.
std::string player_headers(const Playback &playback);

struct Prepared
{
    std::vector<std::string> urls; // best first: as it is, then the transcode
    std::uint64_t start_us = 0;
    bool transcode = false; // the first address is the transcoder's
    std::string error;
};
// The title's current details (resume point, versions), what to play, and the transcoder's
// session registered when the transcode is one of the addresses.
bool prepare(const Transport &transport, Playback *playback, std::uint64_t local_start_us,
             Prepared *out);

// prepare() on a worker, while the menu stays alive (as the portal's link does).
class PlayJob
{
  public:
    ~PlayJob();
    bool start(Playback playback, std::uint64_t local_start_us, Transport transport = {});
    void cancel();
    bool done() const
    {
        return done_.load();
    }
    bool succeeded() const
    {
        return done() && ok_ && !prepared_.urls.empty();
    }
    const Prepared &prepared() const
    {
        return prepared_;
    }
    const Playback &playback() const
    {
        return playback_;
    }

  private:
    static void *work(void *self);
    void *thread_ = nullptr;
    std::atomic<bool> stop_{false}, done_{false};
    iptv::http::RequestControl control_{};
    Transport transport_;
    Playback playback_;
    std::uint64_t local_start_us_ = 0;
    Prepared prepared_;
    bool ok_ = false;
};

enum class State
{
    playing,
    paused,
    stopped,
};
const char *state_name(State state);
std::string timeline_url(const Playback &playback, State state, std::uint64_t time_ms,
                         std::uint64_t duration_ms);
std::string scrobble_url(const Playback &playback);
bool report(const Transport &transport, const Playback &playback, State state,
            std::uint64_t time_ms, std::uint64_t duration_ms, unsigned timeout_ms = 6000,
            const iptv::http::RequestControl *control = nullptr);

// Progress while a title plays: a timeline report when it starts and every `interval_ms`
// after that. finish() stops a report on its way and returns at once: the stopped report, the
// title marked watched when it was completed, and the transcoder's stop are sent by a worker
// of their own, each with a short time limit.
class Timeline
{
  public:
    // False while the player has no position yet.
    using Sample =
        std::function<bool(std::uint64_t *position_us, std::uint64_t *duration_us, bool *paused)>;
    ~Timeline();
    bool start(Playback playback, Sample sample, Transport transport = {},
               unsigned interval_ms = 10000);
    // Set once the last requests are done (or were given up).
    using Done = std::shared_ptr<const std::atomic<bool>>;
    Done finish(std::uint64_t position_us, std::uint64_t duration_us, bool completed);
    unsigned reports() const
    {
        return reports_.load();
    }

  private:
    static void *work(void *self);
    void *thread_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<unsigned> reports_{0};
    iptv::http::RequestControl control_{};
    Transport transport_, given_;
    Playback playback_;
    Sample sample_;
    unsigned interval_ms_ = 10000;
    std::uint64_t last_position_us_ = 0, last_duration_us_ = 0;
};

// ---- signing in ----
struct FoundServer
{
    Resource resource;
    std::string base, pin; // the connection that answered; empty base when none did
};
// The PIN flow on a worker: a code is asked for, shown, and plex.tv is asked every
// `poll_ms` whether someone entered it; then the account's name and servers.
class SignIn
{
  public:
    enum class Step
    {
        starting,
        waiting, // code() is to be entered at plex.tv/link
        finishing,
        done,
        failed,
        cancelled,
    };
    ~SignIn();
    // With the token of an account already signed in, no code is asked for: its servers are
    // looked up again.
    bool start(Identity identity, Transport transport = {}, unsigned poll_ms = 2000,
               std::string known_token = {});
    void cancel();
    Step step() const
    {
        return step_.load();
    }
    bool finished() const
    {
        const Step value = step();
        return value == Step::done || value == Step::failed || value == Step::cancelled;
    }
    std::string code() const;
    // Seconds before the code runs out (0 when unknown or past).
    unsigned seconds_left() const;
    // After done:
    const std::string &token() const
    {
        return token_;
    }
    const std::string &name() const
    {
        return name_;
    }
    const std::vector<FoundServer> &servers() const
    {
        return servers_;
    }
    const std::string &error() const
    {
        return error_;
    }

  private:
    static void *work(void *self);
    void run();
    bool link();
    void lookup();
    void *thread_ = nullptr;
    std::atomic<Step> step_{Step::starting};
    std::atomic<bool> stop_{false};
    iptv::http::RequestControl control_{};
    Transport transport_;
    Identity identity_;
    unsigned poll_ms_ = 2000;
    mutable std::mutex mutex_;
    std::string code_;
    std::uint64_t expires_us_ = 0;
    std::string token_, name_, error_;
    std::vector<FoundServer> servers_;
};

// ---- what is kept on the console ----
// This console's client identifier, and the account's token and name while signed in: one
// file in the app's data, beside the sources (whose passwords hold each server's token).
struct Account
{
    std::string client_id, token, name;
    bool signed_in() const
    {
        return !token.empty();
    }
};
bool load_account(const std::string &path, Account *account);
bool save_account(const std::string &path, const Account &account);

} // namespace ptv::plex
