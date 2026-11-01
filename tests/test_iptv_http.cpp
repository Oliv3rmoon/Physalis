/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_http.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace
{

std::string Describe(iptv::http::Status status, int http_status = 0, int native_error = 0,
                     const char *response = nullptr)
{
    char text[192]{};
    iptv::http::DescribeFailure(status, http_status, native_error, response, text, sizeof(text));
    return text;
}

TEST(IptvHttpTest, DetectsGeoIpBlockCaseInsensitively)
{
    constexpr char response[] = "This channel is GEOIP BLOCKED in your region";
    EXPECT_TRUE(iptv::http::ResponseIndicatesGeographicBlock(response, std::strlen(response)));
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 403, 0, response),
              "unavailable in your region (GeoIP blocked; HTTP 403)");
}

TEST(IptvHttpTest, RedirectsKeepPortalCredentialsOnlyOnTheOriginalOrigin)
{
    const iptv::http::RequestHeaders headers{"ProsperoTV", "https://portal.invalid/c/",
                                             "mac=00:11:22:33:44:55", "Bearer token"};
    for (const auto target : {"https://portal.invalid/other", "HTTPS://PORTAL.invalid:443/other"})
    {
        const auto kept =
            iptv::http::HeadersForUrl("https://portal.invalid/server/load.php", target, headers);
        EXPECT_EQ(kept.cookie, headers.cookie);
        EXPECT_EQ(kept.authorization, headers.authorization);
    }
    for (const auto target :
         {"http://portal.invalid/other", "https://cdn.invalid/stream",
          "https://portal.invalid:444/stream", "https://portal.invalid.evil/", "file:///other"})
    {
        const auto kept =
            iptv::http::HeadersForUrl("https://portal.invalid/server/load.php", target, headers);
        EXPECT_EQ(kept.cookie, nullptr);
        EXPECT_EQ(kept.authorization, nullptr);
        EXPECT_EQ(kept.user_agent, headers.user_agent);
    }
}

TEST(IptvHttpTest, PlaylistSegmentCredentialsStayOnTheConfiguredServer)
{
    iptv::http::RequestHeaders headers{};
    headers.authorization = "Basic fixture";
    headers.credential_origin = "http://tv.invalid:9981";
    const auto own = iptv::http::HeadersForUrl("http://tv.invalid:9981/list.m3u8",
                                               "http://tv.invalid:9981/segment.ts", headers);
    EXPECT_EQ(own.authorization, headers.authorization);
    const auto foreign = iptv::http::HeadersForUrl("http://cdn.invalid/segment.ts",
                                                   "http://cdn.invalid/segment.ts", headers);
    EXPECT_EQ(foreign.authorization, nullptr);
    const auto port = iptv::http::HeadersForUrl("http://tv.invalid:9981/list.m3u8",
                                                "http://tv.invalid/segment.ts", headers);
    EXPECT_EQ(port.authorization, nullptr);
}

TEST(IptvHttpTest, DoesNotGuessThatEveryForbiddenResponseIsGeographic)
{
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 403, 0, "Forbidden"),
              "access denied by the channel provider (HTTP 403)");
}

TEST(IptvHttpTest, ValidatesTheByteRangeBeforeSeekingInAMovie)
{
    std::int64_t size = -1;
    EXPECT_TRUE(iptv::http::ParseStreamRange("Content-Length: 9000000000\r\n", 200, 0, &size));
    EXPECT_EQ(size, 9000000000);
    EXPECT_FALSE(iptv::http::ParseStreamRange("Content-Length: 1000\r\n", 200, 200, &size));
    EXPECT_TRUE(iptv::http::ParseStreamRange(
        "content-range: bytes 200-999/1000\r\nContent-Length: 800\r\n", 206, 200, &size));
    EXPECT_EQ(size, 1000);
    for (const auto headers :
         {"Content-Range: bytes 0-999/1000\r\n", "Content-Range: bytes 200-1000/1000\r\n",
          "Content-Range: bytes 200-999/1000\r\nContent-Length: 799\r\n",
          "Content-Range: bytes 200-999/1000\r\nContent-Range: bytes 200-999/1000\r\n",
          "Content-Length: 18446744073709551615\r\n"})
        EXPECT_FALSE(iptv::http::ParseStreamRange(headers, 206, 200, &size));
    EXPECT_TRUE(
        iptv::http::ParseStreamRange("Content-Range: bytes 200-999/*\r\n", 206, 200, &size));
    EXPECT_EQ(size, -1);
    EXPECT_TRUE(iptv::http::ParseStreamRange("Transfer-Encoding: chunked\r\n", 200, -1, &size));
    EXPECT_EQ(size, -1);
}

