// Physalis - Plex as a source: sign-in on the TV, servers, libraries and playback.
// Copyright (C) 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See plex.hpp. The requests follow PlxNative's verified notes (docs/plex-api.md there): JSON
// answers with Accept: application/json, X-Plex-Container-Start/Size paging, /photo/:/transcode
// posters, the universal transcoder's decision before its start, and /:/timeline.

#include "tv/plex.hpp"

#include "tv/diag.hpp"
#include "tv/platform.hpp"

#include "iptv_catalog.h"
#include "iptv_json.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <sys/stat.h>
#include <tuple>

namespace ptv::plex
{
namespace
{
using iptv::json::JsonReader;
using iptv::json::ReadArray;
using iptv::json::ReadObject;

constexpr char kAccountMagic[] = "NIGHTSHADE-PLEX-1";
// A 4-character code is what plex.tv/link takes; a strong PIN's 25 characters are meant for
// app.plex.tv's own page instead, so the short one is asked for.
constexpr bool kStrongPin = false;
constexpr std::size_t kMaxText = 4096;
constexpr std::size_t kMaxPages = 2048;

std::uint64_t now_us()
{
    return platform::monotonic_us();
}

bool random_bytes(unsigned char *bytes, std::size_t size)
{
    std::FILE *file = std::fopen("/dev/urandom", "rb");
    bool ok = false;
    if (file)
    {
        ok = std::fread(bytes, 1, size, file) == size;
        std::fclose(file);
    }
    if (ok)
        return true;
    // No random device: a value that differs between consoles and launches is enough for an
    // identifier (it is not a secret).
    std::uint64_t state =
        now_us() ^ (platform::unix_time() << 20) ^ reinterpret_cast<std::uintptr_t>(bytes);
    for (std::size_t i = 0; i < size; ++i)
    {
        state += UINT64_C(0x9e3779b97f4a7c15);
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        bytes[i] = static_cast<unsigned char>(z ^ (z >> 31));
    }
    return true;
}

std::string uuid()
{
    unsigned char bytes[16]{};
    random_bytes(bytes, sizeof(bytes));
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fu) | 0x40u);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fu) | 0x80u);
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned i = 0; i < 16; ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out += '-';
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 15];
    }
    return out;
}

std::uint64_t number(std::string_view text)
{
    if (const auto dot = text.find('.'); dot != std::string_view::npos)
        text = text.substr(0, dot);
    if (text.empty() || text.size() > 19 ||
        text.find_first_not_of("0123456789") != std::string_view::npos)
        return 0;
    return std::strtoull(std::string(text).c_str(), nullptr, 10);
}

bool truthy(std::string_view text)
{
    return text == "true" || text == "1";
}

// Text cut at `keep` bytes, at a whole character.
void cut(std::string *text, std::size_t keep)
{
    if (text->size() <= keep)
        return;
    text->resize(keep);
    while (!text->empty() && (static_cast<unsigned char>(text->back()) & 0xc0u) == 0x80u)
        text->pop_back();
    if (!text->empty() && static_cast<unsigned char>(text->back()) >= 0xc0u)
        text->pop_back();
}

// Any value as text (a string, a number, true, false), a long one cut short; an object or a
// list is read past.
bool text(JsonReader *value, std::string *out, std::size_t keep = 1024)
{
    if (value->Peek() == '{' || value->Peek() == '[')
    {
        out->clear();
        return value->SkipValue();
    }
    if (!value->OptionalStringOrScalar(out, static_cast<std::size_t>(-1)))
        return false;
    cut(out, keep);
    return true;
}

bool safe_header_value(std::string_view value)
{
    return !value.empty() && value.size() <= 1024 &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char c) { return c < 0x20u || c == 0x7fu; });
}

struct Options
{
    unsigned connect_ms = 5000, timeout_ms = 15000;
    std::string_view pin;
    const iptv::http::RequestControl *control = nullptr;
};

Reply call(const Transport &transport, const Identity &identity, std::string method,
           std::string url, std::string_view token, const Options &options = {})
{
    if (!transport)
        return {};
    Request request;
    request.method = std::move(method);
    request.url = std::move(url);
    request.headers = headers(identity, token);
    if (!options.pin.empty())
        request.resolve.emplace_back(options.pin);
    request.connect_ms = options.connect_ms;
    request.timeout_ms = options.timeout_ms;
    request.control = options.control;
    return transport(request);
}

// A request to the server, at its connection (and its pin), with its token.
Reply ask(const Transport &transport, const Identity &identity, const Server &server,
          std::string path, Options options = {})
{
    options.pin = server.pin;
    return call(transport, identity, "GET", server.base + path, server.token, options);
}

// {"MediaContainer": {...}}: each field of the container.
template <typename Field> bool read_container(std::string_view json, Field &&field)
{
    JsonReader reader(json);
    bool found = false;
    const bool ok = ReadObject(&reader,
                               [&](const std::string &key, JsonReader *value)
                               {
                                   if (key != "MediaContainer" || value->Peek() != '{')
                                       return value->SkipValue();
                                   found = true;
                                   return ReadObject(value, field);
                               }) &&
                    reader.Finished();
    return ok && found;
}

// [{"tag": "..."}, ...] joined by ", ".
bool read_tags(JsonReader *value, std::string *out, std::size_t most, std::size_t keep)
{
    out->clear();
    if (value->Peek() != '[')
        return value->SkipValue();
    std::size_t count = 0;
    return ReadArray(value,
                     [&](JsonReader *entry)
                     {
                         if (entry->Peek() != '{')
                             return entry->SkipValue();
                         return ReadObject(entry,
                                           [&](const std::string &key, JsonReader *tag)
                                           {
                                               if (key != "tag")
                                                   return tag->SkipValue();
                                               std::string name;
                                               if (!text(tag, &name, 256))
                                                   return false;
                                               if (!name.empty() && count < most &&
                                                   out->size() + name.size() + 2 <= keep)
                                               {
                                                   if (!out->empty())
                                                       *out += ", ";
                                                   *out += name;
                                                   ++count;
                                               }
                                               return true;
                                           });
                     });
}

bool read_media(JsonReader *value, Media *media)
{
    std::string width, height;
    const bool ok = ReadObject(
        value,
        [&](const std::string &key, JsonReader *field)
        {
            if (key == "container")
                return text(field, &media->container, 32);
            if (key == "videoCodec")
                return text(field, &media->video_codec, 32);
            if (key == "videoProfile")
                return text(field, &media->video_profile, 64);
            if (key == "audioCodec")
                return text(field, &media->audio_codec, 32);
            if (key == "audioProfile")
                return text(field, &media->audio_profile, 64);
            if (key == "width")
                return text(field, &width, 16);
            if (key == "height")
                return text(field, &height, 16);
            if (key != "Part" || field->Peek() != '[')
                return field->SkipValue();
            return ReadArray(field,
                             [&](JsonReader *part)
                             {
                                 ++media->parts;
                                 if (part->Peek() != '{')
                                     return part->SkipValue();
                                 return ReadObject(part,
                                                   [&](const std::string &name, JsonReader *entry)
                                                   {
                                                       if (name == "key" && media->parts == 1)
                                                           return text(entry, &media->part_key,
                                                                       iptv::kDefaultMaxUrlBytes);
                                                       return entry->SkipValue();
                                                   });
                             });
        });
    media->width = static_cast<unsigned>(std::min<std::uint64_t>(number(width), 100000));
    media->height = static_cast<unsigned>(std::min<std::uint64_t>(number(height), 100000));
    return ok;
}

