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

<img width="1680" height="1002" alt="image" src="https://github.com/user-attachments/assets/f002b47e-6660-4aa0-8486-8f33ba3bb8a2" />
