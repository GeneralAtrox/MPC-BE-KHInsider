# curl-impersonate (vendored runtime DLLs)

KH Radio uses [curl-impersonate](https://github.com/lexiforest/curl-impersonate)
as an optional, Cloudflare-resilient HTTP transport for `downloads.khinsider.com`.
It forges a real Chrome TLS/HTTP-2 fingerprint, which gets through Cloudflare's
bot challenge on networks where plain WinHTTP is blocked (a 403 / "Just a
moment..." page). See `src/apps/mplayerc/KHInsider.cpp` (`URLRequestCurl` /
`URLRequest` dispatcher).

## Files

| File | Source |
|------|--------|
| `libcurl-impersonate.dll` | libcurl 8.15.0-IMPERSONATE, BoringSSL, statically linked (`/MT`) |
| `zlib.dll` | zlib dependency of the above |

Both are taken from the official release asset
`libcurl-impersonate-v1.5.6.x86_64-win32.tar.gz` (the `bin/` folder).

Dependencies are Windows system DLLs only (crypt32, iphlpapi, ws2_32,
normaliz, wldap32, kernel32) — **no VC++ runtime required**. They must sit
next to `mpc-be64.exe`.

## How it's used

The DLL is loaded lazily via `LoadLibraryEx` and is **optional**: if it is
missing or fails to load, the scraper falls back to WinHTTP, so trusted
networks and dev builds keep working. Certificate verification uses the
Windows certificate store (`CURLSSLOPT_NATIVE_CA`), so no CA bundle is shipped.

## Maintenance — refreshing the fingerprint

Cloudflare flags **stale** Chrome fingerprints (during testing, `chrome120`
and `chrome124` were already blocked; only the newest passed). When KH Radio
starts failing with curl challenges:

1. Download the newest `libcurl-impersonate-vX.Y.Z.x86_64-win32.tar.gz` from
   the [releases page](https://github.com/lexiforest/curl-impersonate/releases).
2. Replace `libcurl-impersonate.dll` + `zlib.dll` here and in the release zip.
3. Bump `KH_IMPERSONATE_TARGET` in `KHInsider.cpp` to the newest `chromeNNN`
   target the new DLL supports (check `bin/curl_chromeNNN.bat` in the tarball).

## License

curl is distributed under the [curl license](https://curl.se/docs/copyright.html)
(MIT/X-derivative), which is GPL-compatible. BoringSSL and zlib carry their own
permissive licenses. curl-impersonate is MIT-licensed.