TEST(IptvHttpTest, AcceptsARedundantFullFileRangeButNeverAnIgnoredSeek)
{
    std::int64_t size = -1;
    constexpr auto full =
        "Content-Length: 9000000000\r\nContent-Range: bytes 0-8999999999/9000000000\r\n";
    for (const auto offset : {-1, 0})
    {
        EXPECT_TRUE(iptv::http::ParseStreamRange(full, 200, offset, &size));
        EXPECT_EQ(size, 9000000000);
    }
    EXPECT_FALSE(iptv::http::ParseStreamRange(full, 200, 200, &size));
    for (const auto headers :
         {"Content-Range: bytes 0-499/1000\r\n", "Content-Range: bytes 200-999/1000\r\n",
          "Content-Range: bytes 0-999/*\r\n",
          "Content-Length: 999\r\nContent-Range: bytes 0-999/1000\r\n"})
        EXPECT_FALSE(iptv::http::ParseStreamRange(headers, 200, -1, &size));
}

TEST(IptvHttpTest, NamesASegmentsContentTypeWithoutItsParameters)
{
    char type[64];
    EXPECT_TRUE(iptv::http::ContentTypeOf(
        "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nContent-Type: Video/MP2T; charset=x\r\n", type,
        sizeof(type)));
    EXPECT_STREQ(type, "video/mp2t");
    EXPECT_TRUE(iptv::http::ContentTypeOf("content-type:image/png\n", type, sizeof(type)));
    EXPECT_STREQ(type, "image/png");
    EXPECT_FALSE(iptv::http::ContentTypeOf("Content-Length: 4\r\nX-Content-Type: a/b\r\n", type,
                                           sizeof(type)));
    EXPECT_STREQ(type, "");
    char small[6];
    EXPECT_TRUE(iptv::http::ContentTypeOf("Content-Type: video/mp4\r\n", small, sizeof(small)));
    EXPECT_STREQ(small, "video");
}

TEST(IptvHttpTest, GivesOnlyTheExtensionOfASegmentAddress)
{
    char extension[12];
    iptv::http::UrlExtension("http://user:secret@192.0.2.1:8989/live/a/b/1234.TS?token=abc",
                             extension, sizeof(extension));
    EXPECT_STREQ(extension, "ts");
    iptv::http::UrlExtension("https://example.test/seg/chunk-17.m4s#x", extension,
                             sizeof(extension));
    EXPECT_STREQ(extension, "m4s");
    iptv::http::UrlExtension("https://example.test/v1.2/segment", extension, sizeof(extension));
    EXPECT_STREQ(extension, "");
    iptv::http::UrlExtension("https://cdn.example.test", extension, sizeof(extension));
    EXPECT_STREQ(extension, "");
    iptv::http::UrlExtension("https://example.test/a.b@c", extension, sizeof(extension));
    EXPECT_STREQ(extension, "");
    iptv::http::UrlExtension("https://example.test/a.verylongext", extension, sizeof(extension));
    EXPECT_STREQ(extension, "");
}

TEST(IptvHttpTest, ExplainsStandardHttpFailures)
{
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 404),
              "stream is offline or no longer exists (HTTP 404)");
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 451),
              "unavailable for legal or regional restrictions (HTTP 451)");
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 503),
              "the channel provider is unavailable (HTTP 503)");
}

TEST(IptvHttpTest, PreservesNativeConnectionErrorForLogsAndUi)
{
    EXPECT_EQ(Describe(iptv::http::Status::request_failed, 0, -42),
              "connection failed (native error 0xFFFFFFD6)");
    // The system library's own errors are not libcurl's.
    EXPECT_EQ(
        Describe(iptv::http::Status::request_failed, 0, static_cast<int>(UINT32_C(0x80431100))),
        "connection failed (native error 0x80431100)");
}

