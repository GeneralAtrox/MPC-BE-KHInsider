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
#include "MainFrm.h"
#include "PlayerKHRadioBar.h"
#include "FileItem.h"
#include "Misc.h"

// declared here instead of including coolscroll.h, which defines a global
// variable and may only be included from one translation unit
typedef COLORREF (*ptr_themeRGB)(const int, const int, const int);
#define CSBS_HOTTRACKED 2
extern "C" {
	BOOL WINAPI InitializeCoolSB(HWND hwnd, ptr_themeRGB ThemeRGB);
	BOOL WINAPI CoolSB_SetSize(HWND hwnd, int wBar, int nLength, int nWidth);
	BOOL WINAPI CoolSB_SetStyle(HWND hwnd, int wBar, UINT nStyle);
}

// option tables mirroring https://downloads.khinsider.com/random-album-advanced

struct KHOption {
	int value;
	LPCWSTR name;
};

static const KHOption s_khTypes[] = {
	{ 0, L"Unknown"      },
	{ 2, L"Gamerips"     },
	{ 1, L"Soundtracks"  },
	{ 3, L"Singles"      },
	{ 5, L"Arrangements" },
	{ 4, L"Remixes"      },
	{ 6, L"Compilations" },
	{ 7, L"Inspired By"  },
};

static const KHOption s_khPlatforms[] = {
	{  0, L"Unknown"              },
	{ 56, L"3DO"                  },
	{ 15, L"3DS"                  },
	{ 24, L"Amiga"                },
	{ 45, L"Android"              },
	{ 49, L"Anime"                },
	{ 23, L"Arcade"               },
	{ 25, L"Atari 8-Bit"          },
	{ 58, L"Atari Jaguar"         },
	{ 26, L"Atari ST"             },
	{ 28, L"CD-i"                 },
	{ 27, L"Commodore 64"         },
	{ 20, L"Dreamcast"            },
	{ 16, L"DS"                   },
	{ 60, L"Family Computer"      },
	{ 62, L"FDS"                  },
	{ 29, L"FM Towns"             },
	{ 59, L"Fujitsu FM77AV"       },
	{  6, L"Game Gear"            },
	{  3, L"GB"                   },
	{  4, L"GBA"                  },
	{ 19, L"GC"                   },
	{  8, L"Genesis / Mega Drive" },
	{ 63, L"IBM PC"               },
	{ 61, L"IBM PC/AT"            },
	{ 46, L"iOS"                  },
	{ 54, L"Linux"                },
	{ 53, L"MacOS"                },
	{  7, L"Master System"        },
	{ 41, L"Mobile"               },
	{ 50, L"Movie"                },
	{ 30, L"MS-DOS"               },
	{  5, L"MSX"                  },
	{ 31, L"MSX2"                 },
	{ 10, L"N64"                  },
	{ 52, L"Neo Geo"              },
	{  1, L"NES"                  },
	{ 51, L"Online"               },
	{ 32, L"PC-88"                },
	{ 33, L"PC-98"                },
	{ 34, L"PC-9821"              },
	{ 48, L"PC-FX"                },
	{ 42, L"PS Vita"              },
	{  9, L"PS1"                  },
	{ 11, L"PS2"                  },
	{ 12, L"PS3"                  },
	{ 44, L"PS4"                  },
	{ 57, L"PS5"                  },
	{ 13, L"PSP"                  },
	{ 21, L"Saturn"               },
	{ 35, L"Sharp X1"             },
	{  2, L"SNES"                 },
	{ 36, L"Spectrum"             },
	{ 65, L"Stadia"               },
	{ 69, L"Steam"                },
	{ 43, L"Switch"               },
	{ 68, L"Switch 2"             },
	{ 22, L"TurboGrafx-16"        },
	{ 55, L"Virtual Boy"          },
	{ 66, L"VR"                   },
	{ 14, L"Wii"                  },
	{ 39, L"Wii U"                },
	{ 37, L"Windows"              },
	{ 38, L"X68000"               },
	{ 17, L"Xbox"                 },
	{ 18, L"Xbox 360"             },
	{ 47, L"Xbox One"             },
	{ 64, L"Xbox Series X/S"      },
};

#define KHRADIO_YEAR_MAX 2026
#define KHRADIO_YEAR_MIN 1980

// total albums to keep in the playlist (the one playing + ones queued ahead)
#define KHRADIO_QUEUE_DEPTH 2

// "Filter non-music tracks" drops tracks shorter than this (seconds)
#define KHRADIO_MIN_TRACK_SEC 30

// worker -> UI message payloads

struct KHRadioTextMsg {
	UINT gen;
	CStringW text;
};