std::string rating_text(std::string_view value)
{
    if (value.empty() || value.size() > 16 ||
        value.find_first_not_of("0123456789.") != std::string_view::npos)
        return {};
    const double rating = std::strtod(std::string(value).c_str(), nullptr);
    if (rating <= 0 || rating > 10)
        return {};
    char out[24];
    std::snprintf(out, sizeof(out), "%.2f", rating);
    std::string result = out;
    while (!result.empty() && result.back() == '0')
        result.pop_back();
    if (!result.empty() && result.back() == '.')
        result.pop_back();
    return result;
}

bool read_item(JsonReader *value, Item *item)
{
    std::string index, parent, duration, offset, added, updated, year, rating, audience;
    if (!ReadObject(value,
                    [&](const std::string &key, JsonReader *field)
                    {
                        if (key == "ratingKey")
                            return text(field, &item->rating_key, 64);
                        if (key == "type")
                            return text(field, &item->type, 32);
                        if (key == "title")
                            return text(field, &item->title, 1024);
                        if (key == "summary")
                            return text(field, &item->summary, kMaxText);
                        if (key == "thumb")
                            return text(field, &item->thumb, 2048);
                        if (key == "year")
                            return text(field, &year, 16);
                        if (key == "rating")
                            return text(field, &rating, 16);
                        if (key == "audienceRating")
                            return text(field, &audience, 16);
                        if (key == "index")
                            return text(field, &index, 16);
                        if (key == "parentIndex")
                            return text(field, &parent, 16);
                        if (key == "duration")
                            return text(field, &duration, 24);
                        if (key == "viewOffset")
                            return text(field, &offset, 24);
                        if (key == "addedAt")
                            return text(field, &added, 24);
                        if (key == "updatedAt")
                            return text(field, &updated, 24);
                        if (key == "Genre")
                            return read_tags(field, &item->genres, 6, 256);
                        if (key == "Role")
                            return read_tags(field, &item->cast, 8, 1024);
                        if (key != "Media" || field->Peek() != '[')
                            return field->SkipValue();
                        return ReadArray(field,
                                         [&](JsonReader *entry)
                                         {
                                             if (entry->Peek() != '{')
                                                 return entry->SkipValue();
                                             Media media;
                                             if (!read_media(entry, &media))
                                                 return false;
                                             if (item->media.size() < 8)
                                                 item->media.push_back(std::move(media));
                                             return true;
                                         });
                    }))
        return false;
    const auto small = [](std::string_view value) -> int
    {
        const auto parsed = number(value);
        return value.empty() || parsed > 99999 ? -1 : static_cast<int>(parsed);
    };
    item->index = small(index);
    item->parent_index = small(parent);
    // Times past a thousand years are no times: their microseconds would not fit.
    constexpr std::uint64_t kMostMs = UINT64_C(31536000000000);
    item->duration_ms = std::min(number(duration), kMostMs);
    item->view_offset_ms = std::min(number(offset), kMostMs);
    item->added_at = number(added);
    item->updated_at = number(updated);
    const auto year_value = number(year);
    if (year_value >= 1880 && year_value <= 2200)
        item->year = std::to_string(year_value);
    item->rating = rating_text(audience.empty() ? rating : audience);
    if (item->rating.empty())
        item->rating = rating_text(rating);
    return true;
}

std::string bracket(std::string_view address)
{
    return address.find(':') != std::string_view::npos ? "[" + std::string(address) + "]"
                                                       : std::string(address);
}

bool numeric_host(std::string_view host)
{
    return !host.empty() && host.find_first_not_of("0123456789.:[]") == std::string_view::npos;
}

std::string host_of(std::string_view url)
{
    const auto scheme = url.find("://");
    if (scheme == std::string_view::npos)
        return {};
    auto rest = url.substr(scheme + 3);
    rest = rest.substr(0, rest.find_first_of("/?#"));
    if (!rest.empty() && rest.front() == '[')
        return std::string(rest.substr(0, rest.find(']') + 1));
    return std::string(rest.substr(0, rest.rfind(':')));
}

// The connection's own address as a bare origin: scheme, host, port.
std::string origin(std::string_view uri)
{
    std::string canonical;
    if (!iptv::CanonicalizeStreamUrl(uri, &canonical))
        return {};
    const auto scheme = canonical.find("://");
    if (scheme == std::string::npos ||
        (canonical.compare(0, scheme, "http") != 0 && canonical.compare(0, scheme, "https") != 0))
        return {};
    const auto end = canonical.find_first_of("/?#", scheme + 3);
    if (end != std::string::npos)
        canonical.resize(end);
    return canonical;
}

std::string metadata_path(std::string_view rating_key)
{
    return "/library/metadata/" + url_encode(rating_key);
}

void query(std::string *out, std::string_view name, std::string_view value)
{
    out->push_back(out->empty() ? '?' : '&');
    out->append(name);
    out->push_back('=');
    out->append(url_encode(value));
}

void query(std::string *out, std::string_view name, std::uint64_t value)
{
    query(out, name, std::to_string(value));
}

// The identity on a URL, for the transcoder (PlxNative sends it there as parameters).
void identity_query(std::string *out, const Identity &identity)
{
    query(out, "X-Plex-Client-Identifier", identity.client_id);
    query(out, "X-Plex-Product", identity.product);
    query(out, "X-Plex-Version", identity.version);
    query(out, "X-Plex-Platform", identity.platform);
    query(out, "X-Plex-Device", identity.device);
    query(out, "X-Plex-Device-Name", identity.device_name);
}

bool equals_any(std::string_view value, std::initializer_list<std::string_view> names)
{
    std::string lower(value);
    for (char &c : lower)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return std::find(names.begin(), names.end(), lower) != names.end();
}

bool contains_ci(std::string_view value, std::string_view part)
{
    std::string lower(value);
    for (char &c : lower)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return lower.find(part) != std::string::npos;
}

void pause_for(const std::atomic<bool> &stop, unsigned milliseconds)
{
    for (unsigned waited = 0; waited < milliseconds && !stop.load(); waited += 20)
        platform::sleep_ms(std::min(20u, milliseconds - waited));
}

} // namespace

std::vector<std::string> headers(const Identity &identity, std::string_view token)
{
    std::vector<std::string> out = {"Accept: application/json"};
    const auto add = [&](const char *name, std::string_view value)
    {
        if (safe_header_value(value))
            out.push_back(std::string(name) + ": " + std::string(value));
    };
    add("X-Plex-Product", identity.product);
    add("X-Plex-Version", identity.version);
    add("X-Plex-Platform", identity.platform);
    add("X-Plex-Device", identity.device);
    add("X-Plex-Device-Name", identity.device_name);
    add("X-Plex-Provides", "player");
    add("X-Plex-Client-Identifier", identity.client_id);
    if (valid_token(token))
        add("X-Plex-Token", token);
    return out;
}

