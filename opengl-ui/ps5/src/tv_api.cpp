// Nightshade TV - Small API calls on a connection of their own (Plex), beside the player.
// Copyright (C) 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Like the artwork (tv_images.cpp): one libcurl easy handle per call, so a call shares no
// state with the catalog's or the player's requests (src/iptv_http.cpp) and can run while a
// film plays. Nothing here logs: a call's headers carry a token, and its URL is not written
// down either.
#include "tv/platform.hpp"
#include "tv_http.h"
#include "update_kit/console_curl.h"
#include <curl/curl.h>
#include <cstring>

namespace ptv::platform
{
namespace
{
struct Call
{
    std::string *body;
    std::size_t limit;
    const iptv::http::RequestControl *control;
    bool too_large = false;
};
std::size_t receive(char *data, std::size_t size, std::size_t count, void *context)
{
    auto &call = *static_cast<Call *>(context);
    const std::size_t bytes = size * count;
    if (bytes > call.limit - call.body->size())
    {
        call.too_large = true;
        return 0;
    }
    call.body->append(data, bytes);
    return bytes;
}
int progress(void *context, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto *control = static_cast<Call *>(context)->control;
    return control && control->cancelled && control->cancelled(control->context) ? 1 : 0;
}
} // namespace

ApiReply api_request(const ApiRequest &request, const iptv::http::RequestControl *control)
{
    ApiReply reply;
    const char *method = request.method ? request.method : "GET";
    const bool post = std::strcmp(method, "POST") == 0;
    const bool put = std::strcmp(method, "PUT") == 0;
    if ((!post && !put && std::strcmp(method, "GET") != 0) ||
        !iptv::http::IsSupportedPlaylistUrl(request.url.c_str()) || tv_http_init(0, 0, 0) < 0)
        return reply;
    CURL *curl = curl_easy_init();
    if (!curl)
        return reply;
    curl_slist *list = nullptr;
    for (const auto &header : request.headers)
        list = curl_slist_append(list, header.c_str());
    // The app's pins, then this request's own (a later entry for a host replaces an earlier).
    curl_slist *pins = tv_http_resolve_list();
    for (const auto &pin : request.resolve)
        pins = curl_slist_append(pins, pin.c_str());
    Call call{&reply.body, request.limit, control};
    console_curl_setup(curl);
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NightshadeTV");
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    if (pins)
        curl_easy_setopt(curl, CURLOPT_RESOLVE, pins);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(request.connect_ms));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout_ms));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &call);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &call);
    if (post)
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    }
    else if (put)
    {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    }
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(list);
    curl_slist_free_all(pins);
    if (result != CURLE_OK || call.too_large || progress(&call, 0, 0, 0, 0))
        return {};
    reply.status = static_cast<int>(status);
    return reply;
}
} // namespace ptv::platform
