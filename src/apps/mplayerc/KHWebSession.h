#pragma once

#include <windows.h>
#include <string>

// The browser and its profile stay on the UI thread. Only Request is called
// by scraper workers. No passwords or cookies leave the browser profile.
namespace KHWebSession {
enum class Status { Starting, Ready, LoginRequired, Blocked, NetworkError, Unavailable, LayoutChanged };
constexpr UINT WM_STATUS = WM_APP + 855;

struct Response {
	Status status = Status::NetworkError;
	unsigned httpStatus = 0;
	std::wstring url;
	std::string html;
};

void Initialize(HWND owner, HWND notify);
void Shutdown();
void ShowLogin();
Status GetStatus();
Response Request(const std::wstring& url, const std::string& body = {});

bool IsSiteUrl(const std::wstring& url);
Status ClassifyResponse(unsigned status, const std::string& html);
}
