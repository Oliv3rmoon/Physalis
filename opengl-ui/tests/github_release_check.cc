// Physalis - Host check of the update check against a GitHub repository's releases.
// SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Nightshade's update_check_check.cpp, for this repository's releases: ps5/src/update_kit/
// github_release.{h,c}, built with the kit's files it uses (update_check.c without its network
// side, self_update.c, self_update_sha256.c), through a stand-in for the network: the release's
// answer, its tag, SHA256SUMS, the notes, the decision, the addresses asked and a download may be
// made from, and that the catalog (which lists ProsperoTV's own releases) is never asked.
// tests/test_github_release.py builds and runs it (make test-integration).
#include "update_kit/github_release.h"
#include "update_kit/self_update.h"
#include "update_kit/update_check.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{

// releases/latest as GitHub answers it for a release the workflow made from tag 01.001.010
// (.github/workflows/tooling.yml: the tag is the contentVersion, the files PPSA99003.zip and
// SHA256SUMS), pretty-printed and shortened to the fields that matter; Compact() gives it as
// GitHub sends it.
const std::string kRelease = R"json({
  "url": "https://api.github.com/repos/Oliv3rmoon/Physalis/releases/417000001",
  "html_url": "https://github.com/Oliv3rmoon/Physalis/releases/tag/01.001.010",
  "id": 417000001,
  "author": {
    "login": "github-actions[bot]",
    "id": 41898282,
    "type": "Bot"
  },
  "tag_name": "01.001.010",
  "target_commitish": "main",
  "name": "Physalis 01.001.010",
  "draft": false,
  "immutable": false,
  "prerelease": false,
  "created_at": "2026-11-02T10:00:00Z",
  "published_at": "2026-11-02T10:20:00Z",
  "assets": [
    {
      "url": "https://api.github.com/repos/Oliv3rmoon/Physalis/releases/assets/653000001",
      "id": 653000001,
      "name": "PPSA99003.zip",
      "label": "",
      "uploader": {
        "login": "github-actions[bot]",
        "type": "Bot"
      },
      "content_type": "application/zip",
      "state": "uploaded",
      "size": 61234567,
      "digest": "sha256:5b2f0c9e7a1d4e3f8b6a0c2d9e1f7a3b5c8d0e2f4a6b8c0d2e4f6a8b0c2d4e6f",
      "download_count": 0,
      "browser_download_url": "https://github.com/Oliv3rmoon/Physalis/releases/download/01.001.010/PPSA99003.zip"
    },
    {
      "url": "https://api.github.com/repos/Oliv3rmoon/Physalis/releases/assets/653000002",
      "id": 653000002,
      "name": "SHA256SUMS",
      "label": "",
      "uploader": {
        "login": "github-actions[bot]",
        "type": "Bot"
      },
      "content_type": "application/octet-stream",
      "state": "uploaded",
      "size": 80,
      "download_count": 0,
      "browser_download_url": "https://github.com/Oliv3rmoon/Physalis/releases/download/01.001.010/SHA256SUMS"
    }
  ],
  "tarball_url": "https://api.github.com/repos/Oliv3rmoon/Physalis/tarball/01.001.010",
  "zipball_url": "https://api.github.com/repos/Oliv3rmoon/Physalis/zipball/01.001.010",
  "body": "## New since 01.001.000\r\n\r\n### Watching\r\n\r\n- **Faster zapping.** Channels open\r\n  sooner. See [the README](https://github.com/Oliv3rmoon/Physalis#readme).\r\n- `SHA256SUMS` is checked.\r\n\r\n> [!NOTE]\r\n> Your sources stay as they are.\r\n\r\nThanks to everyone testing — and to **BlackBearReloaded**.",
  "reactions": {
    "total_count": 1,
    "heart": 1
  }
})json";

constexpr const char *kRepository = "Oliv3rmoon/Physalis";
constexpr const char *kName = "Physalis";
constexpr const char *kTitle = "PPSA99003";
constexpr const char *kApi = "https://api.github.com/repos/Oliv3rmoon/Physalis/releases/latest";
constexpr const char *kSums =
    "https://github.com/Oliv3rmoon/Physalis/releases/download/01.001.010/SHA256SUMS";
constexpr const char *kZip =
    "https://github.com/Oliv3rmoon/Physalis/releases/download/01.001.010/PPSA99003.zip";
// The repository under an earlier name, which GitHub sends on to the new one.
constexpr const char *kOldRepository = "Oliv3rmoon/ProsperoTV-Nightshade";
constexpr const char *kOldApi =
    "https://api.github.com/repos/Oliv3rmoon/ProsperoTV-Nightshade/releases/latest";