std::string new_client_id()
{
    return uuid();
}

std::string new_session_id()
{
    return uuid();
}

bool valid_client_id(std::string_view id)
{
    return id.size() >= 8 && id.size() <= 64 &&
           id.find_first_not_of("0123456789abcdefABCDEF-") == std::string_view::npos;
}

bool valid_token(std::string_view token)
{
    return !token.empty() && token.size() <= 255 &&
           std::all_of(token.begin(), token.end(),
                       [](unsigned char c)
                       {
                           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                                  (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.' ||
                                  c == '~';
                       });
}

bool valid_machine(std::string_view machine)
{
    return machine.size() >= 8 && machine.size() <= 64 &&
           std::all_of(machine.begin(), machine.end(),
                       [](unsigned char c) {
                           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                                  (c >= 'A' && c <= 'Z') || c == '-';
                       });
}

std::uint64_t source_id(std::string_view machine)
{
    if (!valid_machine(machine))
        return 0;
    std::uint64_t hash = UINT64_C(1469598103934665603);
    for (const unsigned char byte : std::string_view("plex\n"))
        hash = (hash ^ byte) * UINT64_C(1099511628211);
    for (const unsigned char byte : machine)
        hash = (hash ^ byte) * UINT64_C(1099511628211);
    return UINT64_C(0x504c000000000000) | (hash & UINT64_C(0x0000ffffffffffff));
}

std::string url_encode(std::string_view text)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (const unsigned char c : text)
    {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += digits[c >> 4];
            out += digits[c & 15];
        }
    }
    return out;
}

Transport platform_transport(const iptv::http::RequestControl *control)
{
    return [control](const Request &request)
    {
        platform::ApiRequest api;
        api.method = request.method.c_str();
        api.url = request.url;
        api.headers = request.headers;
        api.resolve = request.resolve;
        api.connect_ms = request.connect_ms;
        api.timeout_ms = request.timeout_ms;
        // Either control stops it: the transport's (a cancelled job) or the request's own.
        struct Both
        {
            const iptv::http::RequestControl *first, *second;
        } both{control, request.control};
        const iptv::http::RequestControl stop{
            [](void *context)
            {
                const auto &b = *static_cast<const Both *>(context);
                for (const auto *c : {b.first, b.second})
                    if (c && c->cancelled && c->cancelled(c->context))
                        return true;
                return false;
            },
            &both};
        auto reply = platform::api_request(api, &stop);
        return Reply{reply.status, std::move(reply.body)};
    };
}

// ---- plex.tv ---------------------------------------------------------------------------

bool parse_pin(std::string_view json, Pin *pin, std::string *token)
{
    if (!pin)
        return false;
    *pin = {};
    std::string id, expires, auth;
    JsonReader reader(json);
    const bool ok = ReadObject(&reader,
                               [&](const std::string &key, JsonReader *value)
                               {
                                   if (key == "id")
                                       return text(value, &id, 24);
                                   if (key == "code")
                                       return text(value, &pin->code, 64);
                                   if (key == "expiresIn")
                                       return text(value, &expires, 24);
                                   if (key == "authToken")
                                       return text(value, &auth, 256);
                                   return value->SkipValue();
                               }) &&
                    reader.Finished();
    pin->id = static_cast<std::int64_t>(std::min<std::uint64_t>(number(id), INT64_MAX));
    pin->expires_in = static_cast<std::int64_t>(std::min<std::uint64_t>(number(expires), 86400));
    if (token)
        *token = valid_token(auth) ? auth : std::string();
    return ok && pin->id > 0 && !pin->code.empty() &&
           pin->code.find_first_of(" \t\r\n") == std::string::npos;
}

bool create_pin(const Transport &transport, const Identity &identity, Pin *pin)
{
    const auto reply =
        call(transport, identity, "POST",
             std::string(kPlexTv) + "/api/v2/pins?strong=" + (kStrongPin ? "true" : "false"), {});
    return (reply.status == 200 || reply.status == 201) && parse_pin(reply.body, pin, nullptr);
}

PinState poll_pin(const Transport &transport, const Identity &identity, std::int64_t id,
                  std::string *token)
{
    const auto reply = call(transport, identity, "GET",
                            std::string(kPlexTv) + "/api/v2/pins/" + std::to_string(id), {});
    if (reply.status == 404 || reply.status == 410)
        return PinState::expired;
    Pin pin;
    std::string found;
    if (reply.status != 200 || !parse_pin(reply.body, &pin, &found))
        return PinState::failed;
    if (found.empty())
        return PinState::pending;
    if (token)
        *token = std::move(found);
    return PinState::authorized;
}

bool parse_account(std::string_view json, std::string *name)
{
    std::string friendly, title, username;
    JsonReader reader(json);
    const bool ok = ReadObject(&reader,
                               [&](const std::string &key, JsonReader *value)
                               {
                                   if (key == "friendlyName")
                                       return text(value, &friendly, 128);
                                   if (key == "title")
                                       return text(value, &title, 128);
                                   if (key == "username")
                                       return text(value, &username, 128);
                                   return value->SkipValue();
                               }) &&
                    reader.Finished();
    const std::string &chosen = !friendly.empty() ? friendly : !title.empty() ? title : username;
    if (!ok || chosen.empty() || !name)
        return false;
    *name = chosen;
    return true;
}

bool account_name(const Transport &transport, const Identity &identity, const std::string &token,
                  std::string *name)
{
    const auto reply =
        call(transport, identity, "GET", std::string(kPlexTv) + "/api/v2/user", token);
    return reply.status == 200 && parse_account(reply.body, name);
}