struct KHRadioDoneMsg {
	UINT gen;
	bool bAppend;
	int resolved;       // number of tracks queued by this fetch
	int skippedFiltered; // tracks dropped (spoken / short / silent)
	CStringW text;      // error/status to show (empty = success)
};

struct KHRadioAlbumMsg {
	UINT gen;
	bool bAppend;
	KHInsider::Album album;
};

struct KHRadioTrackMsg {
	UINT gen;
	bool bAppend;
	int index;
	CStringW name;
	CStringW audioUrl;
	CStringW coverPath;
};

// CKHRadioDlg

CKHRadioDlg::CKHRadioDlg()
	: CResizableDialog(CKHRadioDlg::IDD, nullptr)
{
}

CKHRadioDlg::~CKHRadioDlg()
{
}

BOOL CKHRadioDlg::Create(CWnd* pParent)
{
	if (!__super::Create(IDD, pParent)) {
		return FALSE;
	}

	m_pParent = pParent;

	return TRUE;
}

void CKHRadioDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_KHRADIO_TYPE_LIST, m_listType);
	DDX_Control(pDX, IDC_KHRADIO_YEAR_LIST, m_listYear);
	DDX_Control(pDX, IDC_KHRADIO_PLATFORM_LIST, m_listPlatform);
	DDX_Control(pDX, IDC_KHRADIO_CLEAR_BUTTON, m_buttonClear);
	DDX_Control(pDX, IDC_KHRADIO_AVOID_CHECK, m_checkAvoidPlayed);
	DDX_Control(pDX, IDC_KHRADIO_SPOKEN_CHECK, m_checkFilterSpoken);
	DDX_Control(pDX, IDC_KHRADIO_RANDOM_BUTTON, m_buttonRandom);
	DDX_Control(pDX, IDC_KHRADIO_STATUS, m_staticStatus);
	DDX_Control(pDX, IDC_KHRADIO_HISTORY_LIST, m_listHistory);
}

BEGIN_MESSAGE_MAP(CKHRadioDlg, CResizableDialog)
	ON_WM_DESTROY()
	ON_WM_CTLCOLOR()
	ON_WM_DRAWITEM()
	ON_LBN_SELCHANGE(IDC_KHRADIO_TYPE_LIST, OnSelChangeFilters)
	ON_LBN_SELCHANGE(IDC_KHRADIO_YEAR_LIST, OnSelChangeFilters)
	ON_LBN_SELCHANGE(IDC_KHRADIO_PLATFORM_LIST, OnSelChangeFilters)
	ON_BN_CLICKED(IDC_KHRADIO_CLEAR_BUTTON, OnClearFilters)
	ON_BN_CLICKED(IDC_KHRADIO_AVOID_CHECK, OnAvoidPlayedClicked)
	ON_BN_CLICKED(IDC_KHRADIO_SPOKEN_CHECK, OnFilterSpokenClicked)
	ON_BN_CLICKED(IDC_KHRADIO_RANDOM_BUTTON, OnRandomAlbum)
	ON_LBN_DBLCLK(IDC_KHRADIO_HISTORY_LIST, OnHistoryDblClk)
	ON_MESSAGE(WM_KHRADIO_STATUS, OnKHRadioStatus)
	ON_MESSAGE(WM_KHRADIO_ALBUM, OnKHRadioAlbum)
	ON_MESSAGE(WM_KHRADIO_TRACK, OnKHRadioTrack)
	ON_MESSAGE(WM_KHRADIO_DONE, OnKHRadioDone)
END_MESSAGE_MAP()

