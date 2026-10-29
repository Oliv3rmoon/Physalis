/*
 * Physalis - Updates from a GitHub repository's releases instead of the ProsperoStore catalog.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Nightshade's way (Oliv3rmoon/Nightshade, headless/update_check/github_release.h), for the
 * releases this repository's workflow makes (.github/workflows/tooling.yml). The build names a
 * repository (TV_UPDATE_REPOSITORY, ps5/patch_tree.py), and the app asks GitHub for that
 * repository's newest release instead of asking the catalog, and fills the same offer the
 * catalog's check does (self_update.h), so the download, the staging by the helper and the update
 * dialog are the kit's, unchanged:
 *
 *   1. https://api.github.com/repos/<owner/name>/releases/latest: the newest release that is
 *      neither a draft nor a pre-release. Its tag must be a content version, as the workflow
 *      names a release ("01.001.000", the contentVersion of its param.json), higher than the
 *      running one. A repository that was renamed answers with a redirect to its new address,
 *      https://api.github.com/repositories/<its number>/releases/latest, which is followed (to
 *      that address alone);
 *   2. its files must include SHA256SUMS and "<title ID>.zip" ("PPSA99003.zip"), both uploaded;
 *   3. SHA256SUMS (small, downloaded like the ZIP) must list that ZIP on one well-formed line:
 *      that SHA-256 is the one the download and the helper then check it against, as they check
 *      the catalog's.
 * Anything else is no offer: no release, GitHub's rate limit (60 requests an hour from one
 * address), an answer that can't be read, a release without both files. The catalog is never
 * asked, so ProsperoTV's own releases are never offered.
 *
 * Trust. The catalog's offer is vouched for by the catalog's Ed25519 signature over its
 * manifest. This one is not signed: it trusts TLS to api.github.com and github.com (and the
 * file host github.com sends a download to, self_update_url_allowed) and the release's own
 * SHA256SUMS, the same trust as a viewer who downloads the release by hand from its page. The
 * helper still checks what it unpacks before anything is replaced: the title ID and the
 * content version in the ZIP's param.json must be this app's and the offered one.
 *
 * C11; also compiles as C++. The decisions build without a network (UPDATE_CHECK_NO_NETWORK) and
 * are tested on the host (opengl-ui/tests/github_release_check.cc, tests/test_github_release.py).
 */
#ifndef PHYSALIS_GITHUB_RELEASE_H
#define PHYSALIS_GITHUB_RELEASE_H

#include "self_update.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define GITHUB_RELEASE_HOST "https://api.github.com/"
#define GITHUB_RELEASE_API GITHUB_RELEASE_HOST "repos/"
/* Redirects of the release's request followed, to a renamed repository's address only. */
#define GITHUB_RELEASE_REDIRECTS 3
/* The release's answer with its notes: GitHub allows 125,000 characters of them, which in
 * Chinese or Japanese is some 380 KB, and a release's files take about 1.4 KB each. */