bool parse_resources(std::string_view json, std::vector<Resource> *servers)
{
    if (!servers)
        return false;
    servers->clear();
    JsonReader reader(json);
    const bool ok =
        ReadArray(
            &reader,
            [&](JsonReader *entry)
            {
                if (entry->Peek() != '{')
                    return entry->SkipValue();
                Resource resource;
                std::string provides, owned, https, matches;
                if (!ReadObject(
                        entry,
                        [&](const std::string &key, JsonReader *value)
                        {
                            if (key == "name")
                                return text(value, &resource.name, 128);
                            if (key == "clientIdentifier")
                                return text(value, &resource.machine, 128);
                            if (key == "accessToken")
                                return text(value, &resource.token, 256);
                            if (key == "provides")
                                return text(value, &provides, 128);
                            if (key == "owned")
                                return text(value, &owned, 8);
                            if (key == "httpsRequired")
                                return text(value, &https, 8);
                            if (key == "publicAddressMatches")
                                return text(value, &matches, 8);
                            if (key != "connections" || value->Peek() != '[')
                                return value->SkipValue();
                            return ReadArray(
                                value,
                                [&](JsonReader *item)
                                {
                                    if (item->Peek() != '{')
                                        return item->SkipValue();
                                    Connection connection;
                                    std::string port, local, relay, v6;
                                    if (!ReadObject(
                                            item,
                                            [&](const std::string &name, JsonReader *field)
                                            {
                                                if (name == "protocol")
                                                    return text(field, &connection.protocol, 16);
                                                if (name == "address")
                                                    return text(field, &connection.address, 256);
                                                if (name == "uri")
                                                    return text(field, &connection.uri, 1024);
                                                if (name == "port")
                                                    return text(field, &port, 8);
                                                if (name == "local")
                                                    return text(field, &local, 8);
                                                if (name == "relay")
                                                    return text(field, &relay, 8);
                                                if (name == "IPv6")
                                                    return text(field, &v6, 8);
                                                return field->SkipValue();
                                            }))
                                        return false;
                                    connection.port = static_cast<int>(
                                        std::min<std::uint64_t>(number(port), 65535));
                                    connection.local = truthy(local);
                                    connection.relay = truthy(relay);
                                    connection.ipv6 = truthy(v6);
                                    if (resource.connections.size() < 32)
                                        resource.connections.push_back(std::move(connection));
                                    return true;
                                });
                        }))
                    return false;
                resource.owned = truthy(owned);
                resource.https_required = truthy(https);
                resource.public_address_matches = truthy(matches);
                if (provides.find("server") != std::string::npos &&
                    valid_machine(resource.machine) && valid_token(resource.token) &&
                    servers->size() < 64)
                    servers->push_back(std::move(resource));
                return true;
            }) &&
        reader.Finished();
    if (!ok)
        servers->clear();
    return ok;
}

bool resources(const Transport &transport, const Identity &identity, const std::string &token,
               std::vector<Resource> *servers)
{
    const auto reply =
        call(transport, identity, "GET",
             std::string(kPlexTv) + "/api/v2/resources?includeHttps=1&includeRelay=1", token);
    return reply.status == 200 && parse_resources(reply.body, servers);
}

std::string pin_for(std::string_view url, std::string_view address)
{
    if (!url.starts_with("https://"))
        return {};
    auto authority = url.substr(8);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    const auto colon = authority.rfind(':');
    const auto host = authority.substr(0, colon);
    const std::string port(colon == std::string_view::npos ? std::string_view("443")
                                                           : authority.substr(colon + 1));
    if (!host.ends_with(".plex.direct") || host.size() > 253 || port.empty() || port.size() > 5 ||
        port.find_first_not_of("0123456789") != std::string::npos ||
        host.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.") !=
            std::string_view::npos)
        return {};
    std::string at(address);
    if (at.empty())
    {
        // 192-168-1-5.<hash>.plex.direct is 192.168.1.5.
        const auto label = host.substr(0, host.find('.'));
        int parts = 0;
        std::string dotted;
        std::size_t from = 0;
        while (from <= label.size())
        {
            const auto dash = std::min(label.find('-', from), label.size());
            const auto part = label.substr(from, dash - from);
            if (part.empty() || part.size() > 3 ||
                part.find_first_not_of("0123456789") != std::string_view::npos ||
                number(part) > 255)
                return {};
            dotted += (parts++ ? "." : "") + std::string(part);
            from = dash + 1;
        }
        if (parts != 4)
            return {};
        at = dotted;
    }
    if (at.size() > 45 || at.find_first_not_of("0123456789abcdefABCDEF.:") != std::string::npos)
        return {};
    return std::string(host) + ":" + port + ":" +
           (at.find(':') != std::string::npos ? "[" + at + "]" : at);
}

std::vector<Candidate> candidates(const Resource &server)
{
    struct Ranked
    {
        Candidate candidate;
        int location, scheme, v6, name;
    };
    std::vector<Ranked> list;
    const auto add = [&](std::string url, const Connection &connection)
    {
        if (url.empty() || std::any_of(list.begin(), list.end(),
                                       [&](const Ranked &r) { return r.candidate.url == url; }))
            return;
        const bool https = url.starts_with("https://");
        // Over plain http any box can say it is this server: only on our own LAN.
        if (!https && (server.https_required || !server.public_address_matches ||
                       !connection.local || connection.relay))
            return;
        const auto host = host_of(url);
        auto pin = https ? pin_for(url, connection.address) : std::string();
        list.push_back({{std::move(url), std::move(pin)},
                        connection.relay   ? 2
                        : connection.local ? 0
                                           : 1,
                        https ? 0 : 1,
                        connection.ipv6 || host.starts_with("[") ? 1 : 0,
                        numeric_host(host) ? 0 : 1});
    };
    for (const auto &connection : server.connections)
    {
        add(origin(connection.uri), connection);
        if (!connection.address.empty() && connection.port > 0)
            add(origin("http://" + bracket(connection.address) + ":" +
                       std::to_string(connection.port)),
                connection);
    }
    std::stable_sort(list.begin(), list.end(),
                     [](const Ranked &a, const Ranked &b)
                     {
                         return std::tie(a.location, a.scheme, a.v6, a.name) <
                                std::tie(b.location, b.scheme, b.v6, b.name);
                     });
    std::vector<Candidate> out;
    for (auto &ranked : list)
        out.push_back(std::move(ranked.candidate));
    return out;
}

bool parse_identity(std::string_view json, std::string *machine)
{
    std::string found;
    const bool ok = read_container(json,
                                   [&](const std::string &key, JsonReader *value)
                                   {
                                       if (key == "machineIdentifier")
                                           return text(value, &found, 128);
                                       return value->SkipValue();
                                   });
    if (!ok || found.empty() || !machine)
        return false;
    *machine = found;
    return true;
}

Reached choose_connection(const Transport &transport, const Identity &identity,
                          const Resource &server, unsigned budget_ms)
{
    auto list = candidates(server);
    if (list.size() > 8)
        list.resize(8);
    // Each candidate on a worker of its own, so the slowest costs no more than the fastest.
    struct Probe
    {
        const Transport *transport;
        const Identity *identity;
        const Resource *server;
        Candidate candidate;
        unsigned budget_ms = 0;
        bool reached = false;
        void *thread = nullptr;
        void run()
        {
            // /identity needs no token: it says only which server answered.
            const auto reply = call(*transport, *identity, "GET", candidate.url + "/identity", {},
                                    {.connect_ms = std::min(2500u, budget_ms),
                                     .timeout_ms = budget_ms,
                                     .pin = candidate.pin});
            std::string machine;
            reached = reply.status == 200 && parse_identity(reply.body, &machine) &&
                      machine == server->machine;
        }
    };
    std::vector<Probe> probes;
    for (auto &candidate : list)
        probes.push_back(
            {&transport, &identity, &server, std::move(candidate), std::max(500u, budget_ms)});
    for (auto &probe : probes)
        probe.thread = platform::thread_start(
            [](void *self) -> void *
            {
                static_cast<Probe *>(self)->run();
                return nullptr;
            },
            &probe, 256u * 1024u, "ptv-plex-probe");
    for (auto &probe : probes)
    {
        if (probe.thread)
            platform::thread_join(probe.thread);
        else
            probe.run();
    }
    for (std::size_t i = 0; i < probes.size(); ++i)
        if (probes[i].reached)
        {
            diag::event("plex: server reached at candidate %zu of %zu (%s)", i + 1, probes.size(),
                        probes[i].candidate.url.starts_with("https://") ? "https" : "http");
            return {probes[i].candidate.url, probes[i].candidate.pin};
        }
    diag::event("plex: none of %zu candidates answered", probes.size());
    return {};
}