BOOL CKHRadioDlg::OnInitDialog()
{
	__super::OnInitDialog();

	PopulateFilterLists();
	RestoreSelections();

	m_history.Load();
	RefreshHistoryList();

	// nothing is playing yet, so any cover images left from a previous
	// (possibly crashed) session are stale - delete them.
	CStringW coverDir;
	if (AfxGetMyApp()->GetAppSavePath(coverDir)) {
		WIN32_FIND_DATAW wfd;
		HANDLE hFind = FindFirstFileW(coverDir + L"khradio_cover_*.img", &wfd);
		if (hFind != INVALID_HANDLE_VALUE) {
			do {
				::DeleteFileW(coverDir + wfd.cFileName);
			} while (FindNextFileW(hFind, &wfd));
			FindClose(hFind);
		}
	}

	AddAnchor(IDC_KHRADIO_TYPE_LABEL, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_TYPE_LIST, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_YEAR_LABEL, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_YEAR_LIST, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_PLATFORM_LABEL, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_PLATFORM_LIST, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_CLEAR_BUTTON, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_AVOID_CHECK, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_SPOKEN_CHECK, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_RANDOM_BUTTON, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_STATUS, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_HISTORY_LABEL, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_HISTORY_LIST, TOP_LEFT, BOTTOM_RIGHT);

	// match the player's dark theme, using the same palette as the playlist bar
	m_bDarkTheme = !!AfxGetAppSettings().bUseDarkTheme;
	if (m_bDarkTheme) {
		m_crText = ThemeRGB(165, 170, 175);
		m_crListText = ThemeRGB(135, 140, 145);
		m_crListBk = ThemeRGB(10, 15, 20);
		m_brushWindow.CreateSolidBrush(ThemeRGB(45, 50, 55));
		m_brushList.CreateSolidBrush(m_crListBk);

		auto pFrame = AfxGetMainFrame();
		for (CListBox* pList : { &m_listType, &m_listYear, &m_listPlatform, &m_listHistory }) {
			// drop the light 3D edge for a flat dark look
			pList->ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
			pList->ModifyStyle(WS_BORDER, 0);
			pList->SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
			InitializeCoolSB(pList->m_hWnd, ThemeRGB);
			// flat/hot-tracked style is what makes CoolSB draw the dark themed
			// arrows; without it the up/down buttons fall back to light Windows
			// rendering
			CoolSB_SetStyle(pList->m_hWnd, SB_VERT, CSBS_HOTTRACKED);
			if (pFrame && SysVersion::IsWin8orLater()) {
				CoolSB_SetSize(pList->m_hWnd, SB_VERT, pFrame->GetSystemMetricsDPI(SM_CYVSCROLL), pFrame->GetSystemMetricsDPI(SM_CXVSCROLL));
			}
		}

		m_buttonRandom.SetButtonStyle(BS_OWNERDRAW);
		m_buttonClear.SetButtonStyle(BS_OWNERDRAW);
	}

	SetStatus(L"Ready. Pick your filters and roll an album.");

	return TRUE;
}

HBRUSH CKHRadioDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (m_bDarkTheme) {
		switch (nCtlColor) {
			case CTLCOLOR_DLG:
			case CTLCOLOR_BTN:
				return m_brushWindow;
			case CTLCOLOR_STATIC:
				pDC->SetTextColor(m_crText);
				pDC->SetBkMode(TRANSPARENT);
				return m_brushWindow;
			case CTLCOLOR_LISTBOX:
				pDC->SetTextColor(m_crListText);
				pDC->SetBkColor(m_crListBk);
				return m_brushList;
		}
	}

	return __super::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CKHRadioDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (m_bDarkTheme && (nIDCtl == IDC_KHRADIO_RANDOM_BUTTON || nIDCtl == IDC_KHRADIO_CLEAR_BUTTON)) {
		CDC dc;
		dc.Attach(lpDrawItemStruct->hDC);

		CRect r(lpDrawItemStruct->rcItem);
		const bool bPressed = !!(lpDrawItemStruct->itemState & ODS_SELECTED);
		const bool bDisabled = !!(lpDrawItemStruct->itemState & ODS_DISABLED);

		dc.FillSolidRect(r, bPressed ? ThemeRGB(30, 35, 40) : ThemeRGB(60, 65, 70));
		dc.Draw3dRect(r, ThemeRGB(85, 90, 95), ThemeRGB(25, 30, 35));

		CStringW text;
		if (CWnd* pBtn = GetDlgItem(nIDCtl)) {
			pBtn->GetWindowTextW(text);
		}
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(bDisabled ? ThemeRGB(95, 100, 105) : m_crText);
		CFont* pOldFont = dc.SelectObject(GetFont());
		dc.DrawTextW(text, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		dc.SelectObject(pOldFont);

		if (lpDrawItemStruct->itemState & ODS_FOCUS) {
			CRect rf(r);
			rf.DeflateRect(2, 2);
			dc.DrawFocusRect(rf);
		}

		dc.Detach();
		return;
	}

	__super::OnDrawItem(nIDCtl, lpDrawItemStruct);
}

void CKHRadioDlg::OnClearFilters()
{
	m_listType.SetSel(-1, FALSE);
	m_listYear.SetSel(-1, FALSE);
	m_listPlatform.SetSel(-1, FALSE);
	SaveSelections();
}

void CKHRadioDlg::OnDestroy()
{
	(*m_pGen)++; // orphan any running fetch thread
	SaveSelections();

	__super::OnDestroy();
}

