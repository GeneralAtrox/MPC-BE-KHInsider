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

### Signing in

Click **Sign in to KHInsider...** in the radio panel and sign in on KHInsider's
own page. Select **Stay logged in** there to retain your login between launches.
Accept the browser's **Save password** prompt to remember your username and
password. Close the sign-in window when finished; it stays open so that prompt
can be answered. The radio verifies access to Random Album Advanced in the
background.
Use **KHInsider account...** to open the site again, including to log out.

The app uses Microsoft Edge WebView2 for both sign-in and album/track page
requests. Its private browser profile is stored under
`%LOCALAPPDATA%\MPC-BE\KHInsider.WebView2`, separate from player settings and
playlists. Do not distribute that profile. Saved passwords and the site's
session cookies remain in the browser profile, never in player settings or
source code. If access expires, the radio asks you to sign in again.
Site verification and network failures have separate
messages.

The [Microsoft Edge WebView2 Evergreen Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)
must be installed. The SDK's static loader is linked into the player, so there
is no additional WebView2 DLL to copy beside the executable.

### Window size

The main window reserves room for the radio form, accounting for Windows display
scaling. On smaller displays, or when the panel is floating, scrollbars keep the
whole form reachable. Keyboard navigation scrolls the focused control into view.

### Building and checking this change

Use the existing Visual Studio/MFC and MSYS build setup. The player project
automatically restores the pinned WebView2 SDK from NuGet on its first build;
internet access is required for that restore.

The focused session tests can be built independently of the media libraries:

```powershell
msbuild tests\khinsider-session.vcxproj /p:Configuration=Debug /p:Platform=x64
_bin\tests\khinsider-session.exe
_bin\tests\khinsider-session.exe --live
_bin\tests\khinsider-session.exe --require-login
```

`--live` checks the current KHInsider session. After signing in and closing the
player, run `--require-login` in separate processes to verify that the saved
session survives a restart; this mode fails if the session is logged out.
`--login` opens the actual login page and, after sign-in, requests three random
albums. It uses the same private
profile as the player. These checks do not validate audible playback; also test
the player at 100%, 150% and 200% scaling, with the panel docked and floating.

<img width="1680" height="1002" alt="image" src="https://github.com/user-attachments/assets/1d926377-b7e7-45a7-86a5-f450e5f5ac9b" />
