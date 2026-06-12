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
extern "C" BOOL WINAPI InitializeCoolSB(HWND hwnd, ptr_themeRGB ThemeRGB);

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

// worker -> UI message payloads

struct KHRadioTextMsg {
	UINT gen;
	CStringW text;
};

struct KHRadioAlbumMsg {
	UINT gen;
	KHInsider::Album album;
};

struct KHRadioTrackMsg {
	UINT gen;
	int index;
	CStringW name;
	CStringW audioUrl;
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
	DDX_Control(pDX, IDC_KHRADIO_RANDOM_BUTTON, m_buttonRandom);
	DDX_Control(pDX, IDC_KHRADIO_STATUS, m_staticStatus);
	DDX_Control(pDX, IDC_KHRADIO_HISTORY_LIST, m_listHistory);
}

BEGIN_MESSAGE_MAP(CKHRadioDlg, CResizableDialog)
	ON_WM_DESTROY()
	ON_WM_CTLCOLOR()
	ON_LBN_SELCHANGE(IDC_KHRADIO_TYPE_LIST, OnSelChangeFilters)
	ON_LBN_SELCHANGE(IDC_KHRADIO_YEAR_LIST, OnSelChangeFilters)
	ON_LBN_SELCHANGE(IDC_KHRADIO_PLATFORM_LIST, OnSelChangeFilters)
	ON_BN_CLICKED(IDC_KHRADIO_CLEAR_BUTTON, OnClearFilters)
	ON_BN_CLICKED(IDC_KHRADIO_AVOID_CHECK, OnAvoidPlayedClicked)
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

	AddAnchor(IDC_KHRADIO_TYPE_LABEL, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_TYPE_LIST, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_YEAR_LABEL, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_YEAR_LIST, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_PLATFORM_LABEL, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_PLATFORM_LIST, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_CLEAR_BUTTON, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_KHRADIO_AVOID_CHECK, TOP_LEFT, TOP_RIGHT);
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

		for (CListBox* pList : { &m_listType, &m_listYear, &m_listPlatform, &m_listHistory }) {
			InitializeCoolSB(pList->m_hWnd, ThemeRGB);
		}
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
}

void CKHRadioDlg::SaveSelections()
{
	CAppSettings& s = AfxGetAppSettings();

	s.strKHRadioTypes = JoinSelection(m_listType);
	s.strKHRadioYears = JoinSelection(m_listYear);
	s.strKHRadioPlatforms = JoinSelection(m_listPlatform);
	s.bKHRadioAvoidPlayed = (m_checkAvoidPlayed.GetCheck() == BST_CHECKED);
}

void CKHRadioDlg::OnSelChangeFilters()
{
	SaveSelections();
}

void CKHRadioDlg::OnAvoidPlayedClicked()
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