void CKHRadioDlg::PopulateFilterLists()
{
	for (const auto& opt : s_khTypes) {
		const int idx = m_listType.AddString(opt.name);
		m_listType.SetItemData(idx, opt.value);
	}

	int idx = m_listYear.AddString(L"Unknown");
	m_listYear.SetItemData(idx, 0);
	for (int year = KHRADIO_YEAR_MAX; year >= KHRADIO_YEAR_MIN; year--) {
		CStringW str;
		str.Format(L"%d", year);
		idx = m_listYear.AddString(str);
		m_listYear.SetItemData(idx, year);
	}

	for (const auto& opt : s_khPlatforms) {
		const int i = m_listPlatform.AddString(opt.name);
		m_listPlatform.SetItemData(i, opt.value);
	}
}

CStringW CKHRadioDlg::JoinSelection(CListBox& list)
{
	CStringW csv;
	for (int i = 0; i < list.GetCount(); i++) {
		if (list.GetSel(i) > 0) {
			if (!csv.IsEmpty()) {
				csv += L",";
			}
			CStringW val;
			val.Format(L"%d", static_cast<int>(list.GetItemData(i)));
			csv += val;
		}
	}
	return csv;
}

void CKHRadioDlg::ApplySelection(CListBox& list, const CStringW& csv)
{
	if (csv.IsEmpty()) {
		return;
	}

	std::vector<int> values;
	int pos = 0;
	CStringW token = csv.Tokenize(L",", pos);
	while (!token.IsEmpty()) {
		values.emplace_back(_wtoi(token));
		token = csv.Tokenize(L",", pos);
	}

	for (int i = 0; i < list.GetCount(); i++) {
		const int data = static_cast<int>(list.GetItemData(i));
		for (const auto v : values) {
			if (v == data) {
				list.SetSel(i, TRUE);
				break;
			}
		}
	}
}

void CKHRadioDlg::GetSelectedValues(CListBox& list, std::vector<int>& values)
{
	values.clear();
	for (int i = 0; i < list.GetCount(); i++) {
		if (list.GetSel(i) > 0) {
			values.emplace_back(static_cast<int>(list.GetItemData(i)));
		}
	}
}

void CKHRadioDlg::RestoreSelections()
{
	const CAppSettings& s = AfxGetAppSettings();

	ApplySelection(m_listType, s.strKHRadioTypes);
	ApplySelection(m_listYear, s.strKHRadioYears);
	ApplySelection(m_listPlatform, s.strKHRadioPlatforms);
	m_checkAvoidPlayed.SetCheck(s.bKHRadioAvoidPlayed ? BST_CHECKED : BST_UNCHECKED);
	m_checkFilterSpoken.SetCheck(s.bKHRadioFilterSpoken ? BST_CHECKED : BST_UNCHECKED);

	const auto selCount = [](CListBox& l) {
		int n = 0;
		for (int i = 0; i < l.GetCount(); i++) if (l.GetSel(i) > 0) n++;
		return n;
	};
	CStringW log;
	log.Format(L"restore: types='%s'(%d sel) years='%s'(%d sel) platforms='%s'(%d sel)",
			   s.strKHRadioTypes.GetString(), selCount(m_listType),
			   s.strKHRadioYears.GetString(), selCount(m_listYear),
			   s.strKHRadioPlatforms.GetString(), selCount(m_listPlatform));
	KHInsider::DebugLog(log);
}

void CKHRadioDlg::SaveSelections()
{
	CAppSettings& s = AfxGetAppSettings();

	s.strKHRadioTypes = JoinSelection(m_listType);
	s.strKHRadioYears = JoinSelection(m_listYear);
	s.strKHRadioPlatforms = JoinSelection(m_listPlatform);
	s.bKHRadioAvoidPlayed = (m_checkAvoidPlayed.GetCheck() == BST_CHECKED);
	s.bKHRadioFilterSpoken = (m_checkFilterSpoken.GetCheck() == BST_CHECKED);
}

void CKHRadioDlg::OnSelChangeFilters()
{
	SaveSelections();
}

void CKHRadioDlg::OnAvoidPlayedClicked()
{
	SaveSelections();
}

void CKHRadioDlg::OnFilterSpokenClicked()
{
	SaveSelections();
}

void CKHRadioDlg::SetStatus(const CStringW& text)
{
	if (::IsWindow(m_staticStatus.m_hWnd)) {
		m_staticStatus.SetWindowTextW(text);
	}
}

void CKHRadioDlg::RefreshHistoryList()
{
	if (!::IsWindow(m_listHistory.m_hWnd)) {
		return;
	}

	m_listHistory.ResetContent();
	for (const auto& album : m_history.Albums()) {
		CStringW str = album.title;
		if (str.IsEmpty()) {
			str = album.url;
		}
		str.AppendFormat(L"  (%u)", static_cast<unsigned>(album.tracks.size()));
		m_listHistory.AddString(str);
	}
}

void CKHRadioDlg::OnRandomAlbum()
{
	SaveSelections();
	StartFetch(false, L"");
}