constexpr const char *kMoved = "https://api.github.com/repositories/1422000007/releases/latest";
constexpr const char *kDigest = "3f6c2a7e9b1d4c8a0e5f7b2d6a9c1e4f8b3d7a0c5e9f2b6d1a4c8e0f3b7d9a2c";
const std::string kSumsText = std::string(kDigest) + "  PPSA99003.zip\n";

// What the What's new view gets from the release's Markdown.
constexpr const char *kNotes = "New since 01.001.000\n"
                               "\n"
                               "Watching\n"
                               "\n"
                               "- Faster zapping. Channels open sooner. See the README.\n"
                               "- SHA256SUMS is checked.\n"
                               "\n"
                               "Note: Your sources stay as they are.\n"
                               "\n"
                               "Thanks to everyone testing — and to BlackBearReloaded.";

std::string With(std::string text, const std::string &from, const std::string &to)
{
    const std::size_t at = text.find(from);
    assert(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

std::string WithAll(std::string text, const std::string &from, const std::string &to)
{
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
    return text;
}

// The answer without the spaces and line ends between its values.
std::string Compact(const std::string &json)
{
    std::string out;
    bool in_string = false, escaped = false;
    for (const char c : json)
    {
        if (in_string)
        {
            out += c;
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                in_string = false;
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        in_string = c == '"';
        out += c;
    }
    return out;
}

github_release_reason Evaluate(const std::string &json, const char *installed,
                               github_release *release = nullptr)
{
    static github_release scratch;
    return github_release_evaluate(json.data(), json.size(), installed, "PPSA99003.zip",
                                   release ? release : &scratch);
}

std::string Notes(const std::string &markdown,
                  std::size_t characters = GITHUB_RELEASE_NOTES_CHARACTERS,
                  int *truncated = nullptr)
{
    static char out[16384];
    int cut = 0;
    github_release_notes(markdown.data(), markdown.size(), out, sizeof(out), characters, &cut);
    if (truncated)
        *truncated = cut;
    return out;
}

int Sums(const std::string &text, std::string *digest = nullptr)
{
    char sha256[65] = {};
    const int found = github_release_sums(text.data(), text.size(), "PPSA99003.zip", sha256);
    if (digest)
        *digest = sha256;
    return found;
}

// The network, stood in for: what each address answers, and every address asked, in order.
struct Network
{
    std::map<std::string, std::pair<int, std::string>> answers; // fetch: status and body
    std::map<std::string, std::string> files;                   // download: the file (status 200)
    std::vector<std::string> asked;
    int failure = 0; // every request fails with this code
};
Network network;

int Fetch(void *, const char *url, char *body, size_t capacity, size_t *length, int *status)
{
    network.asked.push_back(url);
    *length = 0;
    *status = 0;
    if (network.failure)
        return network.failure;
    const auto found = network.answers.find(url);
    if (found == network.answers.end())
    {
        *status = 404;
        return 0;
    }
    if (found->second.second.size() > capacity)
    {
        // As console_fetch: told from a failed transfer.
        *status = found->second.first;
        return SELF_UPDATE_FETCH_TOO_LARGE;
    }
    std::memcpy(body, found->second.second.data(), found->second.second.size());
    *length = found->second.second.size();
    *status = found->second.first;
    return 0;
}

// As console_download: only from github.com, at most `limit` bytes, 0 for a complete 200.
int Download(void *, const char *url, std::uint64_t limit, self_update_sink sink, void *user)
{
    network.asked.push_back(url);
    if (network.failure)
        return network.failure;
    if (!self_update_url_allowed(url, 0))
        return -1;
    const auto found = network.files.find(url);
    if (found == network.files.end())
        return -404;
    if (found->second.size() > limit)
        return -10023;
    return sink(user, found->second.data(), found->second.size()) == 1 ? 0 : -10023;
}

int Verify(void *, const unsigned char *, const unsigned char *, const void *, size_t)
{
    return 0;
}
int OpenHelper(void *, self_update_channel *)
{
    return 0;
}
int LoadSequence(void *, std::uint64_t *)
{
    return 0;
}
int SaveSequence(void *, std::uint64_t)
{
    return 0;
}
std::uint64_t Now(void *)
{
    return 0;
}
const self_update_platform kPlatform = {Fetch,        Download,     Verify, OpenHelper,
                                        LoadSequence, SaveSequence, Now,    nullptr};

self_update_check_result Check(const char *repository, const char *installed,
                               self_update_offer *offer, github_release_report *report,
                               const char *title = kTitle, const char *name = kName)
{
    network.asked.clear();
    return github_release_check(&kPlatform, repository, name, title, installed, offer, report);
}

void Serve(int status, const std::string &body)
{
    network = Network{};
    network.answers[kApi] = {status, body};
    network.files[kSums] = kSumsText;
}

void TagVersions()
{
    // The workflow tags a release with its contentVersion: the tag is the version itself.
    for (const char *version : {"01.001.000", "10.000.000", "00.001.000", "09.999.999"})
    {
        char back[12] = {};
        assert(github_release_tag_version(version, back) == 1 && std::strcmp(back, version) == 0);
    }
    for (const char *tag : {"v01.001.000", "v1.001.000", "1.001.000", "01.001.000-rc1", "01.1.0",
                            "01.001.000 ", " 01.001.000", "build-cache", "", "01.001.00x",
                            "001.001.000"})
        assert(github_release_tag_version(tag, nullptr) == 0);
    assert(github_release_tag_version(nullptr, nullptr) == 0);
}

void Addresses()
{
    char url[256];
    assert(github_release_url(url, sizeof(url), kRepository) != 0 && std::strcmp(url, kApi) == 0);
    for (const char *bad : {"Oliv3rmoon", "/Physalis", "Oliv3rmoon/", "Oliv3rmoon/a/b", "-x/y",
                            "x/..", "x/.", "a b/c", "x/y?z", "x/y#z", "x_y/z"})
        assert(github_release_url(url, sizeof(url), bad) == 0 && url[0] == '\0');
    assert(github_release_repository_valid("o-1/.github") &&
           github_release_repository_valid("x/y.z_w-1"));
    assert(github_release_url(url, 40, kRepository) == 0);
    assert(github_release_file_url(url, sizeof(url), kRepository, "01.001.010", "PPSA99003.zip") !=
               0 &&
           std::strcmp(url, kZip) == 0);
    assert(github_release_file_url(url, sizeof(url), kRepository, "build-cache", "SHA256SUMS") == 0);
    assert(github_release_file_url(url, sizeof(url), kRepository, "01.001.010", "../SHA256SUMS") ==
           0);
    // A download starts on github.com, and its redirect may go only to GitHub's release file host.
    assert(self_update_url_allowed(kZip, 0) == 1 && self_update_url_allowed(kSums, 0) == 1);
    assert(self_update_url_allowed(
               "https://release-assets.githubusercontent.com/github-production-release-asset/1/2", 1) ==
           1);
    assert(self_update_url_allowed(
               "https://release-assets.githubusercontent.com/github-production-release-asset/1/2", 0) ==
           0);
    assert(self_update_url_allowed("https://prosperostore.net/api/v1/apps/PPSA99003.json", 0) == 0);
    // The release's own request follows GitHub's redirect for a renamed repository to the address
    // GitHub gives it, exactly, and to nothing else.
    assert(github_release_api_url_allowed(kMoved) == 1);
    for (const char *elsewhere :
         {kApi, "https://api.github.com/repos/someone/else/releases/latest",
          "https://api.github.com/repositories/1422000007/releases/1",
          "https://api.github.com/repositories//releases/latest",
          "https://api.github.com/repositories/01422000007/releases/latest",
          "https://api.github.com/repositories/12345678901234567890/releases/latest",
          "https://api.github.com.example.net/repositories/1422000007/releases/latest",
          "http://api.github.com/repositories/1422000007/releases/latest",
          "https://api.github.com/repositories/1422000007/releases/latest?x=1", ""})
        assert(github_release_api_url_allowed(elsewhere) == 0);
    assert(github_release_api_url_allowed(nullptr) == 0);
}

void SumsLines()
{
    std::string digest;
    assert(Sums(kSumsText, &digest) == 1 && digest == kDigest);
    // Among other files' lines, sha256sum's binary mark, capitals, CRLF, no last line end.
    const std::string other = std::string(64, 'a') + "  PPSA99008.zip\n";
    assert(Sums(other + kSumsText + other, &digest) == 1 && digest == kDigest);
    assert(Sums(std::string(kDigest) + " *PPSA99003.zip", &digest) == 1 && digest == kDigest);
    std::string upper = std::string(kDigest) + "  PPSA99003.zip\r\n";
    for (std::size_t i = 0; i < 64; ++i)
        upper[i] = static_cast<char>(upper[i] >= 'a' ? upper[i] - 32 : upper[i]);
    assert(Sums(upper, &digest) == 1 && digest == kDigest);
    // No line for the ZIP: only other files', a file whose name merely ends or starts like it.
    assert(Sums("") == 0 && Sums(other) == 0);
    assert(Sums(std::string(kDigest) + "  PPSA99003.zip.sig\n") == 0);
    assert(Sums(std::string(kDigest) + "  OldPPSA99003.zip\n") == 0);
    // Its line malformed (short, not hexadecimal, one space, something before it) or there twice.
    assert(Sums(std::string(kDigest).substr(1) + "  PPSA99003.zip\n") == -1);
    assert(Sums(std::string(63, 'a') + "g  PPSA99003.zip\n") == -1);
    assert(Sums(std::string(kDigest) + " PPSA99003.zip\n") == -1);
    assert(Sums(" " + kSumsText) == -1);
    assert(Sums(kSumsText + kSumsText) == -1);
    // Another file's malformed line is not this one's business.
    assert(Sums("zz  PPSA99008.zip\n" + kSumsText) == 1);
}

void ReleaseNotes()
{
    int truncated = -1;
    github_release release{};
    assert(Evaluate(kRelease, "01.001.000", &release) == GITHUB_RELEASE_NEWER);
    assert(std::string(release.notes) == kNotes && release.notes_truncated == 0);
    assert(Notes("one\ntwo\n\n- a\n  b\n- c\n# Title\ntext") == "one two\n\n- a b\n- c\nTitle\ntext");
    assert(Notes("```\ncode **as is**\n```\nafter") == "code **as is**\nafter");
    assert(Notes("[a](https://x/y) and <https://example.net/p> \\*not bold\\*") ==
           "a and https://example.net/p *not bold*");
    assert(Notes("before\n\n---\n\n<!-- hidden\nstill hidden -->\nafter") == "before\n\nafter");
    assert(Notes("") == "" && Notes("\n\n  \n") == "");
    // At most 4,000 characters, ending with a whole line: what is left is on the release's page.
    std::string long_notes;
    for (int i = 0; i < 60; ++i)
        long_notes += "- " + std::string(97, 'x') + "\n";
    const std::string cut = Notes(long_notes, GITHUB_RELEASE_NOTES_CHARACTERS, &truncated);
    assert(truncated == 1 && cut.size() <= 4000 && cut.size() > 3800 && cut.back() == 'x');
}

void Releases()
{
    const std::string compact = Compact(kRelease);
    assert(compact.find("\"tag_name\":\"01.001.010\"") != std::string::npos &&
           compact.size() < kRelease.size());
    // A newer release with its ZIP and SHA256SUMS: the files' sizes and the content version.
    for (const std::string &json : {kRelease, compact, WithAll(kRelease, "\\r\\n", "\\n")})
    {
        github_release release{};
        assert(Evaluate(json, "01.001.000", &release) == GITHUB_RELEASE_NEWER);
        assert(std::strcmp(release.tag, "01.001.010") == 0 &&
               std::strcmp(release.available, "01.001.010") == 0);
        assert(release.zip_size == 61234567 && release.sums_size == 80);
        assert(std::string(release.notes) == kNotes);
    }
    // Newer by any of its three numbers; the same version or an older one is nothing to offer.
    assert(Evaluate(kRelease, "01.001.009") == GITHUB_RELEASE_NEWER);
    assert(Evaluate(kRelease, "01.000.090") == GITHUB_RELEASE_NEWER);
    assert(Evaluate(kRelease, "01.001.010") == GITHUB_RELEASE_NOT_NEWER);
    assert(Evaluate(kRelease, "01.002.000") == GITHUB_RELEASE_NOT_NEWER);
    assert(Evaluate(kRelease, "development") == GITHUB_RELEASE_BAD_ARGUMENT);
    // A tag that isn't a content version: the build-cache release, Nightshade's own form, others.
    for (const char *tag : {"build-cache", "v1.001.010", "v01.001.010", "01.001.010-test"})
        assert(Evaluate(WithAll(kRelease, "\"tag_name\": \"01.001.010\"",
                                std::string("\"tag_name\": \"") + tag + "\""),
                        "01.001.000") == GITHUB_RELEASE_NOT_A_VERSION);
    assert(Evaluate(With(kRelease, "\"tag_name\": \"01.001.010\"",
                         "\"tag_name\": \"" + std::string(60, 'v') + "\""),
                    "01.001.000") == GITHUB_RELEASE_NOT_A_VERSION);
    assert(Evaluate(With(kRelease, "\"tag_name\": \"01.001.010\"", "\"tag_name\": 7"),
                    "01.001.000") == GITHUB_RELEASE_BAD_RESPONSE);
    // A pre-release or a draft is never offered (releases/latest gives neither).
    assert(Evaluate(With(kRelease, "\"prerelease\": false", "\"prerelease\": true"),
                    "01.001.000") == GITHUB_RELEASE_NOT_PUBLISHED);
    assert(Evaluate(With(kRelease, "\"draft\": false", "\"draft\": true"), "01.001.000") ==
           GITHUB_RELEASE_NOT_PUBLISHED);
    assert(Evaluate(With(kRelease, "\"draft\": false", "\"draft\": \"no\""), "01.001.000") ==
           GITHUB_RELEASE_BAD_RESPONSE);
    // Without both files, uploaded in full, there is no offer: the ZIP is PPSA99003.zip, nothing
    // else (another title's, a renamed one, Nightshade's form).
    for (const char *other : {"PPSA99008.zip", "PPSA99003.zip.sig", "PPSA99003-01.001.010.zip",
                              "Nightshade-TV-v1.001.010.zip", "Physalis-v1.001.010.zip",
                              "ppsa99003.zip", "PPSA99003.ZIP"})
        assert(Evaluate(With(kRelease, "\"name\": \"PPSA99003.zip\"",
                             std::string("\"name\": \"") + other + "\""),
                        "01.001.000") == GITHUB_RELEASE_NO_ZIP);
    assert(Evaluate(With(kRelease, "\"state\": \"uploaded\",\n      \"size\": 61234567",
                         "\"state\": \"starter\",\n      \"size\": 61234567"),
                    "01.001.000") == GITHUB_RELEASE_NO_ZIP);
    assert(Evaluate(With(kRelease, "\"size\": 61234567", "\"size\": 3000000000"), "01.001.000") ==
           GITHUB_RELEASE_NO_ZIP);
    assert(Evaluate(With(kRelease, "\"size\": 61234567", "\"size\": 0"), "01.001.000") ==
           GITHUB_RELEASE_NO_ZIP);
    assert(Evaluate(With(kRelease, "\"name\": \"SHA256SUMS\"", "\"name\": \"SHA256SUMS.txt\""),
                    "01.001.000") == GITHUB_RELEASE_NO_SUMS);
    assert(Evaluate(With(kRelease, "\"size\": 80", "\"size\": 5000"), "01.001.000") ==
           GITHUB_RELEASE_NO_SUMS);
    const std::size_t assets = compact.find("\"assets\":[") + 10;
    const std::string no_files =
        compact.substr(0, assets) + compact.substr(compact.find("],\"tarball_url\""));
    assert(Evaluate(no_files, "01.001.000") == GITHUB_RELEASE_NO_ZIP);
    // Not GitHub's answer: cut short, a page of a network's own, a field twice.
    assert(Evaluate(kRelease.substr(0, kRelease.size() / 2), "01.001.000") ==
           GITHUB_RELEASE_BAD_RESPONSE);
    assert(Evaluate(compact.substr(0, compact.size() - 1), "01.001.000") ==
           GITHUB_RELEASE_BAD_RESPONSE);
    assert(Evaluate("<html>blocked</html>", "01.001.000") == GITHUB_RELEASE_BAD_RESPONSE);
    assert(Evaluate("{\"message\":\"Not Found\"}", "01.001.000") == GITHUB_RELEASE_BAD_RESPONSE);
    assert(Evaluate(With(kRelease, "\"tag_name\": \"01.001.010\",",
                         "\"tag_name\": \"01.001.010\", \"tag_name\": \"09.000.000\","),
                    "01.001.000") == GITHUB_RELEASE_BAD_RESPONSE);
    assert(Evaluate(With(kRelease, "\"name\": \"PPSA99003.zip\",",
                         "\"name\": \"PPSA99003.zip\", \"name\": \"PPSA99003.bin\","),
                    "01.001.000") == GITHUB_RELEASE_BAD_RESPONSE);
    // The ZIP listed twice is not GitHub's answer either (a release's files have distinct names).
    const std::string zip_asset = "{\"name\":\"PPSA99003.zip\",\"state\":\"uploaded\",\"size\":10},";
    assert(Evaluate(With(compact, "\"assets\":[", "\"assets\":[" + zip_asset), "01.001.000") ==
           GITHUB_RELEASE_BAD_RESPONSE);
    // No notes (null) is still a release.
    github_release release{};
    const std::size_t body = kRelease.find("\"body\": \"");
    assert(Evaluate(With(kRelease, kRelease.substr(body, kRelease.find("\"reactions\"") - body),
                         "\"body\": null,\n  "),
                    "01.001.000", &release) == GITHUB_RELEASE_NEWER &&
           release.notes[0] == '\0');
}

void GitHubCheck()
{
    static self_update_offer offer;
    github_release_report report{};
    // Offered: the ZIP's address made from the repository and the tag, the SHA-256 from
    // SHA256SUMS, the size from the release, its notes; two requests, the release and SHA256SUMS.
    Serve(200, Compact(kRelease));
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_AVAILABLE);
    assert(report.reason == GITHUB_RELEASE_NEWER && report.http_status == 200 &&
           std::strcmp(report.tag, "01.001.010") == 0 && report.redirects == 0);
    assert((network.asked == std::vector<std::string>{kApi, kSums}));
    assert(std::strcmp(offer.title, "PPSA99003") == 0 && std::strcmp(offer.name, kName) == 0);
    assert(std::strcmp(offer.installed, "01.001.000") == 0 &&
           std::strcmp(offer.available, "01.001.010") == 0);
    assert(std::strcmp(offer.version, "01.001.010") == 0 && std::strcmp(offer.artifact, kZip) == 0);
    assert(std::strcmp(offer.sha256, kDigest) == 0 && offer.size == 61234567);
    assert(std::strcmp(offer.page,
                       "https://github.com/Oliv3rmoon/Physalis/releases/tag/01.001.010") == 0);
    assert(std::string(offer.notes) == kNotes && offer.notes_truncated == 0);
    assert(self_update_url_allowed(offer.artifact, 0) == 1);
    // From ProsperoTV's last release, installed by hand and then updated, the same.
    assert(Check(kRepository, "01.000.090", &offer, &report) == SELF_UPDATE_AVAILABLE);

    // Nothing to say, never an error: no release (or only pre-releases), the rate limit, another
    // status, no network.
    for (const auto &[status, reason] : std::vector<std::pair<int, github_release_reason>>{
             {404, GITHUB_RELEASE_NONE},
             {403, GITHUB_RELEASE_RATE_LIMITED},
             {429, GITHUB_RELEASE_RATE_LIMITED},
             {500, GITHUB_RELEASE_HTTP_STATUS},
             {304, GITHUB_RELEASE_HTTP_STATUS}})
    {
        Serve(status, "{\"message\":\"Not Found\"}");
        assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN);
        assert(report.reason == reason && report.http_status == status &&
               offer.artifact[0] == '\0');
        assert((network.asked == std::vector<std::string>{kApi}));
    }
    Serve(200, kRelease);
    network.failure = -10006;
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN);
    assert(report.reason == GITHUB_RELEASE_NETWORK && report.platform_error == -10006);

    // The same version, an older one, or a tag that isn't one: SHA256SUMS is not asked for.
    Serve(200, kRelease);
    assert(Check(kRepository, "01.001.010", &offer, &report) == SELF_UPDATE_UP_TO_DATE &&
           report.reason == GITHUB_RELEASE_NOT_NEWER && offer.available[0] == '\0');
    assert((network.asked == std::vector<std::string>{kApi}));
    assert(Check(kRepository, "01.002.000", &offer, &report) == SELF_UPDATE_UP_TO_DATE);
    Serve(200, WithAll(kRelease, "01.001.010", "build-cache"));
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN &&
           report.reason == GITHUB_RELEASE_NOT_A_VERSION &&
           std::strcmp(report.tag, "build-cache") == 0);
    assert((network.asked == std::vector<std::string>{kApi}));

    // SHA256SUMS missing from the file host, without the ZIP's line, or with a malformed one.
    Serve(200, kRelease);
    network.files.clear();
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN &&
           report.reason == GITHUB_RELEASE_SUMS_UNREADABLE && offer.artifact[0] == '\0');
    network.files[kSums] = std::string(64, 'a') + "  PPSA99008.zip\n";
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN &&
           report.reason == GITHUB_RELEASE_SUMS_NO_LINE);
    network.files[kSums] = "not-a-digest  PPSA99003.zip\n";
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN &&
           report.reason == GITHUB_RELEASE_SUMS_MALFORMED && offer.sha256[0] == '\0');
    network.files[kSums] = std::string(5000, 'a');
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN &&
           report.reason == GITHUB_RELEASE_SUMS_UNREADABLE);

    // A renamed repository: GitHub's 301 (or 307) names the new address on api.github.com, which
    // is asked instead. A redirect anywhere else, without an address, or one after another is not
    // followed.
    const std::string old_sums =
        "https://github.com/Oliv3rmoon/ProsperoTV-Nightshade/releases/download/01.001.010/SHA256SUMS";
    const auto redirect = [](int status, const std::string &to)
    { return std::make_pair(status, "{\"message\":\"Moved Permanently\",\"url\":\"" + to + "\"}"); };
    const auto moved = [&](int status)
    {
        Serve(200, Compact(kRelease));
        network.answers.clear();
        network.answers[kOldApi] = redirect(status, kMoved);
        network.answers[kMoved] = {200, Compact(kRelease)};
        network.files[old_sums] = kSumsText;
    };
    for (const int status : {301, 307, 302, 308})
    {
        moved(status);
        assert(Check(kOldRepository, "01.001.000", &offer, &report) == SELF_UPDATE_AVAILABLE);
        assert(report.redirects == 1 && report.http_status == 200 &&
               std::strcmp(offer.sha256, kDigest) == 0);
        assert((network.asked == std::vector<std::string>{kOldApi, kMoved, old_sums}));
    }
    for (const std::string &to :
         {std::string("https://example.net/repositories/1422000007/releases/latest"),
          std::string("https://api.github.com/repos/ThatGuyKalvin/ProsperoTV/releases/latest"),
          std::string("https://github.com/Oliv3rmoon/Physalis/releases/latest")})
    {
        moved(301);
        network.answers[kOldApi] = redirect(301, to);
        network.answers[to] = {200, kRelease};
        assert(Check(kOldRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN &&
               report.reason == GITHUB_RELEASE_REDIRECT_REFUSED && report.http_status == 301);
        assert((network.asked == std::vector<std::string>{kOldApi}));
    }
    Serve(200, kRelease);
    network.answers[kApi] = redirect(301, kMoved);
    network.answers[kMoved] = redirect(301, kMoved); // to itself, again and again
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN &&
           report.reason == GITHUB_RELEASE_REDIRECT_REFUSED &&
           report.redirects == GITHUB_RELEASE_REDIRECTS &&
           network.asked.size() == 1 + GITHUB_RELEASE_REDIRECTS);

    // A repository, name, title ID or installed version that can't be used asks nothing.
    Serve(200, kRelease);
    assert(Check("Oliv3rmoon", "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN &&
           report.reason == GITHUB_RELEASE_BAD_ARGUMENT && network.asked.empty());
    assert(Check(nullptr, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN &&
           network.asked.empty());
    assert(Check("", "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN && network.asked.empty());
    assert(Check(kRepository, "development", &offer, &report) == SELF_UPDATE_UNKNOWN &&
           network.asked.empty());
    assert(Check(kRepository, "01.001.000", &offer, &report, "../PPSA99003") ==
               SELF_UPDATE_UNKNOWN &&
           network.asked.empty());
    for (const char *name : {"", " Physalis", "Physalis ", "Phys  alis", "Phys\nalis",
                             "Physalis/TV", "Physalis and a name that is far too long for it"})
        assert(Check(kRepository, "01.001.000", &offer, &report, kTitle, name) ==
                   SELF_UPDATE_UNKNOWN &&
               report.reason == GITHUB_RELEASE_BAD_ARGUMENT && network.asked.empty());
    // A test title asks for its own ZIP, which the release doesn't carry.
    assert(Check(kRepository, "01.001.000", &offer, &report, "PPSA88021") == SELF_UPDATE_UNKNOWN &&
           report.reason == GITHUB_RELEASE_NO_ZIP);
}

// The release's notes in the answer, in place of the fixture's.
std::string WithBody(const std::string &body)
{
    const std::size_t from = kRelease.find("\"body\": \""), to = kRelease.find("\"reactions\"");
    return kRelease.substr(0, from) + "\"body\": \"" + body + "\",\n  " + kRelease.substr(to);
}

void AnswerSize()
{
    static self_update_offer offer;
    github_release_report report{};
    // As long as GitHub lets notes be (125,000 characters), in Chinese: some 380 KB of answer,
    // offered, with its notes cut to what the dialog takes.
    std::string notes;
    for (int i = 0; i < 125000; ++i)
        notes += "\xe6\xbc\xa2"; // U+6F22
    Serve(200, WithBody(notes));
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_AVAILABLE);
    assert(offer.notes_truncated == 1 &&
           std::string(offer.notes) == notes.substr(0, 3 * GITHUB_RELEASE_NOTES_CHARACTERS));
    // Up to 1 MiB in all; a longer answer is too large, said as such (not as a network failure).
    Serve(200, WithBody(std::string(GITHUB_RELEASE_MAX_ANSWER - 8000, 'x')));
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_AVAILABLE);
    Serve(200, WithBody(std::string(GITHUB_RELEASE_MAX_ANSWER, 'x')));
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_UNKNOWN);
    assert(report.reason == GITHUB_RELEASE_TOO_LARGE && report.http_status == 200 &&
           report.platform_error == 0 &&
           std::strcmp(github_release_reason_text(report.reason), "too-large") == 0);
    assert((network.asked == std::vector<std::string>{kApi}) && offer.artifact[0] == '\0');
}

