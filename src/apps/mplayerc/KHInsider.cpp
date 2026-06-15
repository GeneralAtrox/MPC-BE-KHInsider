/*
 * (C) 2026 MPC-BE KH Radio edition
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "mplayerc.h"
#include "DSUtil/text.h"
#include "KHInsider.h"

#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <map>
#include <regex>
#include <string>
#include <cmath>

#include "rapidjsonHelper.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "ExtLib/minimp3/minimp3.h"

#define KHINSIDER_HOST L"downloads.khinsider.com"
#define KHINSIDER_BASE L"https://downloads.khinsider.com"

// downloads.khinsider.com sits behind Cloudflare, which rejects requests
// that do not look like they come from a real browser.
#define KH_USER_AGENT L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"

#ifndef WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL
#define WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL 133
#endif
#ifndef WINHTTP_PROTOCOL_FLAG_HTTP2
#define WINHTTP_PROTOCOL_FLAG_HTTP2 0x1
#endif
#ifndef WINHTTP_OPTION_DISABLE_FEATURE
#define WINHTTP_OPTION_DISABLE_FEATURE 63
#endif
#ifndef WINHTTP_DISABLE_COOKIES
#define WINHTTP_DISABLE_COOKIES 0x00000001
#endif
#ifndef WINHTTP_DISABLE_KEEP_ALIVE
#define WINHTTP_DISABLE_KEEP_ALIVE 0x00000002
#endif

namespace KHInsider
{
	using urlData = std::vector<char>;

	// Appends a timestamped line to %APPDATA%\MPC-BE\khradio_debug.log.
	static void KHLog(LPCWSTR fmt, ...)
	{
		CStringW path;
		if (!AfxGetMyApp()->GetAppSavePath(path)) {
			return;
		}
		path += L"khradio_debug.log";

		CStringW msg;
		va_list args;
		va_start(args, fmt);
		msg.FormatV(fmt, args);
		va_end(args);

		SYSTEMTIME st;
		GetLocalTime(&st);
		CStringW line;
		line.Format(L"[%02u:%02u:%02u.%03u] %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg.GetString());
		const CStringA utf8 = WStrToUTF8(line);

		CFile f;
		if (f.Open(path, CFile::modeWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::shareDenyNone)) {
			f.SeekToEnd();
			f.Write(utf8.GetString(), utf8.GetLength());
			f.Close();
		}
	}

	// GET or POST (form-urlencoded when 'body' is non-empty) with browser-like
	// headers over WinHTTP. Cloudflare on downloads.khinsider.com only accepts
	// browser user-agents speaking HTTP/2, which WinHTTP supports (WinInet's
	// HTTP/2 option does not take effect and gets a 403). Redirects are
	// followed; 'pFinalUrl' (optional) receives the URL we actually ended up on.
	static bool URLRequest(LPCWSTR verb, const CStringW& url, const CStringA& body, urlData& pData, CStringW* pFinalUrl = nullptr,
						   LPCWSTR extraHeaders = nullptr, size_t maxBytes = 0, DWORD* pHttpStatus = nullptr)
	{
		pData.clear();
		if (pFinalUrl) {
			pFinalUrl->Empty();
		}
		if (pHttpStatus) {
			*pHttpStatus = 0;
		}

		WCHAR host[256] = {};
		WCHAR path[2048] = {};
		URL_COMPONENTSW uc = { sizeof(uc) };
		uc.lpszHostName = host;
		uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
		uc.lpszUrlPath = path;
		uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
		if (!WinHttpCrackUrl(url, 0, 0, &uc)) {
			KHLog(L"WinHttpCrackUrl failed for %s (err %lu)", url.GetString(), GetLastError());
			return false;
		}

		DWORD dwStatus = 0;

		if (auto hSession = WinHttpOpen(KH_USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)) {
			DWORD dwProto = WINHTTP_PROTOCOL_FLAG_HTTP2;
			WinHttpSetOption(hSession, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &dwProto, sizeof(dwProto));

			if (auto hConnect = WinHttpConnect(hSession, host, uc.nPort, 0)) {
				const DWORD dwFlags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
				if (auto hRequest = WinHttpOpenRequest(hConnect, verb, path, nullptr,
													   KHINSIDER_BASE L"/",
													   WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags)) {

					// Don't carry cookies or pooled connections between requests:
					// over the app's lifetime the server otherwise pins the
					// "random" album to the session and keeps returning the same one.
					DWORD dwDisable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_KEEP_ALIVE;
					WinHttpSetOption(hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &dwDisable, sizeof(dwDisable));

					CStringW headers =
						L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
						L"Accept-Language: en-US,en;q=0.9\r\n";
					if (!body.IsEmpty()) {
						headers += L"Content-Type: application/x-www-form-urlencoded\r\n";
					}
					if (extraHeaders) {
						headers += extraHeaders;
					}

					CStringA requestData(body);
					if (WinHttpSendRequest(hRequest, headers, static_cast<DWORD>(-1),
										   body.IsEmpty() ? WINHTTP_NO_REQUEST_DATA : reinterpret_cast<LPVOID>(requestData.GetBuffer()),
										   requestData.GetLength(), requestData.GetLength(), 0)
							&& WinHttpReceiveResponse(hRequest, nullptr)) {

						DWORD dwLen = sizeof(dwStatus);
						WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
											WINHTTP_HEADER_NAME_BY_INDEX, &dwStatus, &dwLen, WINHTTP_NO_HEADER_INDEX);

						std::vector<char> tmp(16 * 1024);
						for (;;) {
							DWORD dwSizeRead = 0;
							if (!WinHttpReadData(hRequest, reinterpret_cast<LPVOID>(tmp.data()), static_cast<DWORD>(tmp.size()), &dwSizeRead) || !dwSizeRead) {
								break;
							}
							pData.insert(pData.end(), tmp.begin(), tmp.begin() + dwSizeRead);
							if (maxBytes && pData.size() >= maxBytes) {
								break;
							}
						}

						if (!pData.empty()) {
							pData.emplace_back('\0');

							if (pFinalUrl) {
								WCHAR urlBuf[2048] = {};
								DWORD urlLen = sizeof(urlBuf);
								if (WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, urlBuf, &urlLen)) {
									*pFinalUrl = urlBuf;
								}
							}
						}
					} else {
						KHLog(L"%s %s: send/receive failed (err %lu)", verb, url.GetString(), GetLastError());
					}

					WinHttpCloseHandle(hRequest);
				}
				WinHttpCloseHandle(hConnect);
			}
			WinHttpCloseHandle(hSession);
		}

		if (pHttpStatus) {
			*pHttpStatus = dwStatus;
		}

		KHLog(L"%s %s -> status %lu, %u bytes%s%s", verb, url.GetString(), dwStatus,
			  static_cast<unsigned>(pData.size()),
			  pFinalUrl && !pFinalUrl->IsEmpty() ? L", final: " : L"",
			  pFinalUrl ? pFinalUrl->GetString() : L"");

		if (dwStatus >= 400) {
			pData.clear();
			return false;
		}

		return !pData.empty();
	}

	void DebugLog(const CStringW& msg)
	{
		KHLog(L"%s", msg.GetString());
	}

	CStringW DecodeHtmlEntities(CStringW str)
	{
		str.Replace(L"&quot;", L"\"");
		str.Replace(L"&#34;", L"\"");
		str.Replace(L"&#39;", L"'");
		str.Replace(L"&apos;", L"'");
		str.Replace(L"&lt;", L"<");
		str.Replace(L"&gt;", L">");
		str.Replace(L"&nbsp;", L" ");

		// numeric entities: &#1234; and &#x12AB;
		int pos = 0;
		while ((pos = str.Find(L"&#", pos)) != -1) {
			const int semi = str.Find(L';', pos);
			if (semi == -1 || semi - pos > 9) {
				pos += 2;
				continue;
			}
			const CStringW num = str.Mid(pos + 2, semi - pos - 2);
			WCHAR* endPtr = nullptr;
			long code = 0;
			if (!num.IsEmpty() && (num[0] == L'x' || num[0] == L'X')) {
				code = wcstol(num.Mid(1), &endPtr, 16);
			} else {
				code = wcstol(num, &endPtr, 10);
			}
			if (code > 0 && code < 0x10000 && endPtr && *endPtr == 0) {
				str.Delete(pos, semi - pos + 1);
				str.Insert(pos, static_cast<WCHAR>(code));
				pos++;
			} else {
				pos += 2;
			}
		}

		str.Replace(L"&amp;", L"&"); // last, so "&amp;quot;" doesn't double-decode
		return str;
	}

	static CStringW StripTags(const CStringW& str)
	{
		CStringW out;
		bool inTag = false;
		for (int i = 0; i < str.GetLength(); i++) {
			const WCHAR ch = str[i];
			if (ch == L'<') {
				inTag = true;
			} else if (ch == L'>') {
				inTag = false;
			} else if (!inTag) {
				out += ch;
			}
		}
		return out;
	}

	static CStringW Trimmed(CStringW str)
	{
		str.Trim(L" \t\r\n");
		return str;
	}

	static bool ParseAlbumPage(const urlData& data, const CStringW& pageUrl, Album& album)
	{
		album.url = pageUrl;
		album.title.Empty();
		album.tracks.clear();

		const std::string html(data.data());

		// Album title: first <h2> inside the page content.
		size_t contentPos = html.find("id=\"pageContent\"");
		if (contentPos == std::string::npos) {
			contentPos = 0;
		}
		const size_t h2Start = html.find("<h2>", contentPos);
		if (h2Start != std::string::npos) {
			const size_t h2End = html.find("</h2>", h2Start);
			if (h2End != std::string::npos) {
				const std::string titleHtml = html.substr(h2Start + 4, h2End - h2Start - 4);
				album.title = Trimmed(DecodeHtmlEntities(StripTags(UTF8ToWStr(titleHtml.c_str()))));
			}
		}

		// Track list: links inside the songlist table, skipping the
		// header/footer rows (their cells may hold sort/download links).
		size_t listStart = html.find("id=\"songlist\"");
		size_t listEnd;
		bool bFallback = false;
		if (listStart != std::string::npos) {
			const size_t headerPos = html.find("id=\"songlist_header\"", listStart);
			if (headerPos != std::string::npos) {
				const size_t headerEnd = html.find("</tr>", headerPos);
				if (headerEnd != std::string::npos) {
					listStart = headerEnd;
				}
			}
			listEnd = html.find("id=\"songlist_footer\"", listStart);
			if (listEnd == std::string::npos) {
				listEnd = html.find("</table>", listStart);
			}
			if (listEnd == std::string::npos) {
				listEnd = html.size();
			}
		} else {
			// The songlist marker is gone - the site may have been redesigned.
			// Scan the whole page, but only accept links under THIS album's
			// path so we don't pick up sidebar links to other albums.
			bFallback = true;
			listStart = 0;
			listEnd = html.size();
			KHLog(L"songlist marker not found - scanning whole page (site layout may have changed)");
		}
		const std::string songlist = html.substr(listStart, listEnd - listStart);

		// album path prefix, e.g. "/game-soundtracks/album/<slug>/", for the fallback
		std::string albumPrefix;
		{
			const CStringA puA = WStrToUTF8(pageUrl);
			const std::string pu(puA.GetString());
			const size_t ap = pu.find("/game-soundtracks/album/");
			if (ap != std::string::npos) {
				albumPrefix = pu.substr(ap) + "/";
			}
		}

		static const std::regex reLink("<a\\s+href=\"(/game-soundtracks/album/[^\"]+)\"[^>]*>([^<]+)</a>", std::regex::icase);
		static const std::regex reDuration("^\\d+:\\d{2}(:\\d{2})?$");
		static const std::regex reSize("^[\\d.,]+\\s*[KMG]B$", std::regex::icase);

		std::map<std::string, bool> seen;
		std::string lastHref; // href of the most recently added track (for its duration cell)

		for (auto it = std::sregex_iterator(songlist.begin(), songlist.end(), reLink); it != std::sregex_iterator(); ++it) {
			const std::string href = (*it)[1].str();
			std::string text = (*it)[2].str();

			// in fallback mode keep only this album's own track links
			if (bFallback && (albumPrefix.empty() || href.compare(0, albumPrefix.size(), albumPrefix) != 0)) {
				continue;
			}

			CStringW name = Trimmed(DecodeHtmlEntities(UTF8ToWStr(text.c_str())));
			if (name.IsEmpty()) {
				continue;
			}
			const std::string nameUtf8(WStrToUTF8(name));
			if (std::regex_match(nameUtf8, reDuration)) {
				// each row has a duration cell linking to the same track page;
				// record it against the track we just added
				if (!album.tracks.empty() && href == lastHref && album.tracks.back().durationSec == 0) {
					int a = 0, b = 0, c = 0;
					const int parts = sscanf_s(nameUtf8.c_str(), "%d:%d:%d", &a, &b, &c);
					album.tracks.back().durationSec = (parts == 3) ? (a * 3600 + b * 60 + c)
													: (parts == 2) ? (a * 60 + b) : 0;
				}
				continue;
			}
			if (std::regex_match(nameUtf8, reSize)) {
				continue;
			}
			if (seen.find(href) != seen.end()) {
				continue;
			}
			seen[href] = true;

			Track track;
			track.name = name;
			track.pageUrl = KHINSIDER_BASE + DecodeHtmlEntities(UTF8ToWStr(href.c_str()));
			album.tracks.emplace_back(track);
			lastHref = href;
		}

		if (album.title.IsEmpty() && !pageUrl.IsEmpty()) {
			// fall back to the URL slug
			const int slash = pageUrl.ReverseFind(L'/');
			if (slash != -1) {
				CStringW slug = pageUrl.Mid(slash + 1);
				slug.Replace(L'-', L' ');
				album.title = slug;
			}
		}

		// album cover: the full-size image linked inside <div class="albumImage">
		const size_t imgPos = html.find("class=\"albumImage\"");
		if (imgPos != std::string::npos) {
			const std::string imgBlock = html.substr(imgPos, 600);
			static const std::regex reCover("href=\"(https?://[^\"]+\\.(jpg|jpeg|png|gif|webp))\"", std::regex::icase);
			std::smatch m;
			if (std::regex_search(imgBlock, m, reCover)) {
				album.coverUrl = DecodeHtmlEntities(UTF8ToWStr(m[1].str().c_str()));
			}
		}

		KHLog(L"parsed album: '%s', %u tracks, cover %s", album.title.GetString(), static_cast<unsigned>(album.tracks.size()),
			  album.coverUrl.IsEmpty() ? L"(none)" : album.coverUrl.GetString());

		return !album.tracks.empty();
	}

	CStringA BuildRandomAlbumForm(const std::vector<int>& types, const std::vector<int>& years, const std::vector<int>& platforms)
	{
		CStringA body;
		for (const auto v : types) {
			body.AppendFormat("type%%5B%%5D=%d&", v);
		}
		for (const auto v : years) {
			body.AppendFormat("year%%5B%%5D=%d&", v);
		}
		for (const auto v : platforms) {
			body.AppendFormat("category%%5B%%5D=%d&", v);
		}
		body += "randomAdvanced=Show+Me+A+Random+Album";
		return body;
	}

	// Translate an HTTP failure into a FetchStatus for the UI.
	static FetchStatus StatusFromHttp(DWORD httpStatus)
	{
		return (httpStatus >= 400) ? FetchStatus::Blocked : FetchStatus::NetworkError;
	}

	bool FetchRandomAlbum(const CStringA& formBody, Album& album, FetchStatus* pStatus)
	{
		// Unique query each call so no cache/edge layer can serve a stale
		// (always-identical) "random" album back to us.
		static volatile LONG s_nonce = 0;
		const LONG nonce = ::InterlockedIncrement(&s_nonce);
		CStringW url;
		url.Format(L"%s/random-album-advanced?_=%lu%ld", KHINSIDER_BASE, ::GetTickCount(), nonce);

		urlData data;
		CStringW finalUrl;
		DWORD httpStatus = 0;
		if (!URLRequest(L"POST", url, formBody, data, &finalUrl, nullptr, 0, &httpStatus)) {
			if (pStatus) { *pStatus = StatusFromHttp(httpStatus); }
			return false;
		}

		// strip the "?from=random" tail so history entries compare cleanly
		const int query = finalUrl.Find(L'?');
		if (query != -1) {
			finalUrl.Truncate(query);
		}

		if (!ParseAlbumPage(data, finalUrl, album)) {
			if (pStatus) { *pStatus = FetchStatus::LayoutChanged; }
			return false;
		}

		if (pStatus) { *pStatus = FetchStatus::Success; }
		return true;
	}

	bool FetchAlbum(const CStringW& albumUrl, Album& album, FetchStatus* pStatus)
	{
		urlData data;
		DWORD httpStatus = 0;
		if (!URLRequest(L"GET", albumUrl, CStringA(), data, nullptr, nullptr, 0, &httpStatus)) {
			if (pStatus) { *pStatus = StatusFromHttp(httpStatus); }
			return false;
		}

		if (!ParseAlbumPage(data, albumUrl, album)) {
			if (pStatus) { *pStatus = FetchStatus::LayoutChanged; }
			return false;
		}

		if (pStatus) { *pStatus = FetchStatus::Success; }
		return true;
	}

	CStringW ResolveTrackAudioUrl(const CStringW& trackPageUrl)
	{
		urlData data;
		if (!URLRequest(L"GET", trackPageUrl, CStringA(), data)) {
			return L"";
		}

		const std::string html(data.data());

		static const std::regex reAudio("<audio[^>]*\\ssrc=\"([^\"]+)\"", std::regex::icase);
		static const std::regex reMp3("href=\"(https?://[^\"]+\\.mp3)\"", std::regex::icase);
		static const std::regex reOther("href=\"(https?://[^\"]+\\.(flac|m4a|ogg))\"", std::regex::icase);

		std::smatch match;
		if (std::regex_search(html, match, reAudio)
				|| std::regex_search(html, match, reMp3)
				|| std::regex_search(html, match, reOther)) {
			return DecodeHtmlEntities(UTF8ToWStr(match[1].str().c_str()));
		}

		KHLog(L"no audio URL found on %s", trackPageUrl.GetString());
		return L"";
	}

	bool DownloadToFile(const CStringW& url, const CStringW& localPath)
	{
		urlData data;
		if (!URLRequest(L"GET", url, CStringA(), data) || data.size() < 2) {
			return false;
		}

		CFile f;
		if (!f.Open(localPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive)) {
			return false;
		}
		// URLRequest appends a trailing '\0' for string callers; drop it here
		f.Write(data.data(), static_cast<UINT>(data.size() - 1));
		f.Close();
		return true;
	}

	// Classify a track as music, spoken, or (near-)silent. First a cheap title
	// check for spoken content, then a content analysis of a downloaded prefix:
	// a prefix that never gets above a faint loudness is treated as empty/quiet;
	// speech has a characteristic ~4 Hz syllabic energy modulation, more pauses,
	// and higher zero-crossing-rate variance than music. Conservative thresholds
	// favour keeping music; every decision is logged so cut-offs can be tuned.
	AudioVerdict ClassifyTrackAudio(const CStringW& audioUrl, const CStringW& trackName)
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr double kQuietPeakRms = 0.015; // prefix never louder than this -> empty/quiet

		CStringW lower = trackName;
		lower.MakeLower();
		static const LPCWSTR spokenWords[] = {
			L"dialogue", L"dialog", L"voice", L"narration", L"narrator", L"monologue",
			L"interview", L"commentary", L"drama cd", L"audio drama", L"radio drama",
			L"skit", L"cutscene", L"cut scene", L"voice clip", L"voices",
			L"\u30DC\u30A4\u30B9",                 // voice (boisu)
			L"\u30BB\u30EA\u30D5",                 // serifu (spoken lines)
			L"\u30C9\u30E9\u30DE",                 // drama
			L"\u30CA\u30EC\u30FC\u30B7\u30E7\u30F3", // narration
			L"\u4F1A\u8A71",                       // kaiwa (conversation)
		};
		for (const auto w : spokenWords) {
			if (lower.Find(w) != -1) {
				KHLog(L"audio check '%s': title match -> SPOKEN", trackName.GetString());
				return AudioVerdict::Spoken;
			}
		}

		// download a short prefix (~256 KB, enough for ~10-15s)
		urlData mp3;
		if (!URLRequest(L"GET", audioUrl, CStringA(), mp3, nullptr, L"Range: bytes=0-262143\r\n", 262144) || mp3.size() < 8192) {
			return AudioVerdict::Music; // can't analyse -> keep the track
		}

		// decode to mono float PCM
		mp3dec_t dec;
		mp3dec_init(&dec);
		mp3dec_frame_info_t info = {};
		std::vector<float> mono;
		const uint8_t* buf = reinterpret_cast<const uint8_t*>(mp3.data());
		int bytesLeft = static_cast<int>(mp3.size()) - 1; // drop appended '\0'
		int hz = 0;
		short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
		const size_t maxSamples = 30u * 48000u;
		while (bytesLeft > 0 && mono.size() < maxSamples) {
			const int samples = mp3dec_decode_frame(&dec, buf, bytesLeft, pcm, &info);
			if (info.frame_bytes <= 0) {
				break;
			}
			buf += info.frame_bytes;
			bytesLeft -= info.frame_bytes;
			if (samples > 0) {
				hz = info.hz;
				if (info.channels >= 2) {
					for (int i = 0; i < samples; i++) {
						mono.push_back((pcm[2 * i] + pcm[2 * i + 1]) * (0.5f / 32768.0f));
					}
				} else {
					for (int i = 0; i < samples; i++) {
						mono.push_back(pcm[i] / 32768.0f);
					}
				}
			}
		}

		if (hz <= 0 || mono.size() < static_cast<size_t>(hz)) {
			return AudioVerdict::Music; // need at least ~1s of audio to judge -> keep
		}

		// 20 ms frames -> energy envelope (50 fps) and zero-crossing rate
		const int frameLen = hz / 50;
		const int nFrames = static_cast<int>(mono.size() / frameLen);
		if (frameLen < 1 || nFrames < 25) {
			return AudioVerdict::Music;
		}

		std::vector<float> energy(nFrames), zcr(nFrames);
		double maxE = 0.0;
		for (int f = 0; f < nFrames; f++) {
			const float* x = &mono[static_cast<size_t>(f) * frameLen];
			double e = 0.0;
			int zc = 0;
			for (int i = 0; i < frameLen; i++) {
				e += static_cast<double>(x[i]) * x[i];
				if (i > 0 && ((x[i] >= 0.0f) != (x[i - 1] >= 0.0f))) {
					zc++;
				}
			}
			energy[f] = static_cast<float>(std::sqrt(e / frameLen));
			zcr[f] = static_cast<float>(zc) / frameLen;
			if (energy[f] > maxE) maxE = energy[f];
		}

		double meanE = 0.0;
		for (const float e : energy) meanE += e;
		meanE /= nFrames;

		// empty/very quiet: the loudest 20 ms in the whole prefix is still faint
		if (maxE < kQuietPeakRms) {
			KHLog(L"audio check '%s': peak=%.4f mean=%.4f -> QUIET", trackName.GetString(), maxE, meanE);
			return AudioVerdict::Quiet;
		}

		int pauses = 0;
		for (const float e : energy) if (e < 0.25 * meanE) pauses++;
		const double pauseRatio = static_cast<double>(pauses) / nFrames;

		double meanZ = 0.0;
		for (const float z : zcr) meanZ += z;
		meanZ /= nFrames;
		double varZ = 0.0;
		for (const float z : zcr) varZ += (z - meanZ) * (z - meanZ);
		varZ /= nFrames;
		const double zcrCoV = meanZ > 1e-6 ? std::sqrt(varZ) / meanZ : 0.0;

		// 4 Hz modulation: power of the energy envelope in the 3-6 Hz speech
		// band relative to the whole 1-15 Hz range (envelope sampled at 50 Hz)
		const double envFs = 50.0;
		std::vector<double> env(nFrames);
		for (int f = 0; f < nFrames; f++) env[f] = energy[f] - meanE;
		const auto bandPower = [&](double f0, double f1) -> double {
			double total = 0.0;
			for (double fr = f0; fr <= f1 + 1e-9; fr += 0.5) {
				double re = 0.0, im = 0.0;
				for (int n = 0; n < nFrames; n++) {
					const double ph = 2.0 * kPi * fr * n / envFs;
					re += env[n] * std::cos(ph);
					im -= env[n] * std::sin(ph);
				}
				total += re * re + im * im;
			}
			return total;
		};
		const double modRatio = bandPower(3.0, 6.0) / (bandPower(1.0, 15.0) + 1e-9);

		// Speech needs ALL of: gaps between phrases (pauseRatio), ~4 Hz syllabic
		// rhythm (modRatio) and voiced/unvoiced alternation (zcrCoV). Requiring
		// the pause gate is what stops loud, gapless music (which can still have
		// high modRatio/zcrCoV) from being misflagged as spoken.
		const bool spoken = (pauseRatio > 0.18) && (modRatio > 0.32) && (zcrCoV > 0.50);

		KHLog(L"audio check '%s': mod=%.2f pause=%.2f zcrCoV=%.2f peak=%.3f -> %s",
			  trackName.GetString(), modRatio, pauseRatio, zcrCoV, maxE, spoken ? L"SPOKEN" : L"music");

		return spoken ? AudioVerdict::Spoken : AudioVerdict::Music;
	}
} // namespace KHInsider

// CKHRadioHistory

CStringW CKHRadioHistory::GetPath() const
{
	CStringW path;
	if (AfxGetMyApp()->GetAppSavePath(path)) {
		path += L"khradio_history.json";
		return path;
	}
	return L"";
}

void CKHRadioHistory::Load()
{
	m_albums.clear();

	const CStringW path = GetPath();
	if (path.IsEmpty() || !::PathFileExistsW(path)) {
		return;
	}

	std::vector<char> data;
	CFile file;
	if (!file.Open(path, CFile::modeRead | CFile::shareDenyWrite)) {
		return;
	}
	const auto len = static_cast<UINT>(file.GetLength());
	data.resize(len + 1);
	file.Read(data.data(), len);
	data[len] = '\0';
	file.Close();

	rapidjson::Document json;
	json.Parse(data.data());
	if (json.HasParseError()) {
		return;
	}

	if (const auto* albums = GetJsonArray(json, "albums")) {
		for (const auto& a : albums->GetArray()) {
			if (!a.IsObject()) {
				continue;
			}
			AlbumEntry entry;
			getJsonValue(a, "url", entry.url);
			getJsonValue(a, "title", entry.title);
			if (entry.url.IsEmpty()) {
				continue;
			}
			if (const auto* tracks = GetJsonArray(a, "tracks")) {
				for (const auto& t : tracks->GetArray()) {
					if (!t.IsObject()) {
						continue;
					}
					TrackEntry te;
					getJsonValue(t, "name", te.name);
					getJsonValue(t, "plays", te.plays);
					if (!te.name.IsEmpty()) {
						entry.tracks.emplace_back(te);
					}
				}
			}
			m_albums.emplace_back(entry);
		}
	}
}

void CKHRadioHistory::Save() const
{
	const CStringW path = GetPath();
	if (path.IsEmpty()) {
		return;
	}

	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> w(sb);

	w.StartObject();
	w.Key("albums");
	w.StartArray();
	for (const auto& album : m_albums) {
		w.StartObject();
		w.Key("url");
		const CStringA url(WStrToUTF8(album.url));
		w.String(url.GetString(), url.GetLength());
		w.Key("title");
		const CStringA title(WStrToUTF8(album.title));
		w.String(title.GetString(), title.GetLength());
		w.Key("tracks");
		w.StartArray();
		for (const auto& track : album.tracks) {
			w.StartObject();
			w.Key("name");
			const CStringA name(WStrToUTF8(track.name));
			w.String(name.GetString(), name.GetLength());
			w.Key("plays");
			w.Int(track.plays);
			w.EndObject();
		}
		w.EndArray();
		w.EndObject();
	}
	w.EndArray();
	w.EndObject();

	CFile file;
	if (file.Open(path, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive)) {
		file.Write(sb.GetString(), static_cast<UINT>(sb.GetSize()));
		file.Close();
	}
}

bool CKHRadioHistory::ContainsAlbum(const CStringW& albumUrl) const
{
	for (const auto& album : m_albums) {
		if (album.url.CompareNoCase(albumUrl) == 0) {
			return true;
		}
	}
	return false;
}

void CKHRadioHistory::RecordTrack(const CStringW& albumUrl, const CStringW& albumTitle, const CStringW& trackName)
{
	if (albumUrl.IsEmpty()) {
		return;
	}

	size_t idx = m_albums.size();
	for (size_t i = 0; i < m_albums.size(); i++) {
		if (m_albums[i].url.CompareNoCase(albumUrl) == 0) {
			idx = i;
			break;
		}
	}

	if (idx == m_albums.size()) {
		AlbumEntry entry;
		entry.url = albumUrl;
		entry.title = albumTitle;
		m_albums.insert(m_albums.begin(), entry);
		idx = 0;
	} else if (idx > 0) {
		// most recently listened first
		AlbumEntry entry = m_albums[idx];
		m_albums.erase(m_albums.begin() + idx);
		m_albums.insert(m_albums.begin(), entry);
		idx = 0;
	}

	if (!albumTitle.IsEmpty()) {
		m_albums[idx].title = albumTitle;
	}

	if (!trackName.IsEmpty()) {
		bool found = false;
		for (auto& track : m_albums[idx].tracks) {
			if (track.name.CompareNoCase(trackName) == 0) {
				track.plays++;
				found = true;
				break;
			}
		}
		if (!found) {
			TrackEntry te;
			te.name = trackName;
			te.plays = 1;
			m_albums[idx].tracks.emplace_back(te);
		}
	}

	Save();
}