void CKHRadioDlg::OnHistoryDblClk()
{
	const int sel = m_listHistory.GetCurSel();
	if (sel == LB_ERR || sel >= static_cast<int>(m_history.Albums().size())) {
		return;
	}

	StartFetch(true, m_history.Albums()[sel].url);
}

void CKHRadioDlg::StartFetch(bool bDirect, const CStringW& albumUrl, bool bAppend)
{
	auto p = std::make_unique<FetchParams>();
	p->hWnd = m_hWnd;
	p->gen = ++(*m_pGen);
	p->pGen = m_pGen;
	p->bDirect = bDirect;
	p->bAppend = bAppend;
	p->albumUrl = albumUrl;
	p->bAvoidPlayed = (m_checkAvoidPlayed.GetCheck() == BST_CHECKED);
	p->bFilterSpoken = (m_checkFilterSpoken.GetCheck() == BST_CHECKED);

	if (!bDirect) {
		std::vector<int> types, years, platforms;
		GetSelectedValues(m_listType, types);
		GetSelectedValues(m_listYear, years);
		GetSelectedValues(m_listPlatform, platforms);
		p->formBody = KHInsider::BuildRandomAlbumForm(types, years, platforms);

		for (const auto& album : m_history.Albums()) {
			p->playedAlbums.emplace_back(album.url);
		}
	}

	if (!bAppend) {
		// a replace fetch empties the playlist, so the queue restarts
		m_queuedAlbumCount = 0;
		m_currentPlayingAlbum.Empty();
	}

	m_bFetching = true;
	m_bPlaylistStarted = false;
	SetStatus(bAppend ? L"Queuing the next album..." : (bDirect ? L"Loading album..." : L"Rolling a random album..."));

	if (AfxBeginThread(FetchThreadProc, p.get())) {
		p.release(); // owned by the thread now
	} else {
		m_bFetching = false;
		SetStatus(L"Could not start the fetch thread.");
	}
}

void CKHRadioDlg::MaybePrefetchNext()
{
	// keep KHRADIO_QUEUE_DEPTH albums in the playlist so the next one is ready
	// well before the current album ends. Only one fetch runs at a time.
	const bool go = m_bRadioActive && !m_bFetching && m_queuedAlbumCount < KHRADIO_QUEUE_DEPTH;
	CStringW lg;
	lg.Format(L"prefetch-check: active=%d fetching=%d queued=%d -> %s",
			  m_bRadioActive ? 1 : 0, m_bFetching ? 1 : 0, m_queuedAlbumCount, go ? L"FETCH" : L"skip");
	KHInsider::DebugLog(lg);
	if (go) {
		StartFetch(false, L"", true);
	}
}