// The release with these files in place of its own.
std::string WithAssets(const std::string &assets)
{
    const std::string compact = Compact(kRelease);
    const std::size_t from = compact.find("\"assets\":[") + 10,
                      to = compact.find("],\"tarball_url\"");
    return compact.substr(0, from) + assets + compact.substr(to);
}

std::string Asset(const std::string &name, unsigned size = 10, const char *state = "uploaded")
{
    return "{\"name\":\"" + name + "\",\"state\":\"" + state +
           "\",\"size\":" + std::to_string(size) + "}";
}

void AssetList()
{
    // The list is read once, element after element.
    const std::string array = "[ 1, \"a\" ,{\"b\":[2,{}]} , null ]";
    std::size_t at = 0;
    const char *value = nullptr;
    std::size_t length = 0;
    std::vector<std::string> seen;
    while (update_check_json_array_next(array.data(), array.size(), &at, &value, &length) == 1)
        seen.emplace_back(value, length);
    assert((seen == std::vector<std::string>{"1", "\"a\"", "{\"b\":[2,{}]}", "null"}));
    assert(update_check_json_array_next(array.data(), array.size(), &at, &value, &length) == 0);
    at = 0;
    assert(update_check_json_array_next("[ ]", 3, &at, &value, &length) == 0 &&
           update_check_json_array_next("[ ]", 3, &at, &value, &length) == 0);
    for (const std::string bad : {"[1,]", "[1 2]", "[1] x", "{\"a\":1}", "[", "", "[\"open]"})
    {
        at = 0;
        int found = 1;
        while (found == 1)
            found = update_check_json_array_next(bad.data(), bad.size(), &at, &value, &length);
        assert(found == -1);
    }
    unsigned number = 0;
    assert(update_check_json_uint("{\"size\":61234567}", 17, "size", &number) == 1 &&
           number == 61234567u);
    for (const char *bad : {"{\"size\":-1}", "{\"size\":01}", "{\"size\":1.5}", "{\"size\":\"1\"}",
                            "{\"size\":99999999999}", "{\"size\":1,\"size\":2}"})
        assert(update_check_json_uint(bad, std::strlen(bad), "size", &number) == -1);
    assert(update_check_json_uint("{\"other\":1}", 11, "size", &number) == 0);
    // A NUL inside a string is refused: the values are read as C strings.
    char text[16];
    assert(update_check_json_string("{\"a\":\"x\\u0000y\"}", 16, "a", text, sizeof(text)) == -1);

    static self_update_offer offer;
    github_release_report report{};
    const std::string zip = Asset("PPSA99003.zip", 61234567), sums = Asset("SHA256SUMS", 80);
    // All of GitHub's 1,000 files are looked at: the release's ZIP may come last.
    std::string many;
    for (int i = 0; i < 998; ++i)
        many += Asset("file-" + std::to_string(i) + ".bin") + ",";
    Serve(200, WithAssets(many + sums + "," + zip));
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_AVAILABLE &&
           std::strcmp(offer.artifact, kZip) == 0 && offer.size == 61234567);
    // ZIPs of other titles beside it change nothing.
    Serve(200, WithAssets(Asset("PPSA99008.zip") + "," + zip + "," + sums));
    assert(Check(kRepository, "01.001.000", &offer, &report) == SELF_UPDATE_AVAILABLE &&
           std::strcmp(offer.artifact, kZip) == 0);
}

