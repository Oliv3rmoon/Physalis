# Update check and self-update

These files are the PS5 Native App Boilerplate's (`examples/update-check` and
`examples/self-update`, boilerplate commit 2966cac), copied with the catalog's address changed to prosperostore.net, as
ProsperoEden uses them:

| File | What it is |
| --- | --- |
| `update_check.{h,c}` | reading `param.json`, comparing content versions, the catalog's answer |
| `console_curl.{h,c}` | what PacBrew's libcurl needs to run in a native title: name lookups on the system resolver, libc stand-ins, the `fcntl` wrapper, the console's certificate list |
| `self_update.{h,c}` | the signed-catalog check (Ed25519, sequence guard) and the update job |
| `self_update_ps5.c` | the console side: libcurl, OpenSSL, the payload loader on port 9021 |
| `github_release.{h,c}` | Physalis: the check against a GitHub repository's releases, which the app uses instead of the catalog (below) |
| `update_schedule.{h,c}` | Physalis: when the check asks again (every four hours of uptime, begun only from the menu) and that a version is offered once a launch |
| `self_update_sha256.{h,c}`, `self_update_protocol.h` | shared with the helper |

`self_update_ps5.c` takes the paths of the helper, of the app's `param.json`
and of the sequence file from `SELF_UPDATE_HELPER_PATH`,
`SELF_UPDATE_PARAM_PATH` and `SELF_UPDATE_SEQUENCE_PATH` when they are defined.
ProsperoTV defines them (`../tv_update_paths.h`, put in front of that file by
`ps5/patch_tree.py`) because its folder is not `/app0` once it runs with
filesystem access. `../tv_update.cpp` is ProsperoTV's use of the kit, and
`src/tv/update_sheet.*` what the viewer sees of it.

The helper the app sends to the console's payload loader is in
`ps5/update_helper` (the boilerplate's `examples/self-update-helper` with
ProsperoEden's two changes: only the entries a release replaces are moved
aside, so anything else in the app's folder stays; and the installed folder
is taken from ShadowMountPlus's `/user/app/<TITLEID>/mount.lnk` before the
usual folders are scanned, with an image install refused). It unpacks with
miniz 3.0.2 (`ps5/third_party/miniz`, MIT).

## Physalis: updates from GitHub releases

`github_release.{h,c}` are Nightshade's (Oliv3rmoon/Nightshade,
`headless/update_check/github_release.*`), for this repository's releases:
the app asks GitHub for the newest release of the repository the build names
(`TV_UPDATE_REPOSITORY` in `../tv_build_options.h`, written by
`ps5/patch_tree.py`; `Oliv3rmoon/Physalis`) instead of the catalog, and
fills the same offer, so the download, the helper and the dialog are the
kit's, unchanged. The release must not be a draft or a pre-release, its tag
must be a content version higher than the app's (the workflow tags a release
with its `contentVersion`), and it must carry `PPSA99003.zip` and a
`SHA256SUMS` that lists that ZIP on one well-formed line. Its trust is TLS
and the release's own `SHA256SUMS`, not the catalog's signature (see the
header). `../tv_update.cpp` never asks the catalog, which lists ProsperoTV's
own releases: an update from there would undo the branding.

For it, `update_check.c` has three more readers, as Nightshade's kit has them
(`update_check_json_value`, `update_check_json_uint` and
`update_check_json_array_next`), and refuses a NUL inside a string it copies;
`self_update_ps5.c` has `self_update_check_self_from` and
`self_update_check_as_from`, and its `console_fetch` tells an answer too long
for its buffer (`SELF_UPDATE_FETCH_TOO_LARGE`) from a failed transfer. The
host check is `opengl-ui/tests/github_release_check.cc`, built and run by
`tests/test_github_release.py` (`make test-integration`).

How an update goes:

1. At launch, and again every four hours of uptime, on a thread of its own,
   the app asks GitHub for the repository's newest release, reads its files
   and its `SHA256SUMS`, and compares the release's content version with
   `sce_sys/param.json`. A check is begun only from the menu, never during
   playback, and a failed one waits as long as an answered one
   (`update_schedule.h`; its host check is
   `opengl-ui/tests/update_schedule_check.cc`, run by
   `tests/test_github_release.py`).
2. A newer release is offered, in the menu, once a launch: a version the
   viewer put off with **Later** is not offered again until the app starts
   again. Nothing happens unless the viewer says so.
3. The app downloads the release ZIP from GitHub over HTTPS and streams it to
   the helper, which checks its SHA-256 against the release's `SHA256SUMS` and
   unpacks it beside the app.
4. The app gives the go-ahead and closes itself. The helper replaces the
   app's files, refreshes the home screen's copies and posts a notification.

A cancel or a failure before step 4 leaves the installed app untouched.

Test titles also read `dev/update-offer.txt` beside the app (five lines: the
newer content version, the release's name, its ZIP on GitHub, the ZIP's
SHA-256, its size), which replaces GitHub's answer so an update can be
tried before the repository has one, and `dev/update-as.txt`
(`PPSA99003 01.000.000`) to ask the repository's releases as another title
and version.

As in ProsperoEden, `self_update_check` also copies the catalog entry's
`release_notes` and `release_notes_truncated` into the offer (`notes`,
`notes_truncated`), for the update dialog's What's new view; the lines marked
"ProsperoEden:" in `self_update.c` and `self_update.h` are that change. In
`dev/update-offer.txt`, any lines after the fifth are the release notes.
