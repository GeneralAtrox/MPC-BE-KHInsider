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
#include "DSUtil/HTTPAsync.h"
#include "DSUtil/text.h"
#include "KHInsider.h"

#include <map>
#include <regex>
#include <string>

#include "rapidjsonHelper.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"

#define KHINSIDER_HOST L"downloads.khinsider.com"
#define KHINSIDER_BASE L"https://downloads.khinsider.com"

namespace KHInsider
{
	using urlData = std::vector<char>;

	static bool URLGetData(LPCWSTR url, urlData& pData)
	{
		pData.clear();

		CHTTPAsync HTTPAsync;
		if (FAILED(HTTPAsync.Connect(url, http::connectTimeout))) {
			return false;
		}

		const auto contentLength = HTTPAsync.GetLenght();
		if (contentLength) {
			pData.resize(contentLength);
			DWORD dwSizeRead = 0;
			if (S_OK != HTTPAsync.Read((PBYTE)pData.data(), contentLength, dwSizeRead, http::readTimeout) || dwSizeRead != contentLength) {
				pData.clear();
			}
		} else {
			std::vector<char> tmp(16 * KILOBYTE);
			for (;;) {
				DWORD dwSizeRead = 0;
				if (S_OK != HTTPAsync.Read((PBYTE)tmp.data(), tmp.size(), dwSizeRead, http::readTimeout)) {
					break;
				}
				pData.insert(pData.end(), tmp.begin(), tmp.begin() + dwSizeRead);
			}
		}

		if (!pData.empty()) {
			pData.emplace_back('\0');
			return true;
		}

		return false;
	}

	// POST 'body' as application/x-www-form-urlencoded and read the final page.
	// WinInet follows the 302 redirect to the album page; 'finalUrl' receives the
	// URL we actually ended up on.
	static bool URLPostForm(LPCWSTR path, const CStringA& body, urlData& pData, CStringW& finalUrl)
	{
		pData.clear();
		finalUrl.Empty();

		if (auto hInet = InternetOpenW(http::userAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0)) {
			if (auto hSession = InternetConnectW(hInet, KHINSIDER_HOST, INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 1)) {
				if (auto hRequest = HttpOpenRequestW(hSession,
													 L"POST",
													 path, nullptr, nullptr, nullptr,
													 INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 1)) {

					LPCWSTR headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
					CStringA requestData(body);
					if (HttpSendRequestW(hRequest, headers, -1,
										 reinterpret_cast<LPVOID>(requestData.GetBuffer()), requestData.GetLength())) {

						std::vector<char> tmp(16 * KILOBYTE);
						for (;;) {
							DWORD dwSizeRead = 0;
							if (!InternetReadFile(hRequest, reinterpret_cast<LPVOID>(tmp.data()), tmp.size(), &dwSizeRead) || !dwSizeRead) {
								break;
							}
							pData.insert(pData.end(), tmp.begin(), tmp.begin() + dwSizeRead);
						}

						if (!pData.empty()) {
							pData.emplace_back('\0');

							WCHAR urlBuf[2048] = {};
							DWORD urlLen = std::size(urlBuf);
							if (InternetQueryOptionW(hRequest, INTERNET_OPTION_URL, urlBuf, &urlLen)) {
								finalUrl = urlBuf;
							}
						}
					}

					InternetCloseHandle(hRequest);
				}
				InternetCloseHandle(hSession);
			}
			InternetCloseHandle(hInet);
		}

		return !pData.empty();
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
		if (listStart == std::string::npos) {
			return false;
		}
		const size_t headerPos = html.find("id=\"songlist_header\"", listStart);
		if (headerPos != std::string::npos) {
			const size_t headerEnd = html.find("</tr>", headerPos);
			if (headerEnd != std::string::npos) {
				listStart = headerEnd;
			}
		}
		size_t listEnd = html.find("id=\"songlist_footer\"", listStart);
		if (listEnd == std::string::npos) {
			listEnd = html.find("</table>", listStart);
		}
		if (listEnd == std::string::npos) {
			listEnd = html.size();
		}
		const std::string songlist = html.substr(listStart, listEnd - listStart);

		static const std::regex reLink("<a\\s+href=\"(/game-soundtracks/album/[^\"]+)\"[^>]*>([^<]+)</a>", std::regex::icase);
		static const std::regex reDuration("^\\d+:\\d{2}(:\\d{2})?$");
		static const std::regex reSize("^[\\d.,]+\\s*[KMG]B$", std::regex::icase);

		std::map<std::string, bool> seen;

		for (auto it = std::sregex_iterator(songlist.begin(), songlist.end(), reLink); it != std::sregex_iterator(); ++it) {
			const std::string href = (*it)[1].str();
			std::string text = (*it)[2].str();

			CStringW name = Trimmed(DecodeHtmlEntities(UTF8ToWStr(text.c_str())));
			if (name.IsEmpty()) {
				continue;
			}
			const std::string nameUtf8(WStrToUTF8(name));
			if (std::regex_match(nameUtf8, reDuration) || std::regex_match(nameUtf8, reSize)) {
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

	bool FetchRandomAlbum(const CStringA& formBody, Album& album)
	{
		urlData data;
		CStringW finalUrl;
		if (!URLPostForm(L"/random-album-advanced", formBody, data, finalUrl)) {
			return false;
		}

		return ParseAlbumPage(data, finalUrl, album);
	}

	bool FetchAlbum(const CStringW& albumUrl, Album& album)
	{
		urlData data;
		if (!URLGetData(albumUrl, data)) {
			return false;
		}

		return ParseAlbumPage(data, albumUrl, album);
	}

	CStringW ResolveTrackAudioUrl(const CStringW& trackPageUrl)
	{
		urlData data;
		if (!URLGetData(trackPageUrl, data)) {
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

		return L"";
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
