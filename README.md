<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="Physalis icon">
</p>

<h1 align="center">Physalis</h1>

<p align="center">
  <strong>Live TV, movies and your Plex library on PS5 homebrew, in Nightshade's black and red.</strong>
</p>

<p align="center">
  Formerly Nightshade TV
</p>

<p align="center">
  <a href="https://github.com/Oliv3rmoon/Physalis/releases/latest"><img src="https://img.shields.io/github/v/release/Oliv3rmoon/Physalis?style=for-the-badge&label=download&color=e5484d&labelColor=0d0d0f" alt="Download the latest release"></a>
  <a href="https://discord.gg/pSqgRU3YP9"><img src="https://img.shields.io/badge/discord-nightly-0d0d0f?style=for-the-badge&logo=discord&logoColor=e5484d" alt="Discord: nightly"></a>
  <a href="https://github.com/Oliv3rmoon/Physalis/stargazers"><img src="https://img.shields.io/github/stars/Oliv3rmoon/Physalis?style=for-the-badge&logo=github&logoColor=e5484d&color=e5484d&labelColor=0d0d0f" alt="GitHub stars"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-0d0d0f?style=for-the-badge&labelColor=0d0d0f&color=e5484d" alt="GPL-3.0-or-later"></a>
  <a href="https://buymeacoffee.com/oliv3rmoon"><img src="https://img.shields.io/badge/buy%20me%20a%20coffee-0d0d0f?style=for-the-badge&logo=buymeacoffee&logoColor=e5484d" alt="Buy me a coffee"></a>
</p>