TEST(IptvHttpTest, ReadsLibcurlCodesAndTheStageOfATimeout)
{
    using iptv::http::CurlCode;
    using iptv::http::CurlFailure;
    using iptv::http::CurlStage;
    CurlStage stage = CurlStage::answer;
    // 0xFFFFD8D4, as a tester saw it: libcurl's 28 with no stage.
    EXPECT_EQ(CurlCode(-10028, &stage), 28);
    EXPECT_EQ(stage, CurlStage::unknown);
    EXPECT_EQ(CurlFailure(28), static_cast<int>(UINT32_C(0xFFFFD8D4)));
    EXPECT_EQ(CurlFailure(28, CurlStage::connect), -12028);
    for (const auto expected :
         {CurlStage::lookup, CurlStage::connect, CurlStage::secure, CurlStage::answer})
    {
        EXPECT_EQ(CurlCode(CurlFailure(28, expected), &stage), 28);
        EXPECT_EQ(stage, expected);
    }
    EXPECT_EQ(CurlCode(-10060, &stage), 60);
    EXPECT_EQ(stage, CurlStage::unknown);
    // Only a timeout has a stage; nothing outside libcurl's range is read as one.
    for (const int other : {0, -1, -42, -10000, -12060, -15028, -9999, 10028,
                            static_cast<int>(UINT32_C(0x80431100)), INT32_MIN})
        EXPECT_EQ(CurlCode(other), 0) << other;
}

TEST(IptvHttpTest, PutsConnectionFailuresInPlainWords)
{
    using iptv::http::CurlFailure;
    using iptv::http::CurlStage;
    const auto failed = [](int native)
    { return Describe(iptv::http::Status::request_failed, 0, native); };
    EXPECT_EQ(failed(-10028), "the channel's server did not answer in time (curl 28)");
    EXPECT_EQ(failed(CurlFailure(28, CurlStage::lookup)),
              "looking up the channel's server took too long (curl 28)");
    EXPECT_EQ(failed(CurlFailure(28, CurlStage::connect)),
              "the channel's server could not be reached in time; it may be offline or blocked "
              "on this network (curl 28)");
    EXPECT_EQ(failed(CurlFailure(28, CurlStage::secure)),
              "the secure connection to the channel's server timed out (curl 28)");
    EXPECT_EQ(failed(CurlFailure(28, CurlStage::answer)),
              "the channel's server took the request but did not answer in time (curl 28)");
    EXPECT_EQ(failed(CurlFailure(6)), "the channel's server could not be found (curl 6)");
    EXPECT_EQ(failed(CurlFailure(7)), "the channel's server refused the connection (curl 7)");
    EXPECT_EQ(failed(CurlFailure(60)), "the channel's server certificate was rejected (curl 60)");
    EXPECT_EQ(failed(CurlFailure(35)),
              "the secure connection to the channel's server failed (curl 35)");
    EXPECT_EQ(failed(CurlFailure(56)), "the connection to the channel's server was cut (curl 56)");
    EXPECT_EQ(failed(CurlFailure(99)), "connection failed (curl 99)");
    // What the player shows fits its message with the longest context in front.
    char text[160];
    iptv::http::DescribeFailure(iptv::http::Status::request_failed, 0,
                                CurlFailure(28, CurlStage::connect), nullptr, text, sizeof(text));
    EXPECT_LT(std::strlen("Channel segment request failed: .") + std::strlen(text), 160u);
}

TEST(IptvHttpTest, AServerThatCannotBeReachedIsNotAskedAgain)
{
    using iptv::http::CurlFailure;
    using iptv::http::CurlStage;
    using iptv::http::ServerUnreachable;
    using iptv::http::Status;
    EXPECT_TRUE(ServerUnreachable(Status::request_failed, CurlFailure(6)));
    EXPECT_TRUE(ServerUnreachable(Status::request_failed, CurlFailure(7)));
    EXPECT_TRUE(ServerUnreachable(Status::request_failed, CurlFailure(28, CurlStage::lookup)));
    EXPECT_TRUE(ServerUnreachable(Status::request_failed, CurlFailure(28, CurlStage::connect)));
    // A server that answered, or a timeout whose stage is not known, is asked again.
    EXPECT_FALSE(ServerUnreachable(Status::request_failed, CurlFailure(28)));
    EXPECT_FALSE(ServerUnreachable(Status::request_failed, CurlFailure(28, CurlStage::secure)));
    EXPECT_FALSE(ServerUnreachable(Status::request_failed, CurlFailure(28, CurlStage::answer)));
    EXPECT_FALSE(ServerUnreachable(Status::request_failed, CurlFailure(60)));
    EXPECT_FALSE(ServerUnreachable(Status::request_failed, -42));
    EXPECT_FALSE(ServerUnreachable(Status::http_status_error, CurlFailure(7)));
    EXPECT_FALSE(ServerUnreachable(Status::read_failed, CurlFailure(28, CurlStage::connect)));
}

} // namespace
