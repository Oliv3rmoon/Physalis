// Physalis - Plex: the PIN sign-in, servers and their connections, libraries, playback
// and the timeline, against recorded answers. Nothing here reaches a network.
// Copyright (C) 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/plex.hpp"
#include "tv/backup.hpp"
#include "tv/diag.hpp"
#include "tv/library.hpp"
#include "tv/model.hpp"
#include "tv/vod.hpp"
#include "host_platform.hpp"
#include "iptv_store.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace ptv;

constexpr char kAccountToken[] = "ACCOUNT-token_123";
constexpr char kServerToken[] = "SERVER-token_456";
constexpr char kSharedToken[] = "SHARED-token_789";
constexpr char kMachine[] = "aaaabbbbccccdddd0000111122223333";
constexpr char kShared[] = "eeeeffff000011112222333344445555";
constexpr char kLocal[] = "https://192-168-1-5.aaaabbbb.plex.direct:32400";

// plex.tv and a server, answering what the test says: by method and exact URL, or by a
// function for answers that change (the PIN before and after someone enters it).
class FakePlex
{
  public:
    using Answer = std::function<plex::Reply(const plex::Request &)>;
    void on(const std::string &method, const std::string &url, int status, std::string body)
    {
        answers_[method + " " + url] = [status, body](const plex::Request &)
        { return plex::Reply{status, body}; };
    }
    void on(const std::string &method, const std::string &url, Answer answer)
    {
        answers_[method + " " + url] = std::move(answer);
    }
    plex::Transport transport()
    {
        return [this](const plex::Request &request)
        {
            Answer answer;
            {
                const std::lock_guard lock(mutex_);
                requests_.push_back(request);
                const auto found = answers_.find(request.method + " " + request.url);
                if (found != answers_.end())
                    answer = found->second;
            }
            return answer ? answer(request) : plex::Reply{};
        };
    }
    std::vector<plex::Request> requests()
    {
        const std::lock_guard lock(mutex_);
        return requests_;
    }
    std::vector<plex::Request> to(std::string_view part)
    {
        std::vector<plex::Request> out;
        for (auto &request : requests())
            if (request.url.find(part) != std::string::npos)
                out.push_back(request);
        return out;
    }

  private:
    std::mutex mutex_;
    std::map<std::string, Answer> answers_;
    std::vector<plex::Request> requests_;
};

bool has_header(const plex::Request &request, std::string_view header)
{
    return std::find(request.headers.begin(), request.headers.end(), header) !=
           request.headers.end();
}
bool has_header_named(const plex::Request &request, std::string_view name)
{
    return std::any_of(request.headers.begin(), request.headers.end(),
                       [&](const std::string &header) { return header.starts_with(name); });
}
std::string identity_json(const std::string &machine)
{
    return R"({"MediaContainer":{"size":0,"claimed":true,"machineIdentifier":")" + machine +
           R"(","version":"1.41.0"}})";
}

plex::Identity identity()
{
    plex::Identity id;
    id.client_id = "0f1e2d3c-4b5a-4978-8a9b-abcdefabcdef";
    id.version = "01.002.000";
    return id;
}

plex::Server server(std::string base = kLocal)
{
    plex::Server s;
    s.base = std::move(base);
    s.pin = plex::pin_for(s.base); // as a server found answering carries it
    s.token = kServerToken;
    s.machine = kMachine;
    s.name = "Living Room";
    s.source_id = plex::source_id(kMachine);
    return s;
}

// An owned server with a LAN address (as plex.direct and as its IP), a public one and a relay;
// a server shared with the account; and a player, which is no server.
const char kResources[] = R"([
 {"name":"Living Room","product":"Plex Media Server","provides":"server","owned":true,
  "clientIdentifier":"aaaabbbbccccdddd0000111122223333","accessToken":"SERVER-token_456",
  "httpsRequired":false,"publicAddressMatches":true,"presence":true,
  "connections":[
   {"protocol":"https","address":"203.0.113.9","port":32400,"uri":"https://203-0-113-9.aaaabbbb.plex.direct:32400","local":false,"relay":false,"IPv6":false},
   {"protocol":"https","address":"192.168.1.5","port":32400,"uri":"https://192-168-1-5.aaaabbbb.plex.direct:32400","local":true,"relay":false,"IPv6":false},
   {"protocol":"https","address":"fd00::5","port":32400,"uri":"https://fd00--5.aaaabbbb.plex.direct:32400","local":true,"relay":false,"IPv6":true},
   {"protocol":"https","address":"198.51.100.4","port":8443,"uri":"https://198-51-100-4.aaaabbbb.plex.direct:8443","local":false,"relay":true,"IPv6":false}]},
 {"name":"Friend's server","provides":"server","owned":false,
  "clientIdentifier":"eeeeffff000011112222333344445555","accessToken":"SHARED-token_789",
  "httpsRequired":false,"publicAddressMatches":false,
  "connections":[
   {"protocol":"https","address":"172.20.0.2","port":32400,"uri":"https://172-20-0-2.eeeeffff.plex.direct:32400","local":true,"relay":false},
   {"protocol":"https","address":"198.51.100.7","port":32400,"uri":"https://198-51-100-7.eeeeffff.plex.direct:32400","local":false,"relay":false}]},
 {"name":"Phone","provides":"client,player","clientIdentifier":"1234567890abcdef","accessToken":"x"}
])";

// ---- sign-in ------------------------------------------------------------------------------

TEST(PlexPin, ParsesPendingAndAuthorizedAnswers)
{
    plex::Pin pin;
    std::string token = "unchanged";
    ASSERT_TRUE(plex::parse_pin(
        R"({"id":4242,"code":"AB12","expiresIn":900,"authToken":null,"qr":"https://plex.tv/x"})",
        &pin, &token));
    EXPECT_EQ(pin.id, 4242);
    EXPECT_EQ(pin.code, "AB12");
    EXPECT_EQ(pin.expires_in, 900);
    EXPECT_TRUE(token.empty());
    ASSERT_TRUE(
        plex::parse_pin(R"({"id":"4242","code":"AB12","authToken":"tok-1"})", &pin, &token));
    EXPECT_EQ(token, "tok-1");
    // A token with anything a header could be split by is no token.
    ASSERT_TRUE(
        plex::parse_pin(R"({"id":1,"code":"A","authToken":"a\r\nX-Evil: 1"})", &pin, &token));
    EXPECT_TRUE(token.empty());
    EXPECT_FALSE(plex::parse_pin(R"({"code":"AB12"})", &pin, &token));
    EXPECT_FALSE(plex::parse_pin("<html>", &pin, &token));
}