Check check_server(const Transport &transport, const Identity &identity, const Server &server,
                   const std::string &account_token)
{
    Check check;
    check.token = server.token;
    check.name = server.name;
    std::vector<Resource> list;
    if (valid_token(account_token) && resources(transport, identity, account_token, &list))
    {
        const auto found = std::find_if(list.begin(), list.end(), [&](const Resource &resource)
                                        { return resource.machine == server.machine; });
        if (found == list.end())
        {
            check.error = "This Plex server is no longer shared with your account.";
            return check;
        }
        check.token = found->token;
        if (!found->name.empty())
            check.name = found->name;
        const auto reached = choose_connection(transport, identity, *found);
        check.base = reached.base;
        check.pin = reached.pin;
    }
    // Without plex.tv only the saved https address is asked: its certificate proves the
    // server, where over plain http any box could answer for it.
    if (check.base.empty() && server.base.starts_with("https://"))
    {
        const auto pin = !server.pin.empty() ? server.pin : pin_for(server.base);
        const auto reply = call(transport, identity, "GET", server.base + "/identity", {},
                                {.connect_ms = 2500, .timeout_ms = 4000, .pin = pin});
        std::string machine;
        if (reply.status == 200 && parse_identity(reply.body, &machine) &&
            machine == server.machine)
        {
            check.base = server.base;
            check.pin = pin;
        }
    }
    check.ok = !check.base.empty();
    if (!check.ok)
        check.error = "The Plex server could not be reached.";
    return check;
}

// ---- one server ------------------------------------------------------------------------

bool parse_sections(std::string_view json, std::vector<Section> *sections)
{
    if (!sections)
        return false;
    sections->clear();
    return read_container(
        json,
        [&](const std::string &key, JsonReader *value)
        {
            if (key != "Directory" || value->Peek() != '[')
                return value->SkipValue();
            return ReadArray(value,
                             [&](JsonReader *entry)
                             {
                                 if (entry->Peek() != '{')
                                     return entry->SkipValue();
                                 Section section;
                                 if (!ReadObject(entry,
                                                 [&](const std::string &name, JsonReader *field)
                                                 {
                                                     if (name == "key")
                                                         return text(field, &section.key, 32);
                                                     if (name == "type")
                                                         return text(field, &section.type, 32);
                                                     if (name == "title")
                                                         return text(field, &section.title, 256);
                                                     return field->SkipValue();
                                                 }))
                                     return false;
                                 if ((section.type == "movie" || section.type == "show") &&
                                     !section.key.empty() &&
                                     section.key.find_first_not_of("0123456789") ==
                                         std::string::npos &&
                                     sections->size() < 256)
                                     sections->push_back(std::move(section));
                                 return true;
                             });
        });
}

bool sections(const Transport &transport, const Identity &identity, const Server &server,
              std::vector<Section> *out)
{
    const auto reply = ask(transport, identity, server, "/library/sections");
    return reply.status == 200 && parse_sections(reply.body, out);
}

bool parse_items(std::string_view json, Page *page, std::size_t most)
{
    if (!page)
        return false;
    *page = {};
    std::string total;
    const bool ok = read_container(
        json,
        [&](const std::string &key, JsonReader *value)
        {
            if (key == "totalSize")
                return text(value, &total, 24);
            if (key != "Metadata" || value->Peek() != '[')
                return value->SkipValue();
            return ReadArray(value,
                             [&](JsonReader *entry)
                             {
                                 if (entry->Peek() != '{' || page->items.size() >= most)
                                     return entry->SkipValue();
                                 Item item;
                                 if (!read_item(entry, &item))
                                     return false;
                                 if (!item.rating_key.empty())
                                     page->items.push_back(std::move(item));
                                 return true;
                             });
        });
    page->has_total = !total.empty();
    page->total = static_cast<std::size_t>(number(total));
    if (!ok)
        page->items.clear();
    return ok;
}

bool list_section(const Transport &transport, const Identity &identity, const Server &server,
                  const Section &section, std::size_t limit, std::size_t page_size,
                  const std::function<bool(Item &&)> &each, bool *truncated)
{
    if (truncated)
        *truncated = false;
    if (page_size == 0 || !each)
        return false;
    const std::string kind = section.type == "show" ? "2" : "1";
    std::size_t start = 0, given = 0;
    for (std::size_t pages = 0; pages < kMaxPages; ++pages)
    {
        std::string url = server.base + "/library/sections/" + url_encode(section.key) + "/all";
        std::string parameters;
        query(&parameters, "type", kind);
        query(&parameters, "includeGuids", "0");
        query(&parameters, "X-Plex-Container-Start", start);
        query(&parameters, "X-Plex-Container-Size", page_size);
        const auto reply =
            ask(transport, identity, server, url.substr(server.base.size()) + parameters);
        Page page;
        // A page is no longer than asked for, whatever the server sends.
        if (reply.status != 200 || !parse_items(reply.body, &page, page_size))
            return false;
        const std::size_t received = page.items.size();
        for (auto &item : page.items)
        {
            if (given >= limit)
            {
                if (truncated)
                    *truncated = true;
                return true;
            }
            if (!each(std::move(item)))
                return false;
            ++given;
        }
        start += received;
        if (received == 0 || (page.has_total && start >= page.total) ||
            (!page.has_total && received < page_size))
            return true;
        if (given >= limit)
        {
            if (truncated)
                *truncated = true;
            return true;
        }
    }
    if (truncated)
        *truncated = true;
    return true;
}

bool children(const Transport &transport, const Identity &identity, const Server &server,
              std::string_view rating_key, std::vector<Item> *items)
{
    if (!items || rating_key.empty())
        return false;
    items->clear();
    const auto reply = ask(transport, identity, server, metadata_path(rating_key) + "/children");
    Page page;
    if (reply.status != 200 || !parse_items(reply.body, &page, 10000))
        return false;
    *items = std::move(page.items);
    return true;
}

bool metadata(const Transport &transport, const Identity &identity, const Server &server,
              std::string_view rating_key, Item *item)
{
    if (!item || rating_key.empty())
        return false;
    const auto reply = ask(transport, identity, server, metadata_path(rating_key));
    Page page;
    if (reply.status != 200 || !parse_items(reply.body, &page, 1) || page.items.empty())
        return false;
    *item = std::move(page.items.front());
    return true;
}

std::string poster_url(const Server &server, std::string_view thumb, unsigned width,
                       unsigned height)
{
    if (thumb.empty() || server.base.empty())
        return {};
    std::string parameters;
    query(&parameters, "width", width);
    query(&parameters, "height", height);
    query(&parameters, "minSize", "0");
    query(&parameters, "upscale", "1");
    query(&parameters, "url", thumb);
    std::string canonical;
    const std::string url = server.base + "/photo/:/transcode" + parameters;
    return url.size() <= iptv::kDefaultMaxUrlBytes && iptv::CanonicalizeStreamUrl(url, &canonical)
               ? canonical
               : std::string();
}

