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

#pragma once

#include <vector>

// Scraper for downloads.khinsider.com used by the KH Radio bar.
namespace KHInsider
{
	struct Track {
		CStringW name;        // display name
		CStringW pageUrl;     // track page on downloads.khinsider.com
		int      durationSec = 0; // length from the album page (0 if unknown)
	};

	// Result of analysing a track's audio for the "skip non-music" filter.
	enum class AudioVerdict {
		Music,   // keep it
		Spoken,  // dialogue/drama/narration
		Quiet,   // (near-)silent / empty
	};

	struct Album {
		CStringW url;       // album page URL
		CStringW title;
		std::vector<CStringW> coverUrls; // cover candidates, best/smallest first (thumbnails, then full-size as fallback)
		CStringW platforms; // comma-separated console list ("Saturn, Arcade"), if any
		std::vector<Track> tracks;
	};

	// Why a fetch failed, so the UI can show a useful message and we can tell
	// "site is down / blocking us" apart from "site changed its HTML".
	enum class FetchStatus {
		Success,
		NetworkError,  // no usable response (offline, timeout, DNS)
		Blocked,       // HTTP 4xx - Cloudflare / bot protection likely changed
		LayoutChanged, // page fetched but no tracks could be parsed
	};

	// POST body for the random-album-advanced form. Each list holds the numeric
	// option values of the site's form ("type[]", "year[]", "category[]").
	CStringA BuildRandomAlbumForm(const std::vector<int>& types, const std::vector<int>& years, const std::vector<int>& platforms);

	// POST the random-album-advanced form; fills 'album' with title and track pages.
	bool FetchRandomAlbum(const CStringA& formBody, Album& album, FetchStatus* pStatus = nullptr);

	// GET an album page directly (used when replaying an album from history).
	bool FetchAlbum(const CStringW& albumUrl, Album& album, FetchStatus* pStatus = nullptr);

	// GET a track page and extract the direct audio URL (mp3 preferred).
	// Returns an empty string on failure.
	CStringW ResolveTrackAudioUrl(const CStringW& trackPageUrl);

	// Download a binary resource (e.g. cover image) to a local file.
	bool DownloadToFile(const CStringW& url, const CStringW& localPath);

	// Download a prefix of the track and classify it as music, spoken, or
	// (near-)silent, for the "skip non-music tracks" filter.
	AudioVerdict ClassifyTrackAudio(const CStringW& audioUrl, const CStringW& trackName);

	// True if a title (a track name or a whole album title) contains drama/voice
	// keywords - lets the radio skip drama-CD / voice albums by title.
	bool IsSpokenTitle(const CStringW& title);

	CStringW DecodeHtmlEntities(CStringW str);

	// Append a line to %APPDATA%\MPC-BE\khradio_debug.log.
	void DebugLog(const CStringW& msg);
}

// Persistent record of every album/track that has been listened to.
// Stored as khradio_history.json next to the player settings.
class CKHRadioHistory
{
public:
	struct TrackEntry {
		CStringW name;
		int plays = 0;
	};

	struct AlbumEntry {
		CStringW url;
		CStringW title;
		std::vector<TrackEntry> tracks;
	};

	void Load();
	void Save() const;

	bool ContainsAlbum(const CStringW& albumUrl) const;
	void RecordTrack(const CStringW& albumUrl, const CStringW& albumTitle, const CStringW& trackName);

	// most recently listened first
	const std::vector<AlbumEntry>& Albums() const { return m_albums; }

private:
	CStringW GetPath() const;

	std::vector<AlbumEntry> m_albums;
};