// A release's tag goes into the log: whatever it holds, it stays on its line.
void LoggedTag()
{
    static self_update_offer offer;
    github_release_report report{};
    const auto tagged = [&](const std::string &json_tag)
    {
        Serve(200, With(kRelease, "\"tag_name\": \"01.001.010\"",
                        "\"tag_name\": \"" + json_tag + "\""));
        return Check(kRepository, "01.001.000", &offer, &report);
    };
    const auto one_line = [](const char *text)
    {
        for (const char *at = text; *at != '\0'; ++at)
            if (*at <= ' ' || *at >= 0x7f)
                return false;
        return true;
    };
    assert(tagged("01\\n[TV] update check: forged") == SELF_UPDATE_UNKNOWN &&
           report.reason == GITHUB_RELEASE_NOT_A_VERSION);
    assert(std::string(report.tag) == "01\\x0a[TV]\\x20update\\x20check:\\x20forged" &&
           one_line(report.tag));
    std::string ends;
    for (int i = 0; i < 39; ++i)
        ends += "\\n";
    assert(tagged(ends) == SELF_UPDATE_UNKNOWN && std::strlen(report.tag) == 39 * 4 &&
           one_line(report.tag));
    assert(tagged("01.001.010") == SELF_UPDATE_AVAILABLE && std::strcmp(report.tag, "01.001.010") == 0);
}