// ---- playback --------------------------------------------------------------------------

bool direct_play(const Media &media)
{
    if (media.parts != 1 || media.part_key.empty() || media.part_key.front() != '/')
        return false;
    const bool matroska = equals_any(media.container, {"mkv", "matroska", "webm"});
    if (!matroska && !equals_any(media.container, {"mp4", "m4v", "mov", "mpegts", "ts"}))
        return false;
    if (media.width > 3840 || media.height > 2160)
        return false;
    // The decoder takes 8-bit H.264; High 10 and the 4:2:2 and 4:4:4 profiles it does not.
    const bool h264 =
        equals_any(media.video_codec, {"h264", "avc"}) && !contains_ci(media.video_profile, "10") &&
        !contains_ci(media.video_profile, "4:2:2") && !contains_ci(media.video_profile, "4:4:4");
    const bool hevc = equals_any(media.video_codec, {"hevc", "h265"});
    const bool vp9 = matroska && equals_any(media.video_codec, {"vp9"}) &&
                     (media.video_profile.empty() || contains_ci(media.video_profile, "0"));
    if (!h264 && !hevc && !vp9)
        return false;
    if (media.audio_codec.empty())
        return true;
    // DTS plays its core; DTS Express has none.
    if (equals_any(media.audio_codec, {"dca", "dts"}))
        return !contains_ci(media.audio_profile, "express");
    return equals_any(media.audio_codec, {"aac", "ac3", "eac3", "mp2", "mp3"});
}

Choice choose_media(const Item &item)
{
    Choice choice;
    for (std::size_t i = 0; i < item.media.size(); ++i)
        if (direct_play(item.media[i]))
            return {static_cast<int>(i), true};
    for (std::size_t i = 0; i < item.media.size(); ++i)
        if (!item.media[i].part_key.empty())
            return {static_cast<int>(i), false};
    return choice;
}

std::string direct_url(const Server &server, const Media &media)
{
    std::string canonical;
    if (media.part_key.empty() || media.part_key.front() != '/' ||
        !iptv::CanonicalizeStreamUrl(server.base + media.part_key, &canonical))
        return {};
    return canonical;
}

std::string transcode_query(const Playback &playback)
{
    // A target the player decodes, inside 4K: HLS in MPEG-TS, H.264 or HEVC copied when the
    // file has it, AAC, AC-3 or E-AC-3 sound.
    static constexpr char kProfile[] =
        "add-transcode-target(type=videoProfile&context=streaming&protocol=hls&container=mpegts"
        "&videoCodec=h264,hevc&audioCodec=aac,ac3,eac3)"
        "+add-limitation(scope=videoCodec&scopeName=*&type=upperBound&name=video.width"
        "&value=3840&replace=true)"
        "+add-limitation(scope=videoCodec&scopeName=*&type=upperBound&name=video.height"
        "&value=2160&replace=true)";
    std::string out;
    query(&out, "path", metadata_path(playback.rating_key));
    query(&out, "mediaIndex", static_cast<std::uint64_t>(std::max(0, playback.media_index)));
    query(&out, "partIndex", "0");
    query(&out, "protocol", "hls");
    query(&out, "fastSeek", "1");
    query(&out, "directPlay", "0");
    query(&out, "directStream", "1");
    query(&out, "directStreamAudio", "1");
    query(&out, "videoResolution", "3840x2160");
    query(&out, "maxVideoBitrate", "40000");
    query(&out, "subtitleSize", "100");
    query(&out, "audioBoost", "100");
    query(&out, "session", playback.session);
    query(&out, "X-Plex-Session-Identifier", playback.session);
    // A resume starts the transcoder there (PlxNative: offset on start, and its decision).
    if (playback.offset_ms >= 1000)
        query(&out, "offset", playback.offset_ms / 1000u);
    identity_query(&out, playback.identity);
    query(&out, "X-Plex-Client-Profile-Name", "Generic");
    query(&out, "X-Plex-Client-Profile-Extra", kProfile);
    return out;
}

std::string transcode_url(const Playback &playback)
{
    return playback.server.base + "/video/:/transcode/universal/start.m3u8" +
           transcode_query(playback);
}

std::string decision_url(const Playback &playback)
{
    return playback.server.base + "/video/:/transcode/universal/decision" +
           transcode_query(playback);
}

std::string stop_url(const Playback &playback)
{
    std::string out;
    query(&out, "session", playback.session);
    return playback.server.base + "/video/:/transcode/universal/stop" + out;
}

std::string player_headers(const Playback &playback)
{
    std::string out;
    const auto add = [&](const char *name, std::string_view value)
    {
        if (!safe_header_value(value))
            return;
        if (!out.empty())
            out += '\n';
        out += name;
        out += ": ";
        out += value;
    };
    if (valid_token(playback.server.token))
        add("X-Plex-Token", playback.server.token);
    add("X-Plex-Client-Identifier", playback.identity.client_id);
    add("X-Plex-Product", playback.identity.product);
    add("X-Plex-Version", playback.identity.version);
    add("X-Plex-Platform", playback.identity.platform);
    add("X-Plex-Device", playback.identity.device);
    add("X-Plex-Device-Name", playback.identity.device_name);
    add("X-Plex-Session-Identifier", playback.session);
    return out;
}

bool prepare(const Transport &transport, Playback *playback, std::uint64_t local_start_us,
             Prepared *out)
{
    if (!playback || !out)
        return false;
    *out = {};
    if (!playback->active())
    {
        out->error = "The Plex server is not set up.";
        return false;
    }
    Item item;
    if (!metadata(transport, playback->identity, playback->server, playback->rating_key, &item))
    {
        out->error = "The Plex server did not answer.";
        return false;
    }
    const Choice choice = choose_media(item);
    if (choice.media_index < 0)
    {
        out->error = "Plex has no file for this title.";
        return false;
    }
    playback->media_index = choice.media_index;
    // Where the title was left, on any device; a resume point the server does not have yet
    // (this console's own) when it has none. Both times are bounded (read_item), so the
    // microseconds fit.
    out->start_us =
        item.view_offset_ms && (!item.duration_ms || item.view_offset_ms < item.duration_ms)
            ? item.view_offset_ms * 1000u
            : local_start_us;
    playback->offset_ms = out->start_us / 1000u;
    // The transcoder's session is registered first (PlxNative: a cold start answers 400): it is
    // the fallback when the file plays as it is, and the stream when it does not.
    const auto decision = ask(transport, playback->identity, playback->server,
                              decision_url(*playback).substr(playback->server.base.size()));
    playback->transcode = true;
    if (choice.direct)
    {
        const auto url = direct_url(playback->server, item.media[choice.media_index]);
        if (!url.empty())
            out->urls.push_back(url);
    }
    out->transcode = out->urls.empty();
    std::string canonical;
    if (iptv::CanonicalizeStreamUrl(transcode_url(*playback), &canonical))
        out->urls.push_back(canonical);
    diag::event("plex: title ready to play %s, transcoder decision http %d, %zu address(es), "
                "resume at %llu s",
                out->transcode ? "through the transcoder" : "as it is", decision.status,
                out->urls.size(), static_cast<unsigned long long>(out->start_us / 1000000u));
    if (out->urls.empty())
        out->error = "Plex has no file for this title.";
    return !out->urls.empty();
}