#define GITHUB_RELEASE_MAX_ANSWER (1024u * 1024u)
/* SHA256SUMS: a line or a few. */
#define GITHUB_RELEASE_MAX_SUMS 4096u
/* As the catalog bounds release_notes (self_update_offer.notes). */
#define GITHUB_RELEASE_NOTES_CHARACTERS 4000u

    typedef enum github_release_reason
    {
        GITHUB_RELEASE_NEWER = 0,             /* offered */
        GITHUB_RELEASE_NOT_NEWER = 1,         /* the newest release is this version or older */
        GITHUB_RELEASE_BAD_ARGUMENT = 2,      /* repository, name, title or version unusable */
        GITHUB_RELEASE_NETWORK = 3,           /* the request failed; see platform_error */
        GITHUB_RELEASE_NONE = 4,              /* HTTP 404: no release (pre-releases don't count) */
        GITHUB_RELEASE_RATE_LIMITED = 5,      /* HTTP 403 or 429 */
        GITHUB_RELEASE_HTTP_STATUS = 6,       /* another HTTP status */
        GITHUB_RELEASE_BAD_RESPONSE = 7,      /* not the JSON GitHub documents */
        GITHUB_RELEASE_NOT_PUBLISHED = 8,     /* a draft or a pre-release */
        GITHUB_RELEASE_NOT_A_VERSION = 9,     /* its tag is not NN.NNN.NNN */
        GITHUB_RELEASE_NO_ZIP = 10,           /* no uploaded <title ID>.zip of a size the kit takes */
        GITHUB_RELEASE_NO_SUMS = 11,          /* no uploaded SHA256SUMS */
        GITHUB_RELEASE_SUMS_UNREADABLE = 12,  /* SHA256SUMS could not be downloaded */
        GITHUB_RELEASE_SUMS_NO_LINE = 13,     /* SHA256SUMS doesn't list the ZIP */
        GITHUB_RELEASE_SUMS_MALFORMED = 14,   /* its line for the ZIP is malformed or repeated */
        GITHUB_RELEASE_REDIRECT_REFUSED = 15, /* a redirect elsewhere than api.github.com, or too many */
        GITHUB_RELEASE_TOO_LARGE = 16         /* an answer over GITHUB_RELEASE_MAX_ANSWER */
    } github_release_reason;

    /* What the check found, for the log. */
    typedef struct github_release_report
    {
        github_release_reason reason;
        int http_status;    /* of the release query's last answer; 0 when none arrived */
        int platform_error; /* the failed request's code, or 0 */
        int redirects;      /* followed (a renamed repository) */
        /* As the log may show it, on one line: printable ASCII as it is, anything else (a line
         * end, a space, a backslash, every byte of a non-ASCII character) as \xNN. */
        char tag[160]; /* the newest release's tag, once read (at most 39 bytes, escaped) */
    } github_release_report;

    /* A release as releases/latest describes it, reduced to what the offer needs. */
    typedef struct github_release
    {
        char tag[40];       /* "01.001.000" */
        char available[12]; /* its content version, the same text */
        uint64_t zip_size;  /* of "<title ID>.zip" */
        uint64_t sums_size;
        char notes[16384]; /* plain text, as self_update_offer.notes */
        int notes_truncated;
    } github_release;

    /* 1 when `tag` is a content version (NN.NNN.NNN), as the workflow tags a release; it is then
     * copied to `version` (when not NULL). */
    int github_release_tag_version(const char *tag, char version[12]);

    /* 1 when `repository` is owner/name as GitHub allows them. */
    int github_release_repository_valid(const char *repository);

    /* https://api.github.com/repos/<repository>/releases/latest. Its length, 0 when the
     * repository isn't valid or `out` too small. */
    size_t github_release_url(char *out, size_t size, const char *repository);

    /* https://github.com/<repository>/releases/download/<tag>/<file>. Its length, or 0. */
    size_t github_release_file_url(char *out, size_t size, const char *repository, const char *tag,
                                   const char *file);

    /* 1 when a redirect of the release's request may be followed to `url`: the address GitHub
     * gives a renamed repository's newest release, exactly
     * https://api.github.com/repositories/<its number>/releases/latest. */
    int github_release_api_url_allowed(const char *url);

    /* Reads releases/latest's answer: the tag, the files `zip` and SHA256SUMS, the notes.
     * GITHUB_RELEASE_NEWER when it is to be offered once SHA256SUMS has given the ZIP's SHA-256,
     * another reason otherwise. */
    github_release_reason github_release_evaluate(const char *json, size_t length,
                                                  const char *installed, const char *zip,
                                                  github_release *release);

    /* The SHA-256 SHA256SUMS gives `file` ("<64 hex>  <file>" or "<64 hex> *<file>", as
     * sha256sum writes them). 1: copied to sha256 (lowercase). 0: no line for the file.
     * -1: its line is malformed, or there are two. Lines for other files are not looked at. */
    int github_release_sums(const char *text, size_t length, const char *file, char sha256[65]);

    /* Release notes as the update dialog lays them out (src/tv/update_sheet.cpp: paragraphs and
     * list items "- " on lines of their own, GitHub's "> [!WARNING]" callouts as "Warning: ...")
     * from the Markdown of a release: headings, emphasis and code marks and link addresses go,
     * links keep their text, a paragraph's lines are joined. At most `characters` characters
     * (code points), ending with a whole line when one fits; *truncated is set when some were
     * left out. Returns the length written (out is always terminated). */
    size_t github_release_notes(const char *markdown, size_t length, char *out, size_t size,
                                size_t characters, int *truncated);

    /* A short English word for a reason, for logs. */
    const char *github_release_reason_text(github_release_reason reason);

    /* The whole check against `repository`'s releases through the kit's platform: fills `offer`
     * as self_update_check() does; `name` is the app's name as the helper's notification says it
     * ("Physalis was updated to 01.001.010"). Blocking. */
    self_update_check_result github_release_check(const self_update_platform *platform,
                                                  const char *repository, const char *name,
                                                  const char *title_id, const char *installed,
                                                  self_update_offer *offer,
                                                  github_release_report *report);

    /* The same for the running app, on the console (self_update_ps5.c): its title ID and
     * content version from its param.json, as self_update_check_self() reads them. */
    self_update_check_result self_update_check_self_from(const char *repository, const char *name,
                                                         self_update_offer *offer,
                                                         github_release_report *report);

    /* As self_update_check_self_from(), asking as another title and version (test builds). */
    self_update_check_result self_update_check_as_from(const char *repository, const char *name,
                                                       const char *title_id, const char *installed,
                                                       self_update_offer *offer,
                                                       github_release_report *report);

#ifdef __cplusplus
}
#endif

#endif
