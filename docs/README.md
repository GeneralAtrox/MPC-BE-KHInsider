# Media Player Classic - KH Radio Edition (MPC-BE)

A fork of [MPC-BE](https://github.com/Aleksoid1978/MPC-BE) that turns the player into a dedicated
**video game music radio** powered by [KHInsider](https://downloads.khinsider.com/).

## KH Radio

This edition replaces regular media playback with a single feature: **KH Radio**, a dockable panel
that replicates the site's [Random Album Advanced](https://downloads.khinsider.com/random-album-advanced) page.

* Filter random albums by **Album Type**, **Year** and **Platform** (Ctrl-click for multiple selections)
* One click on **Show Me A Random Album** fetches a random album and streams it track by track
* Your filter selections are remembered between sessions
* Every album and song you listen to is recorded to a local history (`khradio_history.json`),
  shown in the panel — double-click a history entry to replay that album
* Optionally skip albums you have already heard when rolling
* Local file playback (Open File/DVD/Device, drag-and-drop, recent files, command line) is disabled —
  this player is radio-only

Toggle the panel via **View → KH Radio**.

---

## About MPC-BE

MPC-BE is a free and open source audio and video player for Windows, based on the original
Guliverkli project and "Media Player Classic Home Cinema", with additional features and bug fixes.

### System requirements:
* An SSE2 capable CPU
* Video card supporting DirectX9.0c (PS 3.0)
* Windows 7, 8, 8.1, 10, 11 32-bit/64-bit

### Upstream links
- [MPC-BE Releases      ](https://github.com/Aleksoid1978/MPC-BE/releases)
- [MPC-BE Wiki          ](https://github.com/Aleksoid1978/MPC-BE/wiki)

---

For the people involved in the development, see Authors.txt.
MPC-BE's code is licensed under GPL v3 (see LICENSE).

Translations are done by various translators (see Authors.txt).

---

MPC-BE makes use of the following 3rd party code:

| Project           | License             | Website                                               |
|-------------------|---------------------|-------------------------------------------------------|
| Bento4            | GPLv2               | https://www.bento4.com/                               |
| CFileVersionInfo  |                     |                                                       |
| CLineNumberEdit   |                     |                                                       |
| compact_enc_det   | Apache-2.0 license  | https://github.com/google/compact_enc_det             |
| coolsb            |                     | https://www.codeproject.com/KB/dialog/coolscroll.aspx |
| CSizingControlBar | GPLv2               | http://datamekanix.com/sizecbar/                      |
| Detours           | MIT License         | https://github.com/microsoft/detours/                 |
| fdk-aac           |                     | https://github.com/mstorsjo/fdk-aac/                  |
| FFmpeg            | GPLv3               | http://ffmpeg.org/                                    |
| dav1d             | BSD License         | https://code.videolan.org/videolan/dav1d/             |
| libdivide         | zlib/Boost License  | https://libdivide.com/                                |
| libflac           | GPLv2/BSD License   | https://github.com/xiph/flac                          |
| libpng            | zlib/libpng License | https://github.com/glennrp/libpng/                    |
| libspeex          | BSD License         | https://speex.org/                                    |
| Little CMS        | MIT License         | https://littlecms.com/                                |
| Logitech SDK      |                     |                                                       |
| MediaInfo         | BSD License         | https://mediaarea.net/MediaInfo                       |
| mfx_dispatch      | MIT License         | https://github.com/Intel-Media-SDK/MediaSDK           |
| RapidJSON         | MIT License         | https://github.com/Tencent/rapidjson                  |
| ResizableLib      | Artistic License    | https://github.com/ppescher/resizablelib              |
| soxr              | LGPL                | https://sourceforge.net/projects/soxr/                |
| TreePropSheet     |                     |                                                       |
| uavs3d            | BSD License         | https://github.com/uavs3/uavs3d                       |
| VirtualDub        | GPLv2               | https://virtualdub.org/                               |
| ZenLib            | zlib License        | https://github.com/MediaArea/ZenLib                   |
| zlib              | zlib License        | https://zlib.net/                                     |
| bs2b              | MIT License         | https://bs2b.sourceforge.net/                         |
| VVdeC             | BSD License         | https://github.com/fraunhoferhhi/vvdec/               |