PlayJob::~PlayJob()
{
    cancel();
}

bool PlayJob::start(Playback playback, std::uint64_t local_start_us, Transport transport)
{
    cancel();
    playback_ = std::move(playback);
    // A session of its own each time a title opens: the transcoder of the last one is stopped.
    playback_.session = new_session_id();
    local_start_us_ = local_start_us;
    prepared_ = {};
    ok_ = false;
    stop_.store(false);
    done_.store(false);
    control_ = {[](void *self) { return static_cast<PlayJob *>(self)->stop_.load(); }, this};
    transport_ = transport ? std::move(transport) : platform_transport(&control_);
    thread_ = platform::thread_start(work, this, 512u * 1024u, "ptv-plex-play");
    if (!thread_)
        done_.store(true);
    return thread_ != nullptr;
}

void *PlayJob::work(void *self)
{
    auto &job = *static_cast<PlayJob *>(self);
    job.ok_ = prepare(job.transport_, &job.playback_, job.local_start_us_, &job.prepared_) &&
              !job.stop_.load();
    job.done_.store(true);
    return nullptr;
}

void PlayJob::cancel()
{
    stop_.store(true);
    if (thread_)
    {
        platform::thread_join(thread_);
        thread_ = nullptr;
    }
}

const char *state_name(State state)
{
    switch (state)
    {
    case State::playing:
        return "playing";
    case State::paused:
        return "paused";
    case State::stopped:
        break;
    }
    return "stopped";
}

std::string timeline_url(const Playback &playback, State state, std::uint64_t time_ms,
                         std::uint64_t duration_ms)
{
    std::string out;
    query(&out, "ratingKey", playback.rating_key);
    query(&out, "key", metadata_path(playback.rating_key));
    query(&out, "identifier", "com.plexapp.plugins.library");
    query(&out, "state", state_name(state));
    query(&out, "time", time_ms);
    query(&out, "duration", duration_ms);
    query(&out, "X-Plex-Session-Identifier", playback.session);
    return playback.server.base + "/:/timeline" + out;
}

std::string scrobble_url(const Playback &playback)
{
    std::string out;
    query(&out, "key", playback.rating_key);
    query(&out, "identifier", "com.plexapp.plugins.library");
    return playback.server.base + "/:/scrobble" + out;
}

bool report(const Transport &transport, const Playback &playback, State state,
            std::uint64_t time_ms, std::uint64_t duration_ms, unsigned timeout_ms,
            const iptv::http::RequestControl *control)
{
    const auto reply =
        call(transport, playback.identity, "POST",
             timeline_url(playback, state, time_ms, duration_ms), playback.server.token,
             {.connect_ms = std::min(3000u, timeout_ms),
              .timeout_ms = timeout_ms,
              .pin = playback.server.pin,
              .control = control});
    return reply.status >= 200 && reply.status < 300;
}

namespace
{
// The last requests of a title, on a worker of their own: the menu is not kept waiting.
struct Farewell
{
    Transport transport;
    Playback playback;
    std::uint64_t position_us = 0, duration_us = 0;
    bool completed = false;
    std::shared_ptr<std::atomic<bool>> done;
    void run()
    {
        const Options brief{.connect_ms = 2000, .timeout_ms = 3000, .pin = playback.server.pin};
        const bool reached = report(transport, playback, State::stopped, position_us / 1000,
                                    duration_us / 1000, 3000);
        // Watched when the app counts it completed (the same check as its own resume point).
        if (reached && completed)
            (void)call(transport, playback.identity, "GET", scrobble_url(playback),
                       playback.server.token, brief);
        if (reached && playback.transcode)
            (void)call(transport, playback.identity, "GET", stop_url(playback),
                       playback.server.token, brief);
        diag::event("plex: stopped at %llu of %llu s%s%s",
                    static_cast<unsigned long long>(position_us / 1000000u),
                    static_cast<unsigned long long>(duration_us / 1000000u),
                    reached ? "" : ", the server did not answer",
                    reached && completed ? ", marked watched" : "");
        done->store(true);
    }
    static void *work(void *self)
    {
        auto *farewell = static_cast<Farewell *>(self);
        farewell->run();
        delete farewell;
        return nullptr;
    }
};
} // namespace

Timeline::~Timeline()
{
    if (thread_)
        (void)finish(last_position_us_, last_duration_us_, false);
}

bool Timeline::start(Playback playback, Sample sample, Transport transport, unsigned interval_ms)
{
    if (thread_ || !playback.active())
        return false;
    playback_ = std::move(playback);
    sample_ = std::move(sample);
    control_ = {[](void *self) { return static_cast<Timeline *>(self)->stop_.load(); }, this};
    given_ = transport;
    transport_ = transport ? std::move(transport) : platform_transport(&control_);
    interval_ms_ = std::max(1u, interval_ms);
    stop_.store(false);
    reports_.store(0);
    last_position_us_ = last_duration_us_ = 0;
    thread_ = platform::thread_start(work, this, 256u * 1024u, "ptv-plex-timeline");
    return thread_ != nullptr;
}

Timeline::Done Timeline::finish(std::uint64_t position_us, std::uint64_t duration_us,
                                bool completed)
{
    auto done = std::make_shared<std::atomic<bool>>(false);
    if (!thread_)
    {
        done->store(true);
        return done;
    }
    // A report on its way is stopped (the control), and the worker is left at once.
    stop_.store(true);
    platform::thread_join(thread_);
    thread_ = nullptr;
    auto *farewell = new Farewell{given_ ? given_ : platform_transport(nullptr),
                                  playback_,
                                  position_us ? position_us : last_position_us_,
                                  duration_us ? duration_us : last_duration_us_,
                                  completed,
                                  done};
    void *worker =
        platform::thread_start(Farewell::work, farewell, 256u * 1024u, "ptv-plex-stopped");
    if (worker)
        (void)platform::thread_detach(worker);
    else
        Farewell::work(farewell); // short time limits: the menu waits a few seconds at most
    return done;
}

void *Timeline::work(void *self)
{
    auto &t = *static_cast<Timeline *>(self);
    std::uint64_t next = now_us();
    while (!t.stop_.load())
    {
        const auto now = now_us();
        if (now < next)
        {
            pause_for(t.stop_, static_cast<unsigned>(std::min<std::uint64_t>(
                                   (next - now) / 1000 + 1, t.interval_ms_)));
            continue;
        }
        std::uint64_t position = 0, duration = 0;
        bool paused = false;
        if (t.sample_ && t.sample_(&position, &duration, &paused))
        {
            t.last_position_us_ = position;
            t.last_duration_us_ = duration;
            (void)report(t.transport_, t.playback_, paused ? State::paused : State::playing,
                         position / 1000, duration / 1000, 6000, &t.control_);
            t.reports_.fetch_add(1);
            next = now_us() + std::uint64_t(t.interval_ms_) * 1000u;
        }
        else
            next = now_us() + std::min<std::uint64_t>(500, t.interval_ms_) * 1000u;
    }
    return nullptr;
}