void CKHRadioDlg::StartFetch(bool bDirect, const CStringW& albumUrl)
{
	auto p = std::make_unique<FetchParams>();
	p->hWnd = m_hWnd;
	p->gen = ++(*m_pGen);
	p->pGen = m_pGen;
	p->bDirect = bDirect;
	p->albumUrl = albumUrl;
	p->bAvoidPlayed = (m_checkAvoidPlayed.GetCheck() == BST_CHECKED);

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

	m_bFetching = true;
	m_bPlaylistStarted = false;
	SetStatus(bDirect ? L"Loading album..." : L"Rolling a random album...");

	if (AfxBeginThread(FetchThreadProc, p.get())) {
		p.release(); // owned by the thread now
	} else {
		m_bFetching = false;
		SetStatus(L"Could not start the fetch thread.");
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

	KHInsider::Album album;
	bool ok = false;

	if (p->bDirect) {
		ok = KHInsider::FetchAlbum(p->albumUrl, album);
	} else {
		constexpr int maxAttempts = 8;
		for (int attempt = 1; attempt <= maxAttempts && !cancelled(); attempt++) {
			if (!KHInsider::FetchRandomAlbum(p->formBody, album)) {
				ok = false;
				break;
			}
			ok = true;

			if (p->bAvoidPlayed && attempt < maxAttempts) {
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
			break;
		}
	}

	if (cancelled()) {
		return 0;
	}

	if (!ok || album.tracks.empty()) {
		postText(WM_KHRADIO_DONE, L"Could not fetch an album. Check your connection and filters.");
		return 0;
	}

	{
		auto* m = new KHRadioAlbumMsg{ p->gen, album };
		if (!::PostMessageW(p->hWnd, WM_KHRADIO_ALBUM, 0, reinterpret_cast<LPARAM>(m))) {
			delete m;
			return 0;
		}
	}

	int resolved = 0;
	for (size_t i = 0; i < album.tracks.size() && !cancelled(); i++) {
		const CStringW audioUrl = KHInsider::ResolveTrackAudioUrl(album.tracks[i].pageUrl);
		if (audioUrl.IsEmpty()) {
			continue;
		}

		auto* m = new KHRadioTrackMsg{ p->gen, static_cast<int>(i), album.tracks[i].name, audioUrl };
		if (!::PostMessageW(p->hWnd, WM_KHRADIO_TRACK, 0, reinterpret_cast<LPARAM>(m))) {
			delete m;
			return 0;
		}
		resolved++;
	}

	if (!cancelled()) {
		postText(WM_KHRADIO_DONE, resolved ? L"" : L"No playable tracks found in this album.");
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
	m_audioUrlToTrack.clear();
	m_bPlaylistStarted = false;

	CStringW status;
	status.Format(L"Found: %s (%u tracks) - starting...", m_currentAlbum.title.GetString(), static_cast<unsigned>(m_currentAlbum.tracks.size()));
	SetStatus(status);

	return 0;
}

LRESULT CKHRadioDlg::OnKHRadioTrack(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<KHRadioTrackMsg> m(reinterpret_cast<KHRadioTrackMsg*>(lParam));
	if (!m || m->gen != (*m_pGen)) {
		return 0;
	}

	m_audioUrlToTrack[m->audioUrl] = m->name;

	CStringW label;
	label.Format(L"%02d. %s", m->index + 1, m->name.GetString());

	CFileItemList fis;
	fis.emplace_back(CFileItem(m->audioUrl, label));

	auto pFrame = AfxGetMainFrame();
	if (!pFrame) {
		return 0;
	}

	if (!m_bPlaylistStarted) {
		pFrame->m_wndPlaylistBar.Empty();
		pFrame->m_wndPlaylistBar.Append(fis);
		pFrame->OpenCurPlaylistItem();
		m_bPlaylistStarted = true;

		CStringW status;
		status.Format(L"Playing: %s", m_currentAlbum.title.GetString());
		SetStatus(status);
	} else {
		pFrame->m_wndPlaylistBar.Append(fis);
	}

	return 0;
}

LRESULT CKHRadioDlg::OnKHRadioDone(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<KHRadioTextMsg> m(reinterpret_cast<KHRadioTextMsg*>(lParam));
	if (!m || m->gen != (*m_pGen)) {
		return 0;
	}

	m_bFetching = false;
	if (!m->text.IsEmpty()) {
		SetStatus(m->text);
	} else if (!m_currentAlbum.title.IsEmpty()) {
		CStringW status;
		status.Format(L"Playing: %s (%u tracks queued)", m_currentAlbum.title.GetString(), static_cast<unsigned>(m_audioUrlToTrack.size()));
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

	m_history.RecordTrack(m_currentAlbum.url, m_currentAlbum.title, it->second);
	RefreshHistoryList();
}

// CKHRadioBar

IMPLEMENT_DYNAMIC(CKHRadioBar, CPlayerBar)

CKHRadioBar::CKHRadioBar()
{
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
