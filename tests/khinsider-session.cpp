#include "apps/mplayerc/KHWebSession.h"
#include "apps/mplayerc/KHRadioLayout.h"
#include <cassert>
#include <cstdio>
#include <future>
#include <thread>

using namespace KHWebSession;

int main(int argc, char** argv)
{
	assert(IsSiteUrl(L"https://downloads.khinsider.com/random-album-advanced"));
	assert(!IsSiteUrl(L"http://downloads.khinsider.com/random-album-advanced"));
	assert(!IsSiteUrl(L"https://downloads.khinsider.com.evil.test/"));
	assert(!IsSiteUrl(L"https://downloads.khinsider.com@evil.test/"));
	assert(!IsSiteUrl(L"https://evil.test/downloads.khinsider.com/"));
	assert(ClassifyResponse(200, "<title>Please Log In</title>") == Status::LoginRequired);
	assert(ClassifyResponse(200, "To access the website's features, you need to be registered and logged in.") == Status::LoginRequired);
	assert(ClassifyResponse(200, "<form><input name=\"login\"><input name=\"password\"></form>") == Status::LoginRequired);
	assert(ClassifyResponse(401, "") == Status::LoginRequired);
	assert(ClassifyResponse(403, "") == Status::Blocked);
	assert(ClassifyResponse(429, "") == Status::Blocked);
	assert(ClassifyResponse(200, "<title>Just a moment...</title>") == Status::Blocked);
	assert(ClassifyResponse(503, "maintenance") == Status::NetworkError);
	assert(ClassifyResponse(0, "") == Status::NetworkError);
	assert(ClassifyResponse(200, "<a href='/forums/login'>Log In</a><table id='songlist'>") == Status::Ready);
	puts("PASS: origin restrictions and response classification");
	for (UINT dpi : {96, 144, 192}) {
		const SIZE content = {MulDiv(300, dpi, 96), MulDiv(720, dpi, 96)};
		const int bar = MulDiv(17, dpi, 96);
		auto fit = KHRadioLayout::Measure(content, content, bar, bar);
		assert(!fit.horizontal && !fit.vertical);
		auto shortPanel = KHRadioLayout::Measure({content.cx+bar, content.cy/2}, content, bar, bar);
		assert(!shortPanel.horizontal && shortPanel.vertical);
		auto narrowPanel = KHRadioLayout::Measure({content.cx/2, content.cy/2}, content, bar, bar);
		assert(narrowPanel.horizontal && narrowPanel.vertical);
		assert(narrowPanel.client.cx == content.cx/2-bar);
		assert(narrowPanel.client.cy == content.cy/2-bar);
		const RECT button = {8, 280, 292, 312};
		const auto placed = KHRadioLayout::Place(button, dpi, 96, content, {content.cx+100, content.cy+100}, false);
		assert(placed.left == MulDiv(8, dpi, 96));
		assert(placed.right == MulDiv(292, dpi, 96)+100);
		assert(placed.bottom-placed.top == MulDiv(312, dpi, 96)-MulDiv(280, dpi, 96));
		const auto history = KHRadioLayout::Place(button, dpi, 96, content, {content.cx+100, content.cy+100}, true);
		assert(history.bottom == placed.bottom+100);
	}
	puts("PASS: exact fit, short/narrow viewport, and control geometry at 100%, 150%, 200% DPI");
	assert(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
	HWND owner = CreateWindowExW(0, L"STATIC", L"KH session test", WS_OVERLAPPEDWINDOW,
		0, 0, 600, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	assert(owner);
	Initialize(owner, owner);
	// Closing while a worker waits must release it, even before WebView starts.
	auto cancelled = std::async(std::launch::async, [] { return Request(L"https://downloads.khinsider.com/random-album-advanced"); });
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	Shutdown();
	assert(cancelled.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	assert(cancelled.get().status != Status::Ready);
	puts("PASS: shutdown releases pending worker");
	if (argc > 1) {
		Initialize(owner, owner);
		const bool login = std::string(argv[1]) == "--login";
		if (login) { ShowLogin(); }
		const auto deadline = GetTickCount64() + (login ? 600000 : 60000);
		while (GetTickCount64() < deadline) {
			MSG msg;
			while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
			const auto state = GetStatus();
			if (state != Status::Starting && (!login || state == Status::Ready)) { break; }
			MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
		}
		printf("Live browser status: %d (Ready=1, LoginRequired=2)\n", (int)GetStatus());
		assert(GetStatus() == Status::LoginRequired || GetStatus() == Status::Ready);
		if (GetStatus() == Status::LoginRequired) {
			auto request = std::async(std::launch::async, [] { return Request(L"https://downloads.khinsider.com/random-album-advanced"); });
			assert(request.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
			assert(request.get().status == Status::LoginRequired);
			puts("PASS: live logged-out session reports LoginRequired without parsing it as an album");
		} else {
			for (int i = 0; i < 3; ++i) {
				auto request = std::async(std::launch::async, [] {
					return Request(L"https://downloads.khinsider.com/random-album-advanced", "randomAdvanced=Show+Me+A+Random+Album");
				});
				while (request.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
					MSG msg;
					while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
					MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
				}
				auto response = request.get();
				assert(response.status == Status::Ready);
				assert(response.html.find("songlist") != std::string::npos);
				wprintf(L"PASS: authenticated random album %d: %s\n", i+1, response.url.c_str());
			}
		}
		Shutdown();
	}
	DestroyWindow(owner);
	CoUninitialize();
	return 0;
}