UINT CKHRadioDlg::FetchThreadProc(LPVOID pParam)
{
	std::unique_ptr<FetchParams> p(static_cast<FetchParams*>(pParam));

	const auto cancelled = [&]() {
		return (*p->pGen) != p->gen;
	};
	const auto postText = [&](UINT msg, const CStringW& text) {
		auto* m = new KHRadioTextMsg{ p->gen, text };
		if (!::PostMessageW(p->hWnd, msg, 0, reinterpret_cast<LPARAM>(m))) {
			delete m;
		}
	};

	constexpr int maxAttempts = 8;
	const int attempts = p->bDirect ? 1 : maxAttempts;

	int resolved = 0;
	int skippedFiltered = 0;
	bool fetchOk = false;
	KHInsider::FetchStatus fetchStatus = KHInsider::FetchStatus::NetworkError;

	for (int attempt = 1; attempt <= attempts && !cancelled(); attempt++) {
		KHInsider::Album album;
		const bool gotAlbum = p->bDirect
			? KHInsider::FetchAlbum(p->albumUrl, album, &fetchStatus)
			: KHInsider::FetchRandomAlbum(p->formBody, album, &fetchStatus);
		if (!gotAlbum) {
			break; // network/parse failure; fetchStatus has the reason
		}
		fetchOk = true;

		// skip already-heard albums (random mode only)
		if (!p->bDirect && p->bAvoidPlayed && attempt < attempts) {
			bool bPlayed = false;
			for (const auto& url : p->playedAlbums) {
				if (url.CompareNoCase(album.url) == 0) {
					bPlayed = true;
					break;
				}
			}
			if (bPlayed) {
				CStringW status;
				status.Format(L"Already heard '%s' - rerolling...", album.title.GetString());
				postText(WM_KHRADIO_STATUS, status);
				continue;
			}
		}

		if (album.tracks.empty()) {
			if (!p->bDirect && attempt < attempts) {
				continue;
			}
			break;
		}

		// download the album cover so it can replace the audio logo on playback
		CStringW coverPath;
		if (!album.coverUrl.IsEmpty()) {
			CStringW dir;
			if (AfxGetMyApp()->GetAppSavePath(dir)) {
				CStringW candidate;
				candidate.Format(L"%skhradio_cover_%u.img", dir.GetString(), p->gen);
				if (KHInsider::DownloadToFile(album.coverUrl, candidate)) {
					coverPath = candidate;
				}
			}
		}

		if (p->bFilterSpoken) {
			CStringW status;
			status.Format(L"Checking '%s' for non-music tracks...", album.title.GetString());
			postText(WM_KHRADIO_STATUS, status);
		}

		// resolve tracks; the album message is posted lazily on the first
		// playable track so a fully-filtered album can be rerolled cleanly
		bool albumPosted = false;
		skippedFiltered = 0; // count for this album only (rerolls reset it)
		for (size_t i = 0; i < album.tracks.size() && !cancelled(); i++) {
			// cheap filter first: drop very short tracks using the page's
			// duration, before spending a request resolving the audio
			if (p->bFilterSpoken && album.tracks[i].durationSec > 0
					&& album.tracks[i].durationSec < KHRADIO_MIN_TRACK_SEC) {
				skippedFiltered++;
				KHInsider::DebugLog(L"skip short track '" + album.tracks[i].name + L"'");
				continue;
			}

			const CStringW audioUrl = KHInsider::ResolveTrackAudioUrl(album.tracks[i].pageUrl);
			if (audioUrl.IsEmpty()) {
				continue;
			}
			if (p->bFilterSpoken) {
				const auto verdict = KHInsider::ClassifyTrackAudio(audioUrl, album.tracks[i].name);
				if (verdict != KHInsider::AudioVerdict::Music) {
					skippedFiltered++;
					continue;
				}
			}

			if (!albumPosted) {
				auto* am = new KHRadioAlbumMsg{ p->gen, p->bAppend, album };
				if (!::PostMessageW(p->hWnd, WM_KHRADIO_ALBUM, 0, reinterpret_cast<LPARAM>(am))) {
					delete am;
					return 0;
				}
				albumPosted = true;
			}

			auto* m = new KHRadioTrackMsg{ p->gen, p->bAppend, static_cast<int>(i), album.tracks[i].name, audioUrl, coverPath };
			if (!::PostMessageW(p->hWnd, WM_KHRADIO_TRACK, 0, reinterpret_cast<LPARAM>(m))) {
				delete m;
				return 0;
			}
			resolved++;
		}

		if (cancelled()) {
			return 0;
		}

		if (resolved > 0) {
			break; // got a playable album
		}

		// nothing playable (all spoken or unresolved) - reroll if random
		if (!coverPath.IsEmpty()) {
			::DeleteFileW(coverPath);
		}
		if (!p->bDirect && attempt < attempts) {
			postText(WM_KHRADIO_STATUS, L"Only spoken tracks here - rerolling...");
			continue;
		}
		break;
	}

	if (cancelled()) {
		return 0;
	}

	// build a diagnostic message so site/network breakage is self-explanatory
	CStringW doneText; // empty == success (handler shows "Playing/Queued")
	if (!fetchOk) {
		switch (fetchStatus) {
			case KHInsider::FetchStatus::Blocked:
				doneText = L"KHInsider blocked the request (Cloudflare). Its bot protection may have changed.";
				break;
			case KHInsider::FetchStatus::LayoutChanged:
				doneText = L"Couldn't read the album page - the site layout may have changed.";
				break;
			default:
				doneText = L"Network error - check your connection.";
				break;
		}
	} else if (resolved == 0) {
		doneText = skippedFiltered > 0
			? CStringW(L"Every track was filtered out - try unchecking 'Filter non-music tracks'.")
			: CStringW(L"No playable tracks found - try different filters.");
	}

	auto* dm = new KHRadioDoneMsg{ p->gen, p->bAppend, resolved, skippedFiltered, doneText };
	if (!::PostMessageW(p->hWnd, WM_KHRADIO_DONE, 0, reinterpret_cast<LPARAM>(dm))) {
		delete dm;
	}

	return 0;
}

LRESULT CKHRadioDlg::OnKHRadioStatus(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<KHRadioTextMsg> m(reinterpret_cast<KHRadioTextMsg*>(lParam));
	if (m && m->gen == (*m_pGen)) {
		SetStatus(m->text);
	}
	return 0;
}

