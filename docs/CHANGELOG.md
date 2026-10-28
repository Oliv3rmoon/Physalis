# Changelog

What changed in each release of Nightshade TV, newest first. A release's notes on GitHub are in
`docs/releases/<version>.md`. What changed in ProsperoTV before Nightshade TV is in the earlier
files there.

## Nightshade TV v1.001.000: the first Nightshade TV, based on ProsperoTV 01.000.090

Nightshade TV is Oliv3rmoon's release of ProsperoTV by BlackBearReloaded: ProsperoTV 01.000.090
with a new name, Nightshade's look and updates from Oliv3rmoon/Nightshade-TV. What changed:

- **The name.** The PS5 home screen, the header, the opening, the About page, the update dialog,
  the notifications and every message say Nightshade TV, in all nine languages, and so does the
  phone remote's page. The About page says "Nightshade TV is Oliv3rmoon's release of ProsperoTV by
  BlackBearReloaded." beside the GPL notice.
- **The look.** Nightshade's palette (its "nightly" theme): near-black pages, black glass panels, a
  red accent (#e5484d) for the main actions, the focus and the highlights, cool white text, an
  amber for warnings, and a near-black sky with a low red glow. The theme is called Nightshade in
  Settings; Ocean and Forest are still there. The phone remote's page has the same colours.
- **The art.** The mark is the nightshade flower. The icon (a black set lit red, the flower on its
  screen), the home background (an aerial with the nightshade climbing it) and the launch picture
  (a screen-shaped ring of red light round the flower) are drawn by `tools/render-home-art.py`, in
  Nightshade's style.
- **Updates from this repository.** At the start the app asks
  [Oliv3rmoon/Nightshade-TV](https://github.com/Oliv3rmoon/Nightshade-TV/releases) for its newest
  release that is not a pre-release, and offers it when it is newer and carries `PPSA99003.zip`
  and `SHA256SUMS`; **Update now** checks the ZIP against `SHA256SUMS`. It never asks the
  ProsperoStore catalog, so ProsperoTV's own releases, which would put the old look back, are
  never offered. The update texts say GitHub.
- **Release builds.** A version tag pushed to Oliv3rmoon/Nightshade-TV builds `PPSA99003.zip` and
  publishes it with `SHA256SUMS` as the latest release. Without a build-cache snapshot the build
  starts from nothing instead of failing.
- **What stays.** The title ID (PPSA99003) and the data folder (`/data/prosperotv`), so it installs
  over ProsperoTV and keeps your sources, favorites, recent channels and settings. Backups keep
  their file names, so a ProsperoTV backup restores. Everything else is ProsperoTV 01.000.090.

Built and checked by the host tests; not yet run on a console.

### Install

Close ProsperoTV, copy this release's `PPSA99003` folder (from `PPSA99003.zip`) over your current
one (usually `/data/homebrew/PPSA99003`), and start it. ProsperoTV doesn't offer this release
itself, so this one is copied by hand; from it on, **Update now** applies the next one. To go back,
copy an official [ProsperoTV release](https://github.com/ThatGuyKalvin/ProsperoTV/releases) over it
the same way.