// The catalog lists ProsperoTV's own releases: the check never asks it, whatever happens.
void NoCatalog()
{
    static self_update_offer offer;
    github_release_report report{};
    std::vector<std::string> all;
    const auto keep = [&]() { all.insert(all.end(), network.asked.begin(), network.asked.end()); };
    Serve(200, kRelease);
    (void)Check(kRepository, "01.001.000", &offer, &report);
    keep();
    for (const int status : {404, 403, 500})
    {
        Serve(status, "{}");
        (void)Check(kRepository, "01.001.000", &offer, &report);
        keep();
    }
    Serve(200, kRelease);
    network.failure = -10006;
    (void)Check(kRepository, "01.001.000", &offer, &report);
    keep();
    assert(!all.empty());
    for (const std::string &url : all)
        assert(url.rfind("https://api.github.com/", 0) == 0 ||
               url.rfind("https://github.com/Oliv3rmoon/Physalis/", 0) == 0);
}

} // namespace

int main()
{
    TagVersions();
    Addresses();
    SumsLines();
    ReleaseNotes();
    Releases();
    GitHubCheck();
    AnswerSize();
    AssetList();
    LoggedTag();
    NoCatalog();
    std::puts("update check from GitHub: tag versions, addresses and redirects, SHA256SUMS lines, "
              "release notes, releases/latest answers, the check and its silent answers, the "
              "catalog never asked PASS");
    return 0;
}