LRESULT CKHRadioDlg::OnKHRadioAlbum(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<KHRadioAlbumMsg> m(reinterpret_cast<KHRadioAlbumMsg*>(lParam));
	if (!m || m->gen != (*m_pGen)) {
		return 0;
	}

	m_currentAlbum = m->album;
	if (!m->bAppend) {
		// fresh session: earlier albums leave the playlist, so forget their tracks
		m_audioUrlToTrack.clear();
	}
	m_bPlaylistStarted = false;

	CStringW status;
	status.Format(m->bAppend ? L"Up next: %s (%u tracks)" : L"Found: %s (%u tracks) - starting...",
				  m_currentAlbum.title.GetString(), static_cast<unsigned>(m_currentAlbum.tracks.size()));
	SetStatus(status);

	return 0;
}

LRESULT CKHRadioDlg::OnKHRadioTrack(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<KHRadioTrackMsg> m(reinterpret_cast<KHRadioTrackMsg*>(lParam));
	if (!m || m->gen != (*m_pGen)) {
		return 0;
	}

	m_audioUrlToTrack[m->audioUrl] = { m->name, m_currentAlbum.url, m_currentAlbum.title, m->coverPath };

	CStringW label;
	label.Format(L"%02d. %s", m->index + 1, m->name.GetString());

	CFileItemList fis;
	fis.emplace_back(CFileItem(m->audioUrl, label));

	auto pFrame = AfxGetMainFrame();
	if (!pFrame) {
		return 0;
	}

	if (!m_bPlaylistStarted) {
		// Opening media closes the current item and waits on the graph thread.
		// Doing that while a track is still mid-open (MLS_LOADING) deadlocks the
		// UI thread against the graph thread, so never open in that state.
		const auto loadState = pFrame->GetLoadState();

		if (m->bAppend) {
			pFrame->m_wndPlaylistBar.Append(fis);

			// Jump-start only if playback genuinely ran dry: stopped AND nothing
			// is currently loading. While a track is loading it WILL start
			// playing, so there's nothing to jump-start.
			if (loadState != MLS_LOADING) {
				const OAFilterState fs = pFrame->GetMediaState();
				if (fs != State_Running && fs != State_Paused) {
					if (pFrame->m_wndPlaylistBar.SelectFileInPlaylist(m->audioUrl)) {
						pFrame->OpenCurPlaylistItem();
					}
				}
			}
			m_bPlaylistStarted = true;
		} else {
			// Replace (user rolled a new album): must switch now. If a track is
			// mid-open, requeue this message and retry once the load settles -
			// closing a loading item would deadlock.
			if (loadState == MLS_LOADING) {
				::PostMessageW(m_hWnd, WM_KHRADIO_TRACK, 0, reinterpret_cast<LPARAM>(m.release()));
				return 0;
			}

			pFrame->m_wndPlaylistBar.Empty();
			pFrame->m_wndPlaylistBar.Append(fis);
			pFrame->OpenCurPlaylistItem();
			m_bPlaylistStarted = true;

			CStringW status;
			status.Format(L"Playing: %s", m_currentAlbum.title.GetString());
			SetStatus(status);
		}
	} else {
		pFrame->m_wndPlaylistBar.Append(fis);
	}

	return 0;
}

LRESULT CKHRadioDlg::OnKHRadioDone(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<KHRadioDoneMsg> m(reinterpret_cast<KHRadioDoneMsg*>(lParam));
	if (!m || m->gen != (*m_pGen)) {
		return 0;
	}

	m_bFetching = false;

	if (m->resolved > 0) {
		if (m->bAppend) {
			// another album was appended to the queue
			m_queuedAlbumCount++;
		} else {
			// a replace fetch: this album is now the (only) one queued
			m_bRadioActive = true;
			m_queuedAlbumCount = 1;
		}
		CStringW lg;
		lg.Format(L"done: append=%d resolved=%d -> queued=%d active=%d",
				  m->bAppend ? 1 : 0, m->resolved, m_queuedAlbumCount, m_bRadioActive ? 1 : 0);
		KHInsider::DebugLog(lg);
		// top the queue up if we're still short
		MaybePrefetchNext();
	} else {
		KHInsider::DebugLog(L"done: fetch produced no tracks (resolved=0)");
	}

	if (!m->text.IsEmpty()) {
		SetStatus(m->text);
	} else if (!m_currentAlbum.title.IsEmpty()) {
		CStringW status;
		status.Format(m->bAppend ? L"Queued next: %s" : L"Playing: %s", m_currentAlbum.title.GetString());
		if (m->skippedFiltered > 0) {
			status.AppendFormat(L"  (skipped %d non-music)", m->skippedFiltered);
		}
		SetStatus(status);
	}

	return 0;
}