// ---- signing in ------------------------------------------------------------------------

SignIn::~SignIn()
{
    cancel();
}

bool SignIn::start(Identity identity, Transport transport, unsigned poll_ms,
                   std::string known_token)
{
    cancel();
    identity_ = std::move(identity);
    poll_ms_ = std::max(1u, poll_ms);
    stop_.store(false);
    step_.store(Step::starting);
    {
        const std::lock_guard lock(mutex_);
        code_.clear();
        expires_us_ = 0;
    }
    token_ = valid_token(known_token) ? std::move(known_token) : std::string();
    name_.clear();
    error_.clear();
    servers_.clear();
    control_ = {[](void *self) { return static_cast<SignIn *>(self)->stop_.load(); }, this};
    transport_ = transport ? std::move(transport) : platform_transport(&control_);
    thread_ = platform::thread_start(work, this, 512u * 1024u, "ptv-plex-sign-in");
    if (!thread_)
    {
        error_ = "plex.tv could not be reached.";
        step_.store(Step::failed);
    }
    return thread_ != nullptr;
}

void SignIn::cancel()
{
    stop_.store(true);
    if (thread_)
    {
        platform::thread_join(thread_);
        thread_ = nullptr;
    }
}

std::string SignIn::code() const
{
    const std::lock_guard lock(mutex_);
    return code_;
}

unsigned SignIn::seconds_left() const
{
    const std::lock_guard lock(mutex_);
    const auto now = now_us();
    return expires_us_ > now ? static_cast<unsigned>((expires_us_ - now + 999999u) / 1000000u) : 0u;
}

void *SignIn::work(void *self)
{
    static_cast<SignIn *>(self)->run();
    return nullptr;
}

void SignIn::run()
{
    if (token_.empty() && !link())
        return;
    lookup();
}

bool SignIn::link()
{
    Pin pin;
    if (!create_pin(transport_, identity_, &pin))
    {
        error_ = "plex.tv could not be reached.";
        step_.store(stop_.load() ? Step::cancelled : Step::failed);
        return false;
    }
    {
        const std::lock_guard lock(mutex_);
        code_ = pin.code;
        expires_us_ = now_us() + std::uint64_t(pin.expires_in ? pin.expires_in : 900) * 1000000u;
    }
    step_.store(Step::waiting);
    diag::event("plex: link code shown, valid for %lld s", static_cast<long long>(pin.expires_in));
    std::string token;
    for (;;)
    {
        pause_for(stop_, poll_ms_);
        if (stop_.load())
        {
            step_.store(Step::cancelled);
            return false;
        }
        if (seconds_left() == 0)
        {
            error_ = "The code ran out. Ask for a new one.";
            step_.store(Step::failed);
            return false;
        }
        const auto state = poll_pin(transport_, identity_, pin.id, &token);
        if (state == PinState::authorized)
            break;
        if (state == PinState::expired)
        {
            error_ = "The code ran out. Ask for a new one.";
            step_.store(Step::failed);
            return false;
        }
    }
    token_ = token;
    return true;
}

void SignIn::lookup()
{
    step_.store(Step::finishing);
    (void)account_name(transport_, identity_, token_, &name_);
    std::vector<Resource> list;
    if (!resources(transport_, identity_, token_, &list))
    {
        error_ = "Your Plex servers could not be listed.";
        step_.store(stop_.load() ? Step::cancelled : Step::failed);
        return;
    }
    // The account's own servers first.
    std::stable_sort(list.begin(), list.end(),
                     [](const Resource &a, const Resource &b) { return a.owned > b.owned; });
    // Each server's candidates are asked at once, and all of them within twenty seconds: a
    // server not reached by then is left to Find servers.
    const auto deadline = now_us() + UINT64_C(20000000);
    for (auto &resource : list)
    {
        if (stop_.load())
        {
            step_.store(Step::cancelled);
            return;
        }
        const auto now = now_us();
        const auto left = deadline > now ? (deadline - now) / 1000u : 0u;
        Reached reached;
        if (left >= 500)
            reached = choose_connection(transport_, identity_, resource,
                                        static_cast<unsigned>(std::min<std::uint64_t>(left, 4000)));
        servers_.push_back({std::move(resource), std::move(reached.base), std::move(reached.pin)});
    }
    diag::event("plex: signed in; %zu server(s)", servers_.size());
    step_.store(Step::done);
}

// ---- what is kept on the console -------------------------------------------------------

bool load_account(const std::string &path, Account *account)
{
    if (!account)
        return false;
    *account = {};
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;
    char buffer[2048];
    const std::size_t bytes = std::fread(buffer, 1, sizeof(buffer), file);
    const bool read = std::ferror(file) == 0;
    std::fclose(file);
    if (!read || bytes == sizeof(buffer))
        return false;
    std::string_view rest(buffer, bytes);
    std::string lines[4];
    for (auto &line : lines)
    {
        const auto end = rest.find('\n');
        if (end == std::string_view::npos)
            return false;
        line.assign(rest.substr(0, end));
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        rest.remove_prefix(end + 1);
    }
    const bool name_ok =
        lines[3].size() <= 128 && std::none_of(lines[3].begin(), lines[3].end(), [](unsigned char c)
                                               { return c < 0x20u || c == 0x7fu; });
    if (!rest.empty() || lines[0] != kAccountMagic || !valid_client_id(lines[1]) ||
        (!lines[2].empty() && !valid_token(lines[2])) || !name_ok)
        return false;
    account->client_id = std::move(lines[1]);
    account->token = std::move(lines[2]);
    account->name = account->token.empty() ? std::string() : std::move(lines[3]);
    return true;
}

bool save_account(const std::string &path, const Account &account)
{
    if (!valid_client_id(account.client_id) ||
        (!account.token.empty() && !valid_token(account.token)) || account.name.size() > 128 ||
        account.name.find_first_of("\r\n") != std::string::npos)
        return false;
    const std::string temporary = path + ".tmp";
    std::FILE *file = std::fopen(temporary.c_str(), "wb");
    if (!file)
        return false;
    const std::string content = std::string(kAccountMagic) + "\n" + account.client_id + "\n" +
                                account.token + "\n" +
                                (account.token.empty() ? std::string() : account.name) + "\n";
    bool ok = std::fwrite(content.data(), 1, content.size(), file) == content.size();
    ok = std::fflush(file) == 0 && ok;
    ok = std::fclose(file) == 0 && ok;
    // As the phone remote keeps its pairing tokens: for this app alone where the system allows.
    (void)chmod(temporary.c_str(), 0600);
    if (!ok || std::rename(temporary.c_str(), path.c_str()) != 0)
    {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace ptv::plex