TEST(PlexPin, PendingThenAuthorizedThenServers)
{
    FakePlex fake;
    fake.on("POST", "https://plex.tv/api/v2/pins?strong=false", 201,
            R"({"id":4242,"code":"AB12","expiresIn":900,"authToken":null})");
    int polls = 0;
    fake.on("GET", "https://plex.tv/api/v2/pins/4242",
            [&polls](const plex::Request &)
            {
                return ++polls < 3
                           ? plex::Reply{200, R"({"id":4242,"code":"AB12","authToken":null})"}
                           : plex::Reply{200,
                                         std::string(R"({"id":4242,"code":"AB12","authToken":")") +
                                             kAccountToken + "\"}"};
            });
    fake.on(
        "GET", "https://plex.tv/api/v2/user", 200,
        R"({"id":1,"username":"andrew","title":"Andrew S","friendlyName":"","email":"a@b.invalid"})");
    fake.on("GET", "https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1", 200,
            kResources);
    fake.on("GET", std::string(kLocal) + "/identity", 200, identity_json(kMachine));
    fake.on("GET", "https://198-51-100-7.eeeeffff.plex.direct:32400/identity", 200,
            identity_json(kShared));

    plex::SignIn sign_in;
    ASSERT_TRUE(sign_in.start(identity(), fake.transport(), 5));
    bool saw_code = false;
    for (int i = 0; i < 2000 && !sign_in.finished(); ++i)
    {
        if (sign_in.step() == plex::SignIn::Step::waiting && sign_in.code() == "AB12")
        {
            saw_code = true;
            EXPECT_GT(sign_in.seconds_left(), 800u);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(sign_in.step(), plex::SignIn::Step::done) << sign_in.error();
    EXPECT_TRUE(saw_code);
    EXPECT_GE(polls, 3);
    EXPECT_EQ(sign_in.token(), kAccountToken);
    EXPECT_EQ(sign_in.name(), "Andrew S");   // the title when no friendly name; never the e-mail
    ASSERT_EQ(sign_in.servers().size(), 2u); // the player is left out
    EXPECT_EQ(sign_in.servers()[0].resource.machine, kMachine);
    EXPECT_EQ(sign_in.servers()[0].base, kLocal);
    EXPECT_EQ(sign_in.servers()[0].pin, "192-168-1-5.aaaabbbb.plex.direct:32400:192.168.1.5");
    EXPECT_EQ(sign_in.servers()[0].resource.token, kServerToken);
    EXPECT_EQ(sign_in.servers()[1].base, "https://198-51-100-7.eeeeffff.plex.direct:32400");

    // The identity on every request; the token only once plex.tv gave one.
    const auto create = fake.to("/api/v2/pins?strong=false");
    ASSERT_EQ(create.size(), 1u);
    EXPECT_EQ(create[0].method, "POST");
    for (const char *header :
         {"Accept: application/json", "X-Plex-Product: Physalis", "X-Plex-Version: 01.002.000",
          "X-Plex-Platform: Generic", "X-Plex-Device-Name: Physalis (PS5)",
          "X-Plex-Client-Identifier: 0f1e2d3c-4b5a-4978-8a9b-abcdefabcdef"})
        EXPECT_TRUE(has_header(create[0], header)) << header;
    EXPECT_FALSE(has_header_named(create[0], "X-Plex-Token"));
    for (const auto &poll : fake.to("/api/v2/pins/4242"))
        EXPECT_FALSE(has_header_named(poll, "X-Plex-Token"));
    const auto list = fake.to("/api/v2/resources");
    ASSERT_EQ(list.size(), 1u);
    EXPECT_TRUE(has_header(list[0], std::string("X-Plex-Token: ") + kAccountToken));
    // /identity needs no token, so none is sent where it is not needed.
    for (const auto &probe : fake.to("/identity"))
        EXPECT_FALSE(has_header_named(probe, "X-Plex-Token"));
}

TEST(PlexPin, AGoneCodeFailsAndCancelStops)
{
    FakePlex fake;
    fake.on("POST", "https://plex.tv/api/v2/pins?strong=false", 201,
            R"({"id":7,"code":"ZZ99","expiresIn":900})");
    fake.on("GET", "https://plex.tv/api/v2/pins/7", 404, "");
    plex::SignIn gone;
    ASSERT_TRUE(gone.start(identity(), fake.transport(), 1));
    for (int i = 0; i < 2000 && !gone.finished(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(gone.step(), plex::SignIn::Step::failed);
    EXPECT_EQ(gone.error(), "The code ran out. Ask for a new one.");
    EXPECT_TRUE(gone.token().empty());

    FakePlex slow;
    slow.on("POST", "https://plex.tv/api/v2/pins?strong=false", 201,
            R"({"id":8,"code":"YY88","expiresIn":900})");
    slow.on("GET", "https://plex.tv/api/v2/pins/8", 200, R"({"id":8,"code":"YY88"})");
    plex::SignIn waiting;
    ASSERT_TRUE(waiting.start(identity(), slow.transport(), 1));
    for (int i = 0; i < 2000 && waiting.step() != plex::SignIn::Step::waiting; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    waiting.cancel();
    EXPECT_EQ(waiting.step(), plex::SignIn::Step::cancelled);

    FakePlex down;
    plex::SignIn offline;
    ASSERT_TRUE(offline.start(identity(), down.transport(), 1));
    for (int i = 0; i < 2000 && !offline.finished(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(offline.step(), plex::SignIn::Step::failed);
    EXPECT_EQ(offline.error(), "plex.tv could not be reached.");
}

TEST(PlexPin, ASignedInAccountLooksItsServersUpWithoutACode)
{
    FakePlex fake;
    fake.on("GET", "https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1", 200,
            kResources);
    plex::SignIn again;
    ASSERT_TRUE(again.start(identity(), fake.transport(), 1, kAccountToken));
    for (int i = 0; i < 2000 && !again.finished(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(again.step(), plex::SignIn::Step::done);
    EXPECT_TRUE(fake.to("/api/v2/pins").empty());
    EXPECT_EQ(again.servers().size(), 2u);
    EXPECT_TRUE(again.servers()[0].base.empty()); // nothing answered: kept, to look for later
}

TEST(PlexAccount, KeepsTheClientIdAcrossSignOutAndRefusesBrokenFiles)
{
    char pattern[] = "/tmp/nightshade-plex-XXXXXX";
    const std::string dir = mkdtemp(pattern);
    const std::string path = dir + "/prosperotv-plex-v1.txt";
    const auto id = plex::new_client_id();
    EXPECT_TRUE(plex::valid_client_id(id));
    EXPECT_NE(id, plex::new_client_id());
    ASSERT_TRUE(plex::save_account(path, {id, kAccountToken, "Andrew"}));
    plex::Account read;
    ASSERT_TRUE(plex::load_account(path, &read));
    EXPECT_EQ(read.client_id, id);
    EXPECT_EQ(read.token, kAccountToken);
    EXPECT_EQ(read.name, "Andrew");
    ASSERT_TRUE(plex::save_account(path, {id, {}, "Andrew"}));
    ASSERT_TRUE(plex::load_account(path, &read));
    EXPECT_EQ(read.client_id, id);
    EXPECT_FALSE(read.signed_in());
    EXPECT_TRUE(read.name.empty());
    EXPECT_FALSE(plex::save_account(path, {id, "bad token\n", "x"}));
    std::ofstream(path) << "NIGHTSHADE-PLEX-1\nnot-an-id!\n\n\n";
    EXPECT_FALSE(plex::load_account(path, &read));
    std::filesystem::remove_all(dir);
}

// ---- servers ------------------------------------------------------------------------------

std::vector<std::string> urls(const std::vector<plex::Candidate> &list)
{
    std::vector<std::string> out;
    for (const auto &candidate : list)
        out.push_back(candidate.url);
    return out;
}

TEST(PlexServers, ResourcesParseAndConnectionsRankLocalRemoteRelay)
{
    std::vector<plex::Resource> list;
    ASSERT_TRUE(plex::parse_resources(kResources, &list));
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].name, "Living Room");
    EXPECT_TRUE(list[0].owned);
    EXPECT_EQ(list[0].connections.size(), 4u);
    EXPECT_FALSE(list[1].owned);
    EXPECT_EQ(list[1].token, kSharedToken);

    // At home (plex.tv says the server stands behind our router): local first, https before
    // http, IPv4 before IPv6, then remote, then relay; each plex.direct name pinned to the
    // address plex.tv gave beside it.
    EXPECT_EQ(plex::candidates(list[0]),
              (std::vector<plex::Candidate>{
                  {kLocal, "192-168-1-5.aaaabbbb.plex.direct:32400:192.168.1.5"},
                  {"https://fd00--5.aaaabbbb.plex.direct:32400",
                   "fd00--5.aaaabbbb.plex.direct:32400:[fd00::5]"},
                  {"http://192.168.1.5:32400", ""},
                  {"http://[fd00::5]:32400", ""},
                  {"https://203-0-113-9.aaaabbbb.plex.direct:32400",
                   "203-0-113-9.aaaabbbb.plex.direct:32400:203.0.113.9"},
                  {"https://198-51-100-4.aaaabbbb.plex.direct:8443",
                   "198-51-100-4.aaaabbbb.plex.direct:8443:198.51.100.4"}}));
    // Away from home an answer over plain http proves nothing: https only, owned or not.
    auto away = list[0];
    away.public_address_matches = false;
    EXPECT_EQ(urls(plex::candidates(away)),
              (std::vector<std::string>{kLocal, "https://fd00--5.aaaabbbb.plex.direct:32400",
                                        "https://203-0-113-9.aaaabbbb.plex.direct:32400",
                                        "https://198-51-100-4.aaaabbbb.plex.direct:8443"}));
    // A shared server's LAN is its owner's: no http address at all, even one it advertises.
    auto shared = list[1];
    shared.connections.push_back(
        {"http", "172.20.0.2", "http://172.20.0.2:32400", 32400, true, false, false});
    EXPECT_EQ(urls(plex::candidates(shared)),
              (std::vector<std::string>{"https://172-20-0-2.eeeeffff.plex.direct:32400",
                                        "https://198-51-100-7.eeeeffff.plex.direct:32400"}));
    // The owner requires https: none over http, even at home.
    auto strict = list[0];
    strict.https_required = true;
    for (const auto &candidate : plex::candidates(strict))
        EXPECT_TRUE(candidate.url.starts_with("https://")) << candidate.url;
    // A remote address over plain http is never tried, nor a relay over http.
    auto plain = list[0];
    plain.connections = {
        {"http", "203.0.113.9", "http://203.0.113.9:32400", 32400, false, false, false},
        {"http", "198.51.100.4", "http://198.51.100.4:8443", 8443, true, true, false}};
    EXPECT_TRUE(plex::candidates(plain).empty());

    EXPECT_FALSE(plex::parse_resources("{\"error\":1}", &list));
    EXPECT_TRUE(list.empty());
}

TEST(PlexServers, PlexDirectNamesArePinnedToTheirAddress)
{
    EXPECT_EQ(plex::pin_for("https://192-168-1-5.abc.plex.direct:32400"),
              "192-168-1-5.abc.plex.direct:32400:192.168.1.5");
    EXPECT_EQ(plex::pin_for("https://192-168-1-5.abc.plex.direct"),
              "192-168-1-5.abc.plex.direct:443:192.168.1.5");
    EXPECT_EQ(plex::pin_for("https://x.abc.plex.direct:32400", "10.0.0.2"),
              "x.abc.plex.direct:32400:10.0.0.2");
    EXPECT_TRUE(plex::pin_for("https://x.abc.plex.direct:32400").empty()); // no address in it
    EXPECT_TRUE(plex::pin_for("http://192-168-1-5.abc.plex.direct:32400").empty());
    EXPECT_TRUE(plex::pin_for("https://example.com:443", "1.2.3.4").empty());
    EXPECT_TRUE(plex::pin_for("https://300-1-1-1.abc.plex.direct:1").empty());
    EXPECT_TRUE(plex::pin_for("https://1-2-3-4.abc.plex.direct:32400", "1.2.3.4\r\nX: y").empty());
}

TEST(PlexServers, AllCandidatesAreAskedAtOnceAndTheBestRankedServerIsChosen)
{
    std::vector<plex::Resource> list;
    ASSERT_TRUE(plex::parse_resources(kResources, &list));
    FakePlex fake;
    // At home a stranger's box answers on the LAN address; the public one is the server, and so
    // is its relay: the better-ranked of the two is chosen.
    fake.on("GET", std::string(kLocal) + "/identity", 200, identity_json("someone-else-123"));
    fake.on("GET", "http://192.168.1.5:32400/identity", 500, "");
    fake.on("GET", "https://203-0-113-9.aaaabbbb.plex.direct:32400/identity", 200,
            identity_json(kMachine));
    fake.on("GET", "https://198-51-100-4.aaaabbbb.plex.direct:8443/identity", 200,
            identity_json(kMachine));
    const auto reached = plex::choose_connection(fake.transport(), identity(), list[0]);
    EXPECT_EQ(reached.base, "https://203-0-113-9.aaaabbbb.plex.direct:32400");
    EXPECT_EQ(reached.pin, "203-0-113-9.aaaabbbb.plex.direct:32400:203.0.113.9");
    const auto asked = fake.to("/identity");
    ASSERT_EQ(asked.size(), 6u); // every candidate, at once
    for (const auto &request : asked)
    {
        EXPECT_LE(request.timeout_ms, 4000u);
        EXPECT_FALSE(has_header_named(request, "X-Plex-Token"));
        if (request.url.starts_with("https://"))
            EXPECT_EQ(request.resolve.size(), 1u) << request.url;
        else
            EXPECT_TRUE(request.resolve.empty()) << request.url;
    }

    // Slow boxes cost their own time, not the sum of everyone's.
    FakePlex slow;
    for (const auto &candidate : plex::candidates(list[0]))
        slow.on("GET", candidate.url + "/identity",
                [machine = candidate.url == kLocal ? std::string(kMachine)
                                                   : std::string("other")](const plex::Request &)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    return plex::Reply{200, identity_json(machine)};
                });
    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(plex::choose_connection(slow.transport(), identity(), list[0]).base, kLocal);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(900));

    FakePlex none;
    EXPECT_TRUE(plex::choose_connection(none.transport(), identity(), list[0]).base.empty());
}

TEST(PlexServers, ACheckUsesTheAccountAndAsksTheSavedAddressOnlyOverHttps)
{
    FakePlex fake;
    fake.on("GET", "https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1", 200,
            kResources);
    fake.on("GET", "https://203-0-113-9.aaaabbbb.plex.direct:32400/identity", 200,
            identity_json(kMachine));
    auto saved = server(kLocal);
    saved.token = "old-token";
    auto check = plex::check_server(fake.transport(), identity(), saved, kAccountToken);
    ASSERT_TRUE(check.ok) << check.error;
    EXPECT_EQ(check.base, "https://203-0-113-9.aaaabbbb.plex.direct:32400");
    EXPECT_EQ(check.pin, "203-0-113-9.aaaabbbb.plex.direct:32400:203.0.113.9");
    EXPECT_EQ(check.token, kServerToken);

    // Without plex.tv the saved https address is asked, at the address its name spells.
    FakePlex offline_account;
    offline_account.on("GET", std::string(kLocal) + "/identity", 200, identity_json(kMachine));
    check = plex::check_server(offline_account.transport(), identity(), saved, kAccountToken);
    ASSERT_TRUE(check.ok);
    EXPECT_EQ(check.base, kLocal);
    EXPECT_EQ(check.pin, "192-168-1-5.aaaabbbb.plex.direct:32400:192.168.1.5");
    EXPECT_EQ(check.token, "old-token");
    EXPECT_EQ(offline_account.to("/identity").at(0).resolve,
              (std::vector<std::string>{"192-168-1-5.aaaabbbb.plex.direct:32400:192.168.1.5"}));

    // A saved plain http address never: any box could answer for the server there.
    FakePlex lan;
    lan.on("GET", "http://192.168.1.5:32400/identity", 200, identity_json(kMachine));
    check = plex::check_server(lan.transport(), identity(), server("http://192.168.1.5:32400"),
                               kAccountToken);
    EXPECT_FALSE(check.ok);
    EXPECT_TRUE(lan.to("/identity").empty());

    FakePlex gone;
    gone.on("GET", "https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1", 200, "[]");
    check = plex::check_server(gone.transport(), identity(), saved, kAccountToken);
    EXPECT_FALSE(check.ok);
    EXPECT_EQ(check.error, "This Plex server is no longer shared with your account.");
}

// ---- libraries ----------------------------------------------------------------------------

std::string movie(int key, const std::string &title, const std::string &media)
{
    return R"({"ratingKey":")" + std::to_string(key) + R"(","key":"/library/metadata/)" +
           std::to_string(key) + R"(","type":"movie","title":")" + title +
           R"(","year":2001,"summary":"About )" + title +
           R"(","audienceRating":7.5,"thumb":"/library/metadata/)" + std::to_string(key) +
           R"(/thumb/1700000000","duration":5400000,"viewOffset":60000,"addedAt":)" +
           std::to_string(1700000000 + key) +
           R"(,"Genre":[{"tag":"Drama"},{"tag":"War"}],"Role":[{"tag":"A. Actor"}],"Media":[)" +
           media + "]}";
}
const char kMkv[] =
    R"({"container":"mkv","videoCodec":"h264","videoProfile":"high","audioCodec":"ac3","width":1920,"height":1080,"Part":[{"id":1,"key":"/library/parts/11/1700000000/file.mkv","container":"mkv"}]})";
const char kHevcTrueHd[] =
    R"({"container":"mkv","videoCodec":"hevc","videoProfile":"main 10","audioCodec":"truehd","width":3840,"height":2160,"Part":[{"key":"/library/parts/12/1700000000/file.mkv"}]})";

std::string page(const std::vector<std::string> &items, std::size_t total)
{
    std::string out = R"({"MediaContainer":{"size":)" + std::to_string(items.size()) +
                      R"(,"totalSize":)" + std::to_string(total) + R"(,"Metadata":[)";
    for (std::size_t i = 0; i < items.size(); ++i)
        out += (i ? "," : "") + items[i];
    return out + "]}}";
}

std::string all_url(const std::string &section, int type, int start, int size)
{
    return std::string(kLocal) + "/library/sections/" + section +
           "/all?type=" + std::to_string(type) +
           "&includeGuids=0&X-Plex-Container-Start=" + std::to_string(start) +
           "&X-Plex-Container-Size=" + std::to_string(size);
}

const char kSections[] =
    R"({"MediaContainer":{"size":3,"Directory":[{"key":"1","type":"movie","title":"Movies"},{"key":"2","type":"show","title":"TV Shows"},{"key":"3","type":"artist","title":"Music"}]}})";

TEST(PlexLibrary, SectionsAndPagedListings)
{
    std::vector<plex::Section> list;
    ASSERT_TRUE(plex::parse_sections(kSections, &list));
    ASSERT_EQ(list.size(), 2u); // music is no film or show
    EXPECT_EQ(list[1].title, "TV Shows");

    FakePlex fake;
    std::vector<std::string> movies;
    for (int i = 0; i < 5; ++i)
        movies.push_back(movie(100 + i, "Film " + std::to_string(i), kMkv));
    fake.on("GET", all_url("1", 1, 0, 2), 200, page({movies[0], movies[1]}, 5));
    fake.on("GET", all_url("1", 1, 2, 2), 200, page({movies[2], movies[3]}, 5));
    fake.on("GET", all_url("1", 1, 4, 2), 200, page({movies[4]}, 5));
    std::vector<std::string> titles;
    bool truncated = true;
    ASSERT_TRUE(plex::list_section(
        fake.transport(), identity(), server(), list[0], 100, 2,
        [&](plex::Item &&item)
        {
            titles.push_back(item.title);
            return true;
        },
        &truncated));
    EXPECT_FALSE(truncated);
    EXPECT_EQ(titles, (std::vector<std::string>{"Film 0", "Film 1", "Film 2", "Film 3", "Film 4"}));
    const auto asked = fake.to("/library/sections/1/all");
    ASSERT_EQ(asked.size(), 3u);
    for (const auto &request : asked)
        EXPECT_TRUE(has_header(request, std::string("X-Plex-Token: ") + kServerToken));

    // A limit keeps the first titles and says there were more.
    titles.clear();
    ASSERT_TRUE(plex::list_section(
        fake.transport(), identity(), server(), list[0], 3, 2,
        [&](plex::Item &&item)
        {
            titles.push_back(item.title);
            return true;
        },
        &truncated));
    EXPECT_TRUE(truncated);
    EXPECT_EQ(titles.size(), 3u);

    // A page that does not come is a failed list, not a short one.
    FakePlex broken;
    broken.on("GET", all_url("1", 1, 0, 2), 200, page({movies[0], movies[1]}, 5));
    EXPECT_FALSE(plex::list_section(
        broken.transport(), identity(), server(), list[0], 100, 2,
        [](plex::Item &&) { return true; }, &truncated));
}

TEST(PlexLibrary, MoviesBecomeOnDemandTitlesWithPostersAndDetails)
{
    FakePlex fake;
    fake.on("GET", std::string(kLocal) + "/library/sections", 200, kSections);
    fake.on("GET", all_url("1", 1, 0, 200), 200,
            page({movie(101, "Direct", kMkv), movie(102, "Needs transcoding", kHevcTrueHd)}, 2));
    iptv::Catalog catalog;
    ptv::VodMetadata metadata;
    std::string error;
    bool truncated = true;
    ASSERT_TRUE(ptv::load_plex_vod(server(), identity(), ptv::VodKind::movies, {}, &catalog,
                                   nullptr, &error, &metadata, &truncated, 0, fake.transport()))
        << error;
    EXPECT_FALSE(truncated);
    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(catalog.source_id, plex::source_id(kMachine));
    const auto direct = catalog[0];
    EXPECT_EQ(direct.name, "Direct");
    EXPECT_EQ(direct.tvg_id, "101");
    EXPECT_EQ(direct.group_title, "Movies"); // the library is the category
    EXPECT_EQ(direct.media_kind, iptv::MediaKind::movie);
    EXPECT_EQ(direct.url, std::string(kLocal) + "/library/parts/11/1700000000/file.mkv");
    EXPECT_EQ(direct.tvg_logo,
              std::string(kLocal) +
                  "/photo/:/transcode?width=256&height=256&minSize=0&upscale=1&url=%2Flibrary%"
                  "2Fmetadata%2F101%2Fthumb%2F1700000000");
    EXPECT_EQ(direct.source_line, 1700000101u);
    EXPECT_EQ(catalog[1].url, std::string(kLocal) + "/library/metadata/102");
    const auto &details = metadata.at(std::string(direct.id));
    EXPECT_TRUE(details.complete);
    EXPECT_EQ(details.plot, "About Direct");
    EXPECT_EQ(details.genre, "Drama, War");
    EXPECT_EQ(details.cast, "A. Actor");
    EXPECT_EQ(details.year, "2001");
    EXPECT_EQ(details.rating, "7.5");
    // No address the catalog keeps carries a token.
    for (const auto item : catalog)
    {
        EXPECT_EQ(std::string(item.url).find("Token"), std::string::npos);
        EXPECT_EQ(std::string(item.tvg_logo).find("Token"), std::string::npos);
    }
}

TEST(PlexLibrary, AShowsSeasonsHoldItsEpisodesInOrder)
{
    FakePlex fake;
    fake.on("GET", std::string(kLocal) + "/library/sections", 200, kSections);
    fake.on(
        "GET", all_url("2", 2, 0, 200), 200,
        R"({"MediaContainer":{"size":1,"totalSize":1,"Metadata":[{"ratingKey":"500","type":"show","title":"A show","year":2010,"summary":"Shows things","thumb":"/library/metadata/500/thumb/1","addedAt":1600000000,"updatedAt":1700000500}]}})");
    fake.on(
        "GET", std::string(kLocal) + "/library/metadata/500/children", 200,
        R"({"MediaContainer":{"size":2,"Metadata":[{"ratingKey":"510","type":"season","index":1,"title":"Season 1"},{"ratingKey":"520","type":"season","index":2,"title":"Season 2"}]}})");
    fake.on(
        "GET", std::string(kLocal) + "/library/metadata/510/children", 200,
        R"({"MediaContainer":{"size":2,"Metadata":[{"ratingKey":"512","type":"episode","index":2,"parentIndex":1,"title":"Second","Media":[)" +
            std::string(kMkv) +
            R"(]},{"ratingKey":"511","type":"episode","index":1,"parentIndex":1,"title":"Pilot","Media":[)" +
            kMkv + "]}]}}");
    fake.on(
        "GET", std::string(kLocal) + "/library/metadata/520/children", 200,
        R"({"MediaContainer":{"size":1,"Metadata":[{"ratingKey":"521","type":"episode","index":1,"title":"Return","Media":[)" +
            std::string(kHevcTrueHd) + "]}]}}");

    iptv::Catalog shows;
    std::string error;
    ASSERT_TRUE(ptv::load_plex_vod(server(), identity(), ptv::VodKind::series, {}, &shows, nullptr,
                                   &error, nullptr, nullptr, 0, fake.transport()));
    ASSERT_EQ(shows.size(), 1u);
    EXPECT_EQ(shows[0].tvg_id, "500");
    EXPECT_EQ(shows[0].group_title, "TV Shows");
    EXPECT_EQ(shows[0].source_line, 1700000500u); // a show changes when episodes are added

    iptv::Catalog episodes;
    ASSERT_TRUE(ptv::load_plex_vod(server(), identity(), ptv::VodKind::episodes, "500", &episodes,
                                   nullptr, &error, nullptr, nullptr, 0, fake.transport()));
    ASSERT_EQ(episodes.size(), 3u);
    std::map<std::uint32_t, std::string> order;
    for (const auto episode : episodes)
        order[episode.source_line] =
            std::string(episode.group_title) + ": " + std::string(episode.name);
    EXPECT_EQ(order,
              (std::map<std::uint32_t, std::string>{{100001u, "Season 1: Episode 1 · Pilot"},
                                                    {100002u, "Season 1: Episode 2 · Second"},
                                                    {200001u, "Season 2: Episode 1 · Return"}}));
    EXPECT_FALSE(ptv::load_plex_vod(server(), identity(), ptv::VodKind::episodes, "999", &episodes,
                                    nullptr, &error, nullptr, nullptr, 0, fake.transport()));
    EXPECT_TRUE(episodes.empty());
}

// ---- playback -----------------------------------------------------------------------------

plex::Media media(std::string container, std::string video, std::string audio,
                  std::string profile = {}, unsigned width = 1920, unsigned height = 1080)
{
    plex::Media m;
    m.container = std::move(container);
    m.video_codec = std::move(video);
    m.audio_codec = std::move(audio);
    m.video_profile = std::move(profile);
    m.width = width;
    m.height = height;
    m.parts = 1;
    m.part_key = "/library/parts/1/2/file." + m.container;
    return m;
}

TEST(PlexPlayback, DirectPlayOnlyWhatThePlayerDecodes)
{
    EXPECT_TRUE(plex::direct_play(media("mkv", "h264", "ac3", "high")));
    EXPECT_TRUE(plex::direct_play(media("mp4", "hevc", "eac3", "main 10", 3840, 2160)));
    EXPECT_TRUE(plex::direct_play(media("mov", "h264", "aac")));
    EXPECT_TRUE(plex::direct_play(media("mpegts", "h264", "mp2")));
    EXPECT_TRUE(plex::direct_play(media("mkv", "hevc", "dca")));
    EXPECT_TRUE(plex::direct_play(media("webm", "vp9", "")));
    EXPECT_FALSE(plex::direct_play(media("mp4", "vp9", "aac")));
    EXPECT_FALSE(plex::direct_play(media("mkv", "h264", "aac", "high 10")));
    EXPECT_FALSE(plex::direct_play(media("avi", "mpeg4", "mp3")));
    EXPECT_FALSE(plex::direct_play(media("mkv", "hevc", "truehd")));
    EXPECT_FALSE(plex::direct_play(media("mkv", "av1", "opus")));
    EXPECT_FALSE(plex::direct_play(media("mkv", "hevc", "aac", {}, 4096, 2160)));
    auto express = media("mkv", "h264", "dca");
    express.audio_profile = "dts express";
    EXPECT_FALSE(plex::direct_play(express));
    auto two_parts = media("mkv", "h264", "ac3");
    two_parts.parts = 2;
    EXPECT_FALSE(plex::direct_play(two_parts));

    plex::Item item;
    item.media = {media("mkv", "hevc", "truehd"), media("mp4", "h264", "aac")};
    const auto choice = plex::choose_media(item);
    EXPECT_EQ(choice.media_index, 1);
    EXPECT_TRUE(choice.direct);
    item.media = {media("avi", "mpeg4", "mp3")};
    EXPECT_EQ(plex::choose_media(item).media_index, 0);
    EXPECT_FALSE(plex::choose_media(item).direct);
}

std::string page_of_three()
{
    return page({movie(1, "A", kMkv), movie(2, "B", kMkv), movie(3, "C", kMkv)}, 3);
}

std::string metadata_reply(const std::string &media_json)
{
    return page({movie(101, "Film", media_json)}, 1);
}

TEST(PlexPlayback, AFileThatPlaysAsItIsComesFirstAndResumesWhereItWasLeft)
{
    FakePlex fake;
    fake.on("GET", std::string(kLocal) + "/library/metadata/101", 200, metadata_reply(kMkv));
    plex::Playback playback{server(), identity(), "101", "session-1"};
    // The transcoder's session is registered (its decision) before anything plays, starting
    // where the title is resumed.
    auto expected = playback;
    expected.offset_ms = 60000;
    fake.on("GET", plex::decision_url(expected), 200, R"({"MediaContainer":{"size":1}})");
    plex::Prepared prepared;
    ASSERT_TRUE(plex::prepare(fake.transport(), &playback, 5000000, &prepared)) << prepared.error;
    EXPECT_FALSE(prepared.transcode);
    ASSERT_EQ(prepared.urls.size(), 2u);
    EXPECT_EQ(prepared.urls[0], std::string(kLocal) + "/library/parts/11/1700000000/file.mkv");
    EXPECT_TRUE(prepared.urls[1].starts_with(std::string(kLocal) +
                                             "/video/:/transcode/universal/start.m3u8?"));
    EXPECT_NE(prepared.urls[1].find("&offset=60&"), std::string::npos) << prepared.urls[1];
    EXPECT_EQ(prepared.start_us, 60000000u); // Plex's viewOffset wins over this console's
    const auto decisions = fake.to("/transcode/universal/decision");
    ASSERT_EQ(decisions.size(), 1u);
    EXPECT_EQ(decisions[0].url, plex::decision_url(expected));
    EXPECT_EQ(decisions[0].resolve,
              (std::vector<std::string>{"192-168-1-5.aaaabbbb.plex.direct:32400:192.168.1.5"}));
}

TEST(PlexPlayback, TimesTooLargeToBeTimesAreNotResumedFrom)
{
    for (const char *times : {R"("viewOffset":99999999999999999,"duration":5400000)",
                              R"("viewOffset":99999999999999998,"duration":99999999999999999)"})
    {
        FakePlex fake;
        fake.on(
            "GET", std::string(kLocal) + "/library/metadata/101", 200,
            std::string(
                R"({"MediaContainer":{"size":1,"Metadata":[{"ratingKey":"101","type":"movie","title":"Film",)") +
                times + R"(,"Media":[)" + kMkv + "]}]}}");
        plex::Playback playback{server(), identity(), "101", "session-9"};
        plex::Prepared prepared;
        ASSERT_TRUE(plex::prepare(fake.transport(), &playback, 7000000, &prepared));
        EXPECT_EQ(prepared.start_us, 7000000u) << times; // this console's own point instead
    }
    // A page is no longer than asked for.
    plex::Page page;
    ASSERT_TRUE(plex::parse_items(page_of_three(), &page, 2));
    EXPECT_EQ(page.items.size(), 2u);
    ASSERT_TRUE(plex::parse_items(page_of_three(), &page));
    EXPECT_EQ(page.items.size(), 3u);
}

TEST(PlexPlayback, OtherFilesGoThroughTheHlsTranscoder)
{
    FakePlex fake;
    fake.on("GET", std::string(kLocal) + "/library/metadata/101", 200, metadata_reply(kHevcTrueHd));
    plex::Playback playback{server(), identity(), "101", "session-2"};
    fake.on("GET", plex::decision_url(playback), 200, R"({"MediaContainer":{"size":1}})");
    plex::Prepared prepared;
    ASSERT_TRUE(plex::prepare(fake.transport(), &playback, 0, &prepared));
    EXPECT_TRUE(prepared.transcode);
    EXPECT_TRUE(playback.transcode);
    ASSERT_EQ(prepared.urls.size(), 1u);
    const auto &url = prepared.urls[0];
    EXPECT_TRUE(url.starts_with(std::string(kLocal) + "/video/:/transcode/universal/start.m3u8?"))
        << url;
    for (const char *part :
         {"path=%2Flibrary%2Fmetadata%2F101", "mediaIndex=0", "protocol=hls", "directPlay=0",
          "directStream=1", "session=session-2", "X-Plex-Session-Identifier=session-2",
          "X-Plex-Platform=Generic",
          "X-Plex-Client-Identifier=0f1e2d3c-4b5a-4978-8a9b-abcdefabcdef",
          "X-Plex-Client-Profile-Extra=add-transcode-target%28type%3DvideoProfile%26context%"
          "3Dstreaming%26protocol%3Dhls%26container%3Dmpegts%26videoCodec%3Dh264%2Chevc%"
          "26audioCodec%3Daac%2Cac3%2Ceac3%29"})
        EXPECT_NE(url.find(part), std::string::npos) << part;
    EXPECT_EQ(url.find("Token"), std::string::npos);
    EXPECT_EQ(prepared.start_us, 60000000u);

    // The player sends the token as a header, to this server only.
    const auto headers = plex::player_headers(playback);
    EXPECT_NE(headers.find(std::string("X-Plex-Token: ") + kServerToken), std::string::npos);
    EXPECT_NE(headers.find("X-Plex-Session-Identifier: session-2"), std::string::npos);
    EXPECT_TRUE(iptv::http::ParseExtraHeaders(headers));

    FakePlex silent;
    plex::Prepared failed;
    EXPECT_FALSE(plex::prepare(silent.transport(), &playback, 0, &failed));
    EXPECT_EQ(failed.error, "The Plex server did not answer.");
}

// ---- the timeline -------------------------------------------------------------------------

std::map<std::string, std::string> query_of(const std::string &url)
{
    std::map<std::string, std::string> out;
    const auto start = url.find('?');
    if (start == std::string::npos)
        return out;
    std::string_view rest(url);
    rest.remove_prefix(start + 1);
    while (!rest.empty())
    {
        const auto end = rest.find('&');
        const auto part = rest.substr(0, end);
        const auto equals = part.find('=');
        out[std::string(part.substr(0, equals))] =
            equals == std::string_view::npos ? "" : std::string(part.substr(equals + 1));
        if (end == std::string_view::npos)
            break;
        rest.remove_prefix(end + 1);
    }
    return out;
}

TEST(PlexTimeline, ReportsWhilePlayingAndWhenItStops)
{
    FakePlex fake;
    plex::Playback playback{server(), identity(), "101", "session-3"};
    playback.transcode = true;
    const auto timeline = std::string(kLocal) + "/:/timeline";
    std::atomic<std::uint64_t> position{1000000};
    std::atomic<bool> paused{false};
    fake.on("POST", plex::timeline_url(playback, plex::State::playing, 1000, 5400000), 200, "");
    plex::Timeline reporter;
    ASSERT_TRUE(reporter.start(
        playback,
        [&](std::uint64_t *at, std::uint64_t *duration, bool *is_paused)
        {
            *at = position.load();
            *duration = 5400000000u;
            *is_paused = paused.load();
            return true;
        },
        [&](const plex::Request &request)
        {
            auto reply = fake.transport()(request);
            reply.status = 200;
            return reply;
        },
        20));
    for (int i = 0; i < 500 && reporter.reports() < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    paused = true;
    position = 2000000;
    // One report may already be on its way with the last sample.
    const auto before = reporter.reports();
    for (int i = 0; i < 500 && reporter.reports() < before + 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto done = reporter.finish(5000000000u, 5400000000u, true);
    for (int i = 0; i < 2000 && !done->load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_TRUE(done->load());

    std::vector<std::map<std::string, std::string>> reports;
    for (const auto &request : fake.to("/:/timeline"))
    {
        EXPECT_EQ(request.method, "POST");
        EXPECT_TRUE(request.url.starts_with(timeline + "?"));
        EXPECT_TRUE(has_header(request, std::string("X-Plex-Token: ") + kServerToken));
        EXPECT_TRUE(
            has_header(request, "X-Plex-Client-Identifier: 0f1e2d3c-4b5a-4978-8a9b-abcdefabcdef"));
        EXPECT_EQ(request.url.find("Token"), std::string::npos);
        reports.push_back(query_of(request.url));
    }
    ASSERT_GE(reports.size(), 3u);
    EXPECT_EQ(reports.front().at("state"), "playing");
    EXPECT_EQ(reports.front().at("time"), "1000");
    EXPECT_EQ(reports.front().at("duration"), "5400000");
    EXPECT_EQ(reports.front().at("ratingKey"), "101");
    EXPECT_EQ(reports.front().at("key"), "%2Flibrary%2Fmetadata%2F101");
    EXPECT_EQ(reports.front().at("X-Plex-Session-Identifier"), "session-3");
    EXPECT_TRUE(
        std::any_of(reports.begin(), reports.end(), [](const auto &report)
                    { return report.at("state") == "paused" && report.at("time") == "2000"; }));
    EXPECT_EQ(reports.back().at("state"), "stopped");
    EXPECT_EQ(reports.back().at("time"), "5000000");
    // Ended: watched, and the transcoder's session freed, after the last report.
    const auto all = fake.requests();
    ASSERT_GE(all.size(), 2u);
    EXPECT_EQ(all[all.size() - 2].method, "GET");
    EXPECT_EQ(all[all.size() - 2].url,
              std::string(kLocal) + "/:/scrobble?key=101&identifier=com.plexapp.plugins.library");
    EXPECT_EQ(all.back().url,
              std::string(kLocal) + "/video/:/transcode/universal/stop?session=session-3");
}

void await(const plex::Timeline::Done &done)
{
    for (int i = 0; i < 5000 && !done->load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_TRUE(done->load());
}

TEST(PlexTimeline, OnlyACompletedTitleIsMarkedWatchedAndADeadServerIsLeftAlone)
{
    FakePlex fake;
    plex::Playback playback{server(), identity(), "101", "session-4"};
    plex::Timeline reporter;
    ASSERT_TRUE(reporter.start(
        playback, [](std::uint64_t *, std::uint64_t *, bool *) { return false; },
        [&](const plex::Request &request)
        {
            auto reply = fake.transport()(request);
            reply.status = 204;
            return reply;
        },
        10));
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    // Past 90% but left before its end: the app keeps a resume point, so Plex is not told
    // it was watched either.
    await(reporter.finish(5100000000u, 5400000000u, false));
    const auto all = fake.requests();
    ASSERT_EQ(all.size(), 1u); // nothing to say until the player has a position; then stopped
    EXPECT_EQ(query_of(all[0].url).at("state"), "stopped");
    EXPECT_TRUE(fake.to("/:/scrobble").empty());
    EXPECT_TRUE(fake.to("/transcode/universal/stop").empty()); // no transcoder was asked for

    FakePlex dead;
    plex::Timeline unanswered;
    playback.transcode = true;
    ASSERT_TRUE(unanswered.start(
        playback, [](std::uint64_t *, std::uint64_t *, bool *) { return false; }, dead.transport(),
        10));
    await(unanswered.finish(5400000000u, 5400000000u, true));
    EXPECT_EQ(dead.requests().size(), 1u); // the stopped report went unanswered: nothing more
}

TEST(PlexTimeline, StoppingDoesNotWaitForTheServer)
{
    plex::Playback playback{server(), identity(), "101", "session-6"};
    playback.transcode = true;
    std::atomic<bool> in_flight{false}, saw_cancel{false};
    std::mutex mutex;
    std::vector<std::string> sent;
    plex::Timeline reporter;
    ASSERT_TRUE(reporter.start(
        playback,
        [](std::uint64_t *at, std::uint64_t *duration, bool *paused)
        {
            *at = 1000000;
            *duration = 2000000;
            *paused = false;
            return true;
        },
        [&](const plex::Request &request)
        {
            {
                const std::lock_guard lock(mutex);
                sent.push_back(request.url);
            }
            if (request.url.find("state=playing") != std::string::npos)
            {
                // A server that never answers: only the timeline's control ends it.
                in_flight = true;
                for (int i = 0; i < 10000; ++i)
                {
                    if (request.control && request.control->cancelled &&
                        request.control->cancelled(request.control->context))
                    {
                        saw_cancel = true;
                        return plex::Reply{};
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return plex::Reply{};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            return plex::Reply{200, ""};
        },
        10));
    for (int i = 0; i < 2000 && !in_flight; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_TRUE(in_flight);
    const auto started = std::chrono::steady_clock::now();
    const auto done = reporter.finish(1500000, 2000000, true);
    // Back at once: the report on its way was stopped, and the last ones are someone else's.
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(200));
    EXPECT_TRUE(saw_cancel);
    EXPECT_FALSE(done->load());
    await(done);
    const std::lock_guard lock(mutex);
    ASSERT_EQ(sent.size(), 4u);
    EXPECT_NE(sent[1].find("state=stopped&time=1500"), std::string::npos) << sent[1];
    EXPECT_NE(sent[2].find("/:/scrobble?"), std::string::npos);
    EXPECT_NE(sent[3].find("/video/:/transcode/universal/stop?session=session-6"),
              std::string::npos);
}

// ---- the token ----------------------------------------------------------------------------

std::vector<std::string> g_lines;
void keep(const char *line)
{
    g_lines.emplace_back(line);
}

TEST(PlexToken, IsNeverLoggedAndNeverInAnAddress)
{
    g_lines.clear();
    diag::set_sink(keep);
    diag::set_enabled(true);
    FakePlex fake;
    fake.on("POST", "https://plex.tv/api/v2/pins?strong=false", 201,
            R"({"id":1,"code":"QQ11","expiresIn":900})");
    fake.on("GET", "https://plex.tv/api/v2/pins/1", 200,
            std::string(R"({"id":1,"code":"QQ11","authToken":")") + kAccountToken + "\"}");
    fake.on("GET", "https://plex.tv/api/v2/user", 200, R"({"username":"secret-name"})");
    fake.on("GET", "https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1", 200,
            kResources);
    fake.on("GET", std::string(kLocal) + "/identity", 200, identity_json(kMachine));
    fake.on("GET", std::string(kLocal) + "/library/sections", 200, kSections);
    fake.on("GET", all_url("1", 1, 0, 200), 200, page({movie(101, "Film", kHevcTrueHd)}, 1));
    fake.on("GET", std::string(kLocal) + "/library/metadata/101", 200, metadata_reply(kHevcTrueHd));

    plex::SignIn sign_in;
    ASSERT_TRUE(sign_in.start(identity(), fake.transport(), 1));
    for (int i = 0; i < 2000 && !sign_in.finished(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(sign_in.step(), plex::SignIn::Step::done);
    (void)plex::check_server(fake.transport(), identity(), server(), kAccountToken);
    iptv::Catalog catalog;
    std::string error;
    ASSERT_TRUE(ptv::load_plex_vod(server(), identity(), ptv::VodKind::movies, {}, &catalog,
                                   nullptr, &error, nullptr, nullptr, 0, fake.transport()));
    plex::Playback playback{server(), identity(), "101", "session-5"};
    plex::Prepared prepared;
    ASSERT_TRUE(plex::prepare(fake.transport(), &playback, 0, &prepared));
    plex::Timeline reporter;
    ASSERT_TRUE(reporter.start(
        playback,
        [](std::uint64_t *at, std::uint64_t *duration, bool *paused)
        {
            *at = 1;
            *duration = 2;
            *paused = false;
            return true;
        },
        fake.transport(), 5));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    await(reporter.finish(1, 2, true));
    diag::set_enabled(false);
    diag::set_sink(nullptr);

    ASSERT_FALSE(g_lines.empty());
    for (const auto &line : g_lines)
        for (const char *secret : {kAccountToken, kServerToken, kSharedToken, "secret-name"})
            EXPECT_EQ(line.find(secret), std::string::npos) << line;
    for (const auto &request : fake.requests())
        for (const char *secret : {kAccountToken, kServerToken, kSharedToken})
            EXPECT_EQ(request.url.find(secret), std::string::npos) << request.url;
    for (const auto &url : prepared.urls)
        EXPECT_EQ(url.find(kServerToken), std::string::npos);
}

// ---- Plex as a source of the app ----------------------------------------------------------

std::string read_file(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

class PlexModelTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        host::reset();
        char pattern[] = "/tmp/nightshade-plex-model-XXXXXX";
        dir = mkdtemp(pattern);
        host::set_network(true, "");
        host::set_api_response("POST", "https://plex.tv/api/v2/pins?strong=false", 201,
                               R"({"id":99,"code":"PS55","expiresIn":900})");
        host::set_api_response("GET", "https://plex.tv/api/v2/pins/99", 200,
                               std::string(R"({"id":99,"code":"PS55","authToken":")") +
                                   kAccountToken + "\"}");
        host::set_api_response("GET", "https://plex.tv/api/v2/user", 200,
                               R"({"friendlyName":"Andrew"})");
        host::set_api_response("GET",
                               "https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1",
                               200, kResources);
        host::set_api_response("GET", std::string(kLocal) + "/identity", 200,
                               identity_json(kMachine));
        host::set_api_response("GET", std::string(kLocal) + "/library/sections", 200, kSections);
        host::set_api_response("GET", all_url("1", 1, 0, 200), 200,
                               page({movie(101, "Film", kMkv)}, 1));
        host::set_api_response("GET", all_url("2", 2, 0, 200), 200,
                               R"({"MediaContainer":{"size":0,"totalSize":0,"Metadata":[]}})");
    }
    void TearDown() override
    {
        host::reset();
        std::filesystem::remove_all(dir);
    }
    bool settle(Model &model)
    {
        for (int i = 0; i < 2000; ++i)
        {
            model.poll();
            if (!model.plex_signing_in() && !model.refreshing() && !model.vod().busy() &&
                !model.vod().requested())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
    std::string dir;
};

TEST_F(PlexModelTest, SignInMakesServersSourcesAndPlaysWithTheTokenInAHeaderOnly)
{
    std::string client_id;
    {
        Model model(dir);
        model.set_plex_poll_interval(1);
        model.set_app_version("01.002.000");
        ASSERT_TRUE(model.open());
        client_id = model.plex_identity().client_id;
        EXPECT_TRUE(plex::valid_client_id(client_id));
        model.add_source(iptv::SourceKind::Plex);
        ASSERT_TRUE(model.plex_signing_in());
        ASSERT_TRUE(settle(model));
        EXPECT_TRUE(model.plex_signed_in());
        EXPECT_EQ(model.plex_account_name(), "Andrew");

        // Each server that answered is a source; the server's token is its password, as an
        // Xtream password. The shared one answered nowhere: it is not saved at an address that
        // never proved to be it, and the viewer is told.
        std::vector<SavedSource> plex_sources;
        for (const auto &source : model.saved_sources())
            if (source.kind == 6)
                plex_sources.push_back(source);
        ASSERT_EQ(plex_sources.size(), 1u);
        const auto notices = model.take_notices();
        EXPECT_TRUE(
            std::any_of(notices.begin(), notices.end(), [](const Notice &notice)
                        { return notice.title == "Some Plex servers could not be reached"; }));
        // Its plex.direct name is dialled at its address from now on, by every connection.
        EXPECT_EQ(host::pinned_hosts(),
                  (std::vector<std::string>{"192-168-1-5.aaaabbbb.plex.direct:32400:192.168.1.5"}));
        EXPECT_EQ(plex_sources[0].name, "Living Room");
        EXPECT_EQ(plex_sources[0].url, kLocal);
        EXPECT_EQ(plex_sources[0].username, "Andrew");
        EXPECT_EQ(plex_sources[0].password, kServerToken);
        EXPECT_EQ(plex_sources[0].mac, kMachine);
        EXPECT_EQ(model.active_source(), iptv::SourceKind::Plex);
        EXPECT_EQ(model.selected_source_id(), plex_sources[0].id);
        EXPECT_FALSE(model.has_catalog()); // no live channels

        // Its films are the On Demand list; their posters carry the token to that server only.
        model.vod().select(VodKind::movies);
        ASSERT_TRUE(settle(model));
        ASSERT_EQ(model.vod().catalog().size(), 1u);
        EXPECT_EQ(model.image_credentials().origin, kLocal);
        EXPECT_EQ(model.image_credentials().headers,
                  (std::vector<std::string>{std::string("X-Plex-Token: ") + kServerToken}));

        ASSERT_TRUE(model.play_vod(0));
        PlayRequest request;
        ASSERT_TRUE(model.take_play_request(&request));
        EXPECT_TRUE(request.on_demand);
        EXPECT_TRUE(request.plex.active());
        EXPECT_EQ(request.plex.rating_key, "101");
        EXPECT_EQ(request.credential_origin, kLocal);
        EXPECT_NE(request.extra_headers.find(std::string("X-Plex-Token: ") + kServerToken),
                  std::string::npos);
        for (const auto &url : request.urls)
            EXPECT_EQ(url.find(kServerToken), std::string::npos);
        model.close();
    }
    // The account is kept with the app's data, and the console keeps its identifier.
    plex::Account saved;
    ASSERT_TRUE(plex::load_account(dir + "/prosperotv-plex-v1.txt", &saved));
    EXPECT_EQ(saved.token, kAccountToken);
    EXPECT_EQ(saved.client_id, client_id);
    for (const auto &request : host::api_requests())
        EXPECT_TRUE(std::any_of(request.headers.begin(), request.headers.end(),
                                [&](const std::string &header)
                                { return header == "X-Plex-Client-Identifier: " + client_id; }))
            << request.url;

    Model again(dir);
    ASSERT_TRUE(again.open());
    EXPECT_TRUE(again.plex_signed_in());
    EXPECT_EQ(again.plex_identity().client_id, client_id);
    EXPECT_EQ(again.active_source(), iptv::SourceKind::Plex);
    ASSERT_TRUE(settle(again));
    ASSERT_TRUE(again.plex_sign_out());
    EXPECT_FALSE(again.plex_signed_in());
    for (const auto &source : again.saved_sources())
        EXPECT_NE(source.kind, 6);
    EXPECT_NE(again.active_source(), iptv::SourceKind::Plex);
    ASSERT_TRUE(plex::load_account(dir + "/prosperotv-plex-v1.txt", &saved));
    EXPECT_FALSE(saved.signed_in());
    EXPECT_EQ(saved.client_id, client_id);
    // The servers' rows were overwritten, not only unlinked: the file keeps no token.
    EXPECT_EQ(read_file(dir + "/prosperotv-library.sqlite3").find(kServerToken), std::string::npos);
    again.close();
}

TEST_F(PlexModelTest, AServerNotReachedThisLaunchIsSentNothingWithItsToken)
{
    std::int64_t id = 0;
    {
        Library library;
        ASSERT_TRUE(library.open(dir + "/prosperotv-library.sqlite3"));
        SavedSource source{
            0, 6, "Living Room", kLocal, "Andrew", kServerToken, kMachine, RefreshSchedule::daily};
        ASSERT_TRUE(library.save_source(&source));
        ASSERT_TRUE(library.select_source(source.id));
        id = source.id;
    }
    ASSERT_TRUE(plex::save_account(dir + "/prosperotv-plex-v1.txt",
                                   {identity().client_id, kAccountToken, "Andrew"}));
    // Neither plex.tv nor the server answers this time.
    host::reset();
    host::set_network(true, "");
    Model model(dir);
    ASSERT_TRUE(model.open());
    ASSERT_TRUE(settle(model));
    EXPECT_EQ(model.active_source(), iptv::SourceKind::Plex);
    EXPECT_EQ(model.plex_reach(id), 0);
    EXPECT_TRUE(model.image_credentials().origin.empty());
    EXPECT_TRUE(model.image_credentials().headers.empty());

    // The films saved at an earlier launch are still listed...
    {
        FakePlex earlier;
        earlier.on("GET", std::string(kLocal) + "/library/sections", 200, kSections);
        earlier.on("GET", all_url("1", 1, 0, 200), 200, page({movie(101, "Film", kMkv)}, 1));
        iptv::Catalog films;
        std::string error;
        ASSERT_TRUE(ptv::load_plex_vod(server(), identity(), VodKind::movies, {}, &films, nullptr,
                                       &error, nullptr, nullptr, 0, earlier.transport()));
        ASSERT_EQ(iptv::SaveCatalog(model.vod().cache_file(VodKind::movies), films,
                                    {.allow_empty = true}),
                  iptv::StoreStatus::ok);
    }
    model.vod().select(VodKind::movies);
    ASSERT_TRUE(settle(model));
    ASSERT_EQ(model.vod().catalog().size(), 1u);
    // ...but nothing plays from a server that did not answer.
    EXPECT_FALSE(model.play_vod(0));
    PlayRequest request;
    EXPECT_FALSE(model.take_play_request(&request));

    // A token went only to plex.tv; the saved address was asked, over https and with none,
    // who it is, and nothing else.
    std::vector<std::string> to_saved;
    for (const auto &asked : host::api_requests())
    {
        if (std::any_of(asked.headers.begin(), asked.headers.end(), [](const std::string &header)
                        { return header.starts_with("X-Plex-Token"); }))
            EXPECT_TRUE(asked.url.starts_with("https://plex.tv/")) << asked.url;
        if (asked.url.starts_with(kLocal))
            to_saved.push_back(asked.url);
    }
    EXPECT_EQ(to_saved, (std::vector<std::string>{std::string(kLocal) + "/identity"}));
    model.close();
}

TEST_F(PlexModelTest, BackupsKeepNoPlexServerAndNoToken)
{
    const std::string config = dir + "/config", drive = dir + "/usb0", other = dir + "/other";
    for (const auto &folder : {config, drive, other})
        std::filesystem::create_directory(folder);
    {
        Library library;
        ASSERT_TRUE(library.open(config + "/prosperotv-library.sqlite3"));
        SavedSource plex_source{
            0, 6, "Living Room", kLocal, "Andrew", kServerToken, kMachine, RefreshSchedule::daily};
        SavedSource xtream{0,           2,
                           "Family TV", "https://provider.invalid",
                           "user",      "xtream-secret",
                           {},          RefreshSchedule::daily};
        ASSERT_TRUE(library.save_source(&plex_source));
        ASSERT_TRUE(library.save_source(&xtream));
        ASSERT_TRUE(library.select_source(plex_source.id));
    }
    std::string error;
    const auto archive = drive + "/ProsperoTV-backup.sqlite3";
    ASSERT_TRUE(ptv::backup_settings(config, archive, error)) << error;
    const auto bytes = read_file(archive);
    EXPECT_EQ(bytes.find(kServerToken), std::string::npos);
    EXPECT_NE(bytes.find("xtream-secret"), std::string::npos); // the library itself is there
    EXPECT_FALSE(std::filesystem::exists(config + "/prosperotv-backup-library.tmp"));
    // The console keeps its Plex server; a console restored from the backup does not.
    {
        Library library;
        ASSERT_TRUE(library.open(config + "/prosperotv-library.sqlite3"));
        const auto kept = library.sources();
        EXPECT_TRUE(std::any_of(kept.begin(), kept.end(),
                                [](const SavedSource &source) { return source.kind == 6; }));
    }
    ASSERT_TRUE(ptv::restore_settings(other, archive, error)) << error;
    Library restored;
    ASSERT_TRUE(restored.open(other + "/prosperotv-library.sqlite3"));
    const auto sources = restored.sources();
    EXPECT_TRUE(std::none_of(sources.begin(), sources.end(),
                             [](const SavedSource &source) { return source.kind == 6; }));
    EXPECT_TRUE(std::any_of(sources.begin(), sources.end(),
                            [](const SavedSource &source) { return source.kind == 2; }));
}

} // namespace