void CKHRadioDlg::OnPlaybackStarted(const CStringW& path)
{
	const auto it = m_audioUrlToTrack.find(path);
	if (it == m_audioUrlToTrack.end()) {
		return;
	}

	const TrackRef track = it->second;
	m_history.RecordTrack(track.albumUrl, track.albumTitle, track.name);
	RefreshHistoryList();

	CStringW status;
	status.Format(L"Now playing: %s - %s", track.albumTitle.GetString(), track.name.GetString());
	SetStatus(status);

	auto pFrame = AfxGetMainFrame();

	if (track.albumUrl != m_currentPlayingAlbum) {
		// a new album just started playing
		const bool bFirstAlbum = m_currentPlayingAlbum.IsEmpty();
		m_currentPlayingAlbum = track.albumUrl;

		// show this album's cover in place of the audio logo. The frame keeps
		// the file and loads it from SetAudioPicture(), so we don't delete it.
		if (pFrame && !track.coverPath.IsEmpty()) {
			pFrame->SetRadioAlbumCover(track.coverPath);
		}

		// continuous radio: a queued album has now started, so one fewer is
		// waiting ahead - prefetch another to keep the queue full.
		if (!bFirstAlbum && m_queuedAlbumCount > 0) {
			m_queuedAlbumCount--;
		}
		CStringW lg;
		lg.Format(L"radio: now playing album '%s' (first=%d), queued=%d", track.albumTitle.GetString(), bFirstAlbum ? 1 : 0, m_queuedAlbumCount);
		KHInsider::DebugLog(lg);
		MaybePrefetchNext();
	}
}

bool CKHRadioDlg::ContinueRadioAtEnd()
{
	if (!m_bRadioActive) {
		return false; // not a radio session - let the player stop normally
	}

	// The playlist ran dry (the prefetched album wasn't ready in time, or a
	// fetch failed). Make sure a fetch is running; its tracks will jump-start
	// playback on arrival. Either way, tell the frame not to stop.
	KHInsider::DebugLog(L"radio: playlist reached end while active - continuing");
	if (!m_bFetching) {
		m_queuedAlbumCount = 0;
		m_currentPlayingAlbum.Empty();
		StartFetch(false, L"", false); // fresh album, plays when it arrives
	}
	return true;
}

// CKHRadioBar

IMPLEMENT_DYNAMIC(CKHRadioBar, CPlayerBar)

CKHRadioBar::CKHRadioBar()
{
	// draw the bar's frame/gripper with the dark palette instead of the
	// light system colours (otherwise the dock shows a white border)
	m_bUseDarkTheme = !!AfxGetAppSettings().bUseDarkTheme;
}

COLORREF CKHRadioBar::ColorThemeRGB(const int iR, const int iG, const int iB) const
{
	return ThemeRGB(iR, iG, iB);
}

CKHRadioBar::~CKHRadioBar()
{
}

BOOL CKHRadioBar::Create(CWnd* pParentWnd, UINT defDockBarID)
{
	if (!CPlayerBar::Create(ResStr(IDS_KHRADIO_BAR), pParentWnd, ID_VIEW_KHRADIO, defDockBarID, L"KHRadioBar")) {
		return FALSE;
	}

	m_pParent = pParentWnd;
	m_dlg.Create(this);
	m_dlg.ShowWindow(SW_SHOWNORMAL);

	CRect r;
	m_dlg.GetWindowRect(r);
	m_szMinVert = m_szVert = r.Size();
	m_szMinHorz = m_szHorz = r.Size();
	m_szMinFloat = m_szFloat = r.Size();

	return TRUE;
}

void CKHRadioBar::ReloadTranslatableResources()
{
	SetWindowText(ResStr(IDS_KHRADIO_BAR));
}

BOOL CKHRadioBar::PreTranslateMessage(MSG* pMsg)
{
	if (IsWindow(pMsg->hwnd) && IsVisible() && pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST) {
		if (IsDialogMessageW(pMsg)) {
			return TRUE;
		}
	}

	return __super::PreTranslateMessage(pMsg);
}

BEGIN_MESSAGE_MAP(CKHRadioBar, CPlayerBar)
	ON_WM_SIZE()
	ON_WM_NCLBUTTONUP()
END_MESSAGE_MAP()

void CKHRadioBar::OnSize(UINT nType, int cx, int cy)
{
	__super::OnSize(nType, cx, cy);

	if (::IsWindow(m_dlg.m_hWnd)) {
		CRect r;
		GetClientRect(r);
		m_dlg.MoveWindow(r);
	}
}

void CKHRadioBar::OnNcLButtonUp(UINT nHitTest, CPoint point)
{
	__super::OnNcLButtonUp(nHitTest, point);

	if (nHitTest == HTCLOSE) {
		AfxGetAppSettings().bShowKHRadioBar = false;
	}
}
