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

#include <afxwin.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>
#include <ExtLib/ui/ResizableLib/ResizableDialog.h>
#include "PlayerBar.h"
#include "KHInsider.h"

#define WM_KHRADIO_STATUS (WM_APP + 850)
#define WM_KHRADIO_ALBUM  (WM_APP + 851)
#define WM_KHRADIO_TRACK  (WM_APP + 852)
#define WM_KHRADIO_DONE   (WM_APP + 853)

// CKHRadioDlg dialog — replica of the site's "Random Album Advanced" form
// plus listening history.

class CKHRadioDlg : public CResizableDialog
{
public:
	CKHRadioDlg();
	virtual ~CKHRadioDlg();

	BOOL Create(CWnd* pParent = nullptr);

	enum { IDD = IDD_KHRADIO_DLG };

	CWnd* m_pParent = nullptr;

	CListBox m_listType;
	CListBox m_listYear;
	CListBox m_listPlatform;
	CButton  m_checkAvoidPlayed;
	CButton  m_buttonRandom;
	CStatic  m_staticStatus;
	CListBox m_listHistory;

	// called from CMainFrame when a new item starts playing
	void OnPlaybackStarted(const CStringW& path);

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	DECLARE_MESSAGE_MAP()

	afx_msg void OnDestroy();
	afx_msg void OnSelChangeFilters();
	afx_msg void OnAvoidPlayedClicked();
	afx_msg void OnRandomAlbum();
	afx_msg void OnHistoryDblClk();
	afx_msg LRESULT OnKHRadioStatus(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnKHRadioAlbum(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnKHRadioTrack(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnKHRadioDone(WPARAM wParam, LPARAM lParam);

private:
	struct FetchParams {
		HWND hWnd = nullptr;
		UINT gen = 0;
		std::shared_ptr<std::atomic<UINT>> pGen; // shared with the dialog; mismatch = cancelled
		bool bDirect = false;              // true: fetch m_albumUrl, false: POST random form
		CStringA formBody;
		CStringW albumUrl;
		bool bAvoidPlayed = false;
		std::vector<CStringW> playedAlbums; // snapshot of history URLs
	};

	static UINT FetchThreadProc(LPVOID pParam);

	void StartFetch(bool bDirect, const CStringW& albumUrl);
	void SetStatus(const CStringW& text);
	void PopulateFilterLists();
	void RestoreSelections();
	void SaveSelections();
	void RefreshHistoryList();
	static CStringW JoinSelection(CListBox& list);
	static void ApplySelection(CListBox& list, const CStringW& csv);
	static void GetSelectedValues(CListBox& list, std::vector<int>& values);

	std::shared_ptr<std::atomic<UINT>> m_pGen = std::make_shared<std::atomic<UINT>>(0);
	bool m_bFetching = false;

	KHInsider::Album m_currentAlbum;
	bool m_bPlaylistStarted = false;
	std::map<CStringW, CStringW> m_audioUrlToTrack; // direct audio URL -> track name

	CKHRadioHistory m_history;
};

// CKHRadioBar

class CKHRadioBar : public CPlayerBar
{
	DECLARE_DYNAMIC(CKHRadioBar)

public:
	CWnd* m_pParent = nullptr;

	CKHRadioBar();
	virtual ~CKHRadioBar();

	BOOL Create(CWnd* pParentWnd, UINT defDockBarID);
	virtual void ReloadTranslatableResources();

	CKHRadioDlg m_dlg;

protected:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()

public:
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnNcLButtonUp(UINT nHitTest, CPoint point);
};
