# Changelog

What changed in each release of Physalis, newest first. Physalis was called Nightshade TV until
01.002.000, and the entries for those releases keep the name they had. A release's notes on GitHub
are in `docs/releases/<version>.md`. What changed in ProsperoTV before Nightshade TV is in the
earlier files there.

## Physalis v1.002.004: long lists, faster

Scroll faster, jump to a letter in On demand, and why a channel didn't open in plain words, as a
tester asked. What changed:

- **Faster scrolling.** Hold Up or Down and the focus goes faster the longer you hold it: two rows
  a step after about a second, then four, then eight. In Live TV, Favorites, On demand, Sources and
  the provider's categories. L2 and R2 still turn a page; held for 3, 5 and 7 seconds they turn
  two, four and eight pages at a time, and they now turn pages of the provider's categories too.
- **Letters in On demand.** In Movies, TV shows and their categories, Right goes onto the letters
  beside the list, # and A to Z as on Live TV: Up and Down jump to the next letter with titles,
  and Left, Cross or Circle go back to the list. Going onto the letters sorts the list Title A–Z.
  Ported from ProsperoTV, by JMU Technologies.
- **Network errors in plain words.** A channel that doesn't open says why in your language, such
  as "the channel's server could not be reached in time; it may be offline or blocked on this
  network (curl 28)", instead of "connection failed (native error 0xFFFFD8D4)". When a channel's
  server can't be reached, it is no longer asked three times over (about thirty seconds for some
  channels); Physalis says so after the first try. Some of the public list's channels are on
  servers that no longer take connections. A channel list that can't be downloaded says why the
  same way.

## Physalis v1.002.003: the community in About

Where to find other Physalis viewers, from the app. What changed:

- **The community in About.** The About tab now names the community's Discord server,
  **nightly**, with its address (`discord.gg/pSqgRU3YP9`) and a QR code your phone opens it with.
  The first time Physalis opens with this version, a card shows the same once, after the opening;
  any button closes it, and it doesn't come back. In all nine languages.
- **QR codes right to left.** In Arabic and Hebrew the QR codes (pairing a phone, signing in to
  Plex and now the community) were mirrored with the rest of the screen. They keep their place but
  are drawn as made, the way a phone expects to read them.

## Physalis v1.002.002: free public channels with one press

Channels on a fresh install without typing an address. What changed:

- **Free public channels.** With no source set up, Live TV's empty screen offers **Square: Free
  public channels**. It adds [iptv-org](https://github.com/iptv-org/iptv)'s community-maintained
  public list as an ordinary playlist source and chooses it, so its channels load now and at every
  start. Physalis still ships no list of its own: the list is added only when you press Square,
  pressing it again doesn't add a second copy, and it's removed on the Sources tab like any other
  source. Neither Physalis nor iptv-org hosts the streams, and some may not play.

## Physalis v1.002.001: renamed from Nightshade TV

Renamed to Physalis; same app, same title ID, your settings stay. Physalis, a Nightshade app, is
the Chinese lantern, a relative of the nightshade. What changed:

- **The name.** The PS5 home screen, the header, the opening, the About page, the update dialog,
  the notifications and every message say Physalis, in all nine languages, and so does the phone
  remote's page. In your Plex account's device list the console is Physalis (PS5).
- **Updates.** The app now asks [Oliv3rmoon/Physalis](https://github.com/Oliv3rmoon/Physalis/releases)
  for a newer release. The old address, Oliv3rmoon/Nightshade-TV, forwards there, so Nightshade TV
  01.002.000 and older still find this release and **Update now** installs it over themselves. The
  release's files are named as before: `PPSA99003.zip` and `SHA256SUMS`.
- **What stays.** The title ID (PPSA99003) and the data folder (`/data/prosperotv`), so it installs
  over Nightshade TV and keeps your sources, favorites, recent channels, Plex account and
  settings. Backups keep their file names. The Nightshade theme and look, the art and the credit to
  ProsperoTV and its authors are as they were.

## Nightshade TV v1.002.000: Plex

Your Plex libraries beside your playlists and accounts. What changed:

- **Plex as a source.** In **Sources**, **Add Plex** shows a four-character code and a QR code that
  opens [plex.tv/link](https://plex.tv/link); enter the code on a phone or computer signed in to
  Plex, and every server the account reaches, its own and those shared with it, becomes a source.
  Sources then shows the account's name, **Find servers** and **Sign out**.
- **Movies and TV shows from Plex.** A server's movie libraries are its Movies and its show
  libraries its TV shows in **On demand**, each library a category, with seasons, episodes,
  posters and details, in the screens the Xtream lists use, with search, Home, Continue and the
  parental rules. Live TV says a Plex server has no live channels and opens On demand.
- **Playing as it is, or through the transcoder.** A file the PS5 player decodes (MP4, MOV,
  Matroska, WebM or MPEG-TS; H.264 8-bit, HEVC or VP9 up to 4K; AAC, AC-3, E-AC-3, MP2, MP3 or DTS)
  plays as it is; anything else through the server's universal transcoder over HLS, which copies
  the video when it can. Playback starts where the title was left on any device (the transcoder
  starts there too), tells the server where it is every ten seconds and when it stops, in the
  background so the menu comes back at once, and marks the title watched when it plays to its end.
- **Connections.** The local address at home, the remote one away, Plex's relay last, https first,
  all asked at once and checked by the server's own `/identity`, once per launch. Plain http only
  on your own network, since over http any box could answer for the server; `*.plex.direct` names
  dialled at the address plex.tv gives, so https works behind a router that blocks them. Until a
  server has answered nothing is sent to it, not even a poster; Sources says **Can't be
  reached**, its saved lists stay, and signing in saves only the servers that answered.
- **The tokens** are kept as Xtream passwords are, and travel in `X-Plex-Token` headers only, to
  their own server, never in an address. The diagnostic log never contains them or the account's
  name. Settings backups leave the Plex servers out, and signing out overwrites their rows.
- **Every new text** is in all nine languages.

The Plex API is used as [PlxNative](https://github.com/GLinnik21/plx-native) by GLinnik21
(GPL-3.0), a fast native Plex client for LG webOS, documents and uses it; its notes and code were the
reference for the sign-in, the connection ranking, the transcoder's parameters and the timeline. No
PlxNative code is included. Limits are in the guide's [Plex](GUIDE.md#plex) section (no Plex
Home profiles, no Plex live TV, music or photos, no transcoder quality choice).

Built and checked by the host tests, against recorded answers from plex.tv and a server; not yet
run on a console with a real Plex server.

### Install

Copy this release's `PPSA99003` folder (from `PPSA99003.zip`) over your current one, or choose
**Update now** when Nightshade TV 01.001.000 offers it. Your sources, favorites and settings stay.

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