Physalis is a native IPTV client for the PS5, and a Nightshade app by **[Oliv3rmoon](https://github.com/Oliv3rmoon)**: your own playlists and accounts, a programme guide, recording and multiview, now with Plex. The current release is **01.002.002**, a full release; what changed is in the [changelog](docs/CHANGELOG.md).

## Why Physalis

- **Your Plex library on the PS5.** Sign in with a code at plex.tv/link and your Plex servers' movies and TV shows appear in **On demand**, beside your playlists. A file plays as it is when the PS5 can decode it, through the server's transcoder otherwise, and Plex keeps your place on every device. See [Plex](#plex).
- **Free channels in one press.** With no source set up, **Square** on Live TV adds a community-maintained public playlist as an ordinary source. Physalis ships no channel list of its own, and you remove that source like any other.
- **A full TV app.** M3U playlists, Xtream accounts, MAC-code portals and tuners on your network; a programme guide with search and catch-up where the provider keeps an archive; movies and series; recording to USB; two or four channels side by side in multiview; pausing live TV; HDR10 and HLG.
- **Made for the couch.** DualSense navigation at up to 4K, a phone remote you pair with a QR code, the TV's own remote over HDMI-CEC, favorites in folders, a parental PIN and a children's mode, in nine languages.
- **Nightshade's look.** Black glass panels, a red accent, a near-black sky with a low red glow, and the nightshade flower on the icon and the home-screen pictures. The Ocean and Forest themes are still in **Settings**.
- **Updates from its own releases.** At the start it asks this repository for a newer release, shows its notes and installs it with **Update now**, after checking the ZIP against `SHA256SUMS`.
- **Your setup carries over.** It keeps the title ID (`PPSA99003`) and the data folder (`/data/prosperotv`), so your sources, favorites, recent channels and settings stay as they are.

## Physalis and ProsperoTV

Physalis installs over ProsperoTV, so here is where they differ. A dash means the other app's README doesn't list it.

| | Physalis | ProsperoTV |
| --- | --- | --- |
| M3U, Xtream, MAC-code portals and network tuners | Yes | Yes |
| Programme guide, recording, multiview, phone remote | Yes | Yes |
| Plex libraries as a source | Yes (since 01.002.000) | — |
| Free public channels in one press | Yes (since 01.002.002) | — |
| Look | Nightshade, black and red; Ocean and Forest | Dusk, Ocean and Forest |
| Where updates come from | This repository's GitHub releases | prosperostore.net |
| Title ID and data folder | `PPSA99003`, `/data/prosperotv` | The same |

## Install

1. Close the app if it is running.
2. Download `PPSA99003.zip` from the [latest release](https://github.com/Oliv3rmoon/Physalis/releases/latest) and unzip it.
3. Copy its `PPSA99003` folder over your current one (for example `/data/homebrew/PPSA99003`), replacing the files.

It keeps the original's title ID, so it installs over the original and your data stays where it is. The home screen calls it **Physalis**. Physalis starts with no channels: add a playlist or an account on the **Sources** tab, press **Square** on the empty Live TV screen for the free public channels, or choose **Add Plex**.

To go back, copy an [official release](https://github.com/ThatGuyKalvin/ProsperoTV/releases) over it the same way. If the original was installed as an image (`PPSA99003.ffpfsc`), remove the image first; [the guide](docs/GUIDE.md#updating-prosperotv) says how.

## Plex

A Plex server is one more source. In **Sources**, choose **Add Plex**: a four-character code appears beside a QR code. On a phone or computer signed in to Plex, open [plex.tv/link](https://plex.tv/link) and enter it. Every server your account reaches, its own and those shared with it, becomes a source, and its movies and TV shows are in **On demand**, with seasons, episodes, posters and details, search, Continue and the parental rules.

- **Plays as it is, or through the transcoder.** MP4, MOV, Matroska, WebM or MPEG-TS with H.264 (8-bit), HEVC or VP9 up to 3840 x 2160 play directly; anything else goes through the server's transcoder over HLS.
- **Limits.** No Plex Home profiles; no live TV, DVR, music or photos from Plex; no quality choice for the transcoder.
- **Not yet run on a console with a real Plex server.** The host tests check it against recorded answers from plex.tv and a server.

The full list of what works and what doesn't is in [the guide](docs/GUIDE.md#plex).

## Docs

- **[Guide](docs/GUIDE.md):** updates, Plex in full, sources, the programme guide, controls, the phone remote and the TV remote.
- **[Changelog](docs/CHANGELOG.md)**, and a [release](https://github.com/Oliv3rmoon/Physalis/releases) page for each version.
- **Setting up and fixing:** [Getting started](docs/GETTING_STARTED.md), [Troubleshooting](docs/TROUBLESHOOTING.md), [Configuration](docs/CONFIGURATION.md).
- **Building it:** [Architecture](docs/ARCHITECTURE.md), [Testing](docs/TESTING.md), [Deployment](docs/DEPLOYMENT.md), [Contributing](CONTRIBUTING.md), [Roadmap](ROADMAP.md).

## Requests, bugs and support

- **Ask for a fix or a feature:** [open an issue](https://github.com/Oliv3rmoon/Physalis/issues/new/choose), or ask in **[nightly on Discord](https://discord.gg/pSqgRU3YP9)**.
- **Bugs in my releases:** open an issue here. Turn on **Diagnostic log** in Settings first; Settings shows where the log is. Leave playlist addresses and passwords out.
- **If my work helps you,** you can [buy me a coffee](https://buymeacoffee.com/oliv3rmoon).

## The Nightshade family

Physalis is one of seven repositories in the Nightshade family, all by [Oliv3rmoon](https://github.com/Oliv3rmoon): the same black-and-red look, one Discord.

| App | What it is |
| --- | --- |
| [**Nightshade**](https://github.com/Oliv3rmoon/Nightshade) | The Switch emulator for PS5 homebrew |
| **Physalis** (this repo) | Live TV, movies and Plex on the PS5 |
| [**Datura**](https://github.com/Oliv3rmoon/Datura) | Game streaming from your PC to the PS5 |
| [**Mandrake**](https://github.com/Oliv3rmoon/Mandrake) | PS1 emulation, with two emulators |
| [**Dulcamara**](https://github.com/Oliv3rmoon/Dulcamara) | RPG Maker 2000 and 2003 games, and Ren'Py visual novels |
| [**Solanum**](https://github.com/Oliv3rmoon/Solanum) | A store for open-source PS5 homebrew, with its catalog [on the web](https://oliv3rmoon.github.io/Solanum-Catalog/) |
| [**Belladonna**](https://github.com/Oliv3rmoon/Belladonna) | Nightshade's PS5 Vulkan driver |

## Built on

[ProsperoTV](https://github.com/ThatGuyKalvin/ProsperoTV) 01.000.090, the native IPTV client for PS5 homebrew by BlackBearReloaded and ThatGuyKalvin.

## Credits

- **[ProsperoTV](https://github.com/ThatGuyKalvin/ProsperoTV)**, by **BlackBearReloaded**
  ([GitHub](https://github.com/blackbearreloaded)), who created it and wrote most of it, carried on
  by **ThatGuyKalvin** and its contributors and testers (see [Thanks](#thanks) below). Physalis
  is their app with a new look and a new update source; the credit for it is theirs.
- **[iptv-org](https://github.com/iptv-org/iptv)**, whose community-maintained public playlists
  ProsperoTV first played, and whose list **Square** on an empty Live TV screen adds as a source.
  Neither Physalis nor ProsperoTV ships it, neither hosts the streams, and neither is affiliated
  with iptv-org.
- **[Nightshade](https://github.com/Oliv3rmoon/Nightshade)**, whose palette, art code and GitHub
  update check this release reuses.
- **[PlxNative](https://github.com/GLinnik21/plx-native)**, by **GLinnik21**, whose documentation
  and code of the Plex API were the reference for the [Plex](#plex) source.
- **[PS5 Native App Boilerplate](https://github.com/LedyBacer/ps5-native-app-boilerplate)**, which
  ProsperoTV is built on: its C++20 application structure, reproducible clean-room runtime, FSELF
  tooling, tests, deployment flow and release automation.
- **[ps5-homebrew-ui](https://github.com/LedyBacer/ps5-homebrew-ui)**, the kit the interface is built with.
- Built with the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) by John Törnblom
  (ps5-payload-dev). Third-party components, authors and licenses are listed in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Thanks

Thank you to the people who test ProsperoTV on their own consoles and report what they find:

- **Bastian-R-dotcom** ([GitHub](https://github.com/Bastian-R-dotcom)), for the reports and requests behind Movies and TV shows, the Xtream and M3U fixes, the handling of badly formatted media, the 4K colour report, the film details report and newest-first On Demand.
- **Cold** ([GitHub](https://github.com/JMUtechnologies)), for helping with testing, the channel-by-channel sound measurements that found 5.1 sound playing too quietly, and the report of channels missing from a category.
- **Lighting999**, for the Jordan TV report that led to live playlists of any length.
- **laurensramandt** ([GitHub](https://github.com/laurensramandt)), for asking for custom user agents.
- **tvk-source** ([GitHub](https://github.com/tvk-source)), for asking for M3U Plus support.
- **Cevap05** ([GitHub](https://github.com/Cevap05)), for asking for large playlists.
- **toro-2** ([GitHub](https://github.com/toro-2)), for the first report of channels that would not load.

And to the people who contribute code:

- **ThatGuyKalvin** ([GitHub](https://github.com/ThatGuyKalvin)), for the playback menu, channel list and controls drawn at the screen's resolution, DTS sound, lip sync timed by when each picture reaches the screen, the sound in the statistics line, pictures 0.4 s sooner at every start and seek, newest-first On Demand, PlayStation button shapes in every hint, keeping whole lists when a provider sends oversized data, seeking within the open video, the HDR colour fix, the Stats line switch, the Continue row of a show, long on-demand lists, the quicker On Demand menu, the TV remote and the film and show counts of a source.
- **Cold** ([GitHub](https://github.com/JMUtechnologies)), for **Normalize channel volume**, which brings quiet and loud channels to a similar level.
- **Oliv3rmoon** ([GitHub](https://github.com/Oliv3rmoon)), for listing a channel in every category its provider names.

## License

Physalis is an unofficial release. ProsperoTV is made by BlackBearReloaded, ThatGuyKalvin and their contributors; neither they nor Sony endorse Physalis. It is free software under the GNU General Public License, version 3 or later, as ProsperoTV is ([LICENSE](LICENSE)).

Copyright © 2026 BlackBearReloaded and contributors. Licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). The repository's move to ThatGuyKalvin changes no copyright or licence notice. Third-party components keep their own licenses. Binary releases are built from the tagged source in this repository.

## Disclaimer

- **No affiliation.** This is an independent homebrew project. It is not
  affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment.
  "PlayStation", "PS5" and related marks are trademarks of Sony Interactive
  Entertainment Inc.
- **No proprietary material.** No Sony SDK, firmware, encryption keys or
  decrypted system modules are included.
- **No warranty.** This project is provided "as is", without warranty of any
  kind, to the extent permitted by law. See sections 15 and 16 of the GPL.
- **Use at your own risk.** Running homebrew requires a modified console, which
  may void its warranty, breach the platform's terms of service, or cause data
  loss.
- **Legal use only.** Use it only with hardware, accounts and content you own.
  This project does not support or enable piracy.

## AI assistance

This project was developed with AI assistance from OpenAI and/or Anthropic tools.

## Help it grow

Like Physalis? **[Star it](https://github.com/Oliv3rmoon/Physalis)** and **[join the Discord](https://discord.gg/pSqgRU3YP9)**.
