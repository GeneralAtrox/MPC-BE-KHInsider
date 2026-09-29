// Copyright (C) 2026 MPC-BE KH Radio edition. GPL-3.0-or-later.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "KHWebSession.h"
#include <WebView2.h>
#include <wrl.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "ExtLib/rapidjson/include/rapidjson/document.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace KHWebSession {
namespace {
constexpr wchar_t Home[] = L"https://downloads.khinsider.com/random-album-advanced";
constexpr wchar_t Login[] = L"https://downloads.khinsider.com/forums/login";
constexpr UINT WM_PUMP = WM_APP + 880;
constexpr auto RequestTimeout = std::chrono::seconds(45);

std::string Utf8(const std::wstring& text)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
	std::string result(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), n, nullptr, nullptr);
	return result;
}

std::wstring Wide(const std::string& text)
{
	const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
	std::wstring result(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), n);
	return result;
}

struct Pending {
	std::wstring url;
	std::string body;
	bool sent = false;
	bool done = false;
	bool probe = false;
	ULONGLONG started = GetTickCount64();
	Response response;
	std::condition_variable cv;
};

struct Session : std::enable_shared_from_this<Session> {
	HWND window = nullptr;
	HWND notify = nullptr;
	std::atomic<bool> closed{false};
	std::atomic<Status> status{Status::Starting};
	std::mutex mutex;
	std::map<std::string, std::shared_ptr<Pending>> pending;
	ComPtr<ICoreWebView2Controller> controller;
	ComPtr<ICoreWebView2> web;
	bool documentReady = false;
	bool loginRequested = false;
	bool closeAfterLogin = false;
	ULONGLONG initialized = GetTickCount64();
	std::wstring profile;

	void SetStatus(Status next) {
		if (!closed && status.exchange(next) != next) {
			PostMessageW(notify, WM_STATUS, (WPARAM)next, 0);
		}
	}

	void Cancel(Status reason) {
		std::lock_guard<std::mutex> lock(mutex);
		for (auto& entry : pending) {
			entry.second->response.status = reason;
			entry.second->done = true;
			entry.second->cv.notify_all();
		}
		pending.clear();
	}

	void Fail(Status reason) {
		documentReady = false;
		SetStatus(reason);
		Cancel(reason);
	}

	std::string Add(const std::shared_ptr<Pending>& request) {
		GUID guid;
		if (FAILED(CoCreateGuid(&guid))) { return {}; }
		wchar_t id[40] = {};
		StringFromGUID2(guid, id, 40);
		std::string key = Utf8(id);
		std::lock_guard<std::mutex> lock(mutex);
		if (closed) { return {}; }
		pending.emplace(key, request);
		PostMessageW(window, WM_PUMP, 0, 0);
		return key;
	}

	void Complete(const std::string& id, Response response) {
		bool probe = false;
		Status result;
		{
			std::lock_guard<std::mutex> lock(mutex);
			auto it = pending.find(id);
			if (it == pending.end()) { return; } // timed out or cancelled
			probe = it->second->probe;
			if (probe && response.status == Status::Ready
					&& response.html.find("randomAdvanced") == std::string::npos) {
				response.status = Status::LayoutChanged;
			}
			it->second->response = std::move(response);
			result = it->second->response.status;
			it->second->done = true;
			it->second->cv.notify_all();
			pending.erase(it);
		}
		if (probe || result == Status::LoginRequired || result == Status::Blocked) {
			SetStatus(result);
		}
		if (probe && result == Status::Ready && closeAfterLogin) {
			loginRequested = false;
			closeAfterLogin = false;
			ShowWindow(window, SW_HIDE);
		}
	}

	void CheckTimeouts() {
		std::vector<std::string> expired;
		{
			std::lock_guard<std::mutex> lock(mutex);
			for (const auto& entry : pending) {
				if (GetTickCount64()-entry.second->started > 40000) { expired.push_back(entry.first); }
			}
		}
		for (const auto& id : expired) { Complete(id, {}); }
		if (status == Status::Starting && GetTickCount64()-initialized > 45000) { Fail(Status::NetworkError); }
	}

	void Pump() {
		if (!documentReady || !web || closed) { return; }
		std::map<std::string, std::shared_ptr<Pending>> work;
		{
			std::lock_guard<std::mutex> lock(mutex);
			for (auto& entry : pending) {
				if (!entry.second->sent) {
					entry.second->sent = true;
					work.emplace(entry);
				}
			}
		}
		for (const auto& entry : work) {
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
			json.StartObject();
			json.Key("id"); json.String(entry.first.c_str());
			json.Key("url"); json.String(Utf8(entry.second->url).c_str());
			json.Key("body"); json.String(entry.second->body.c_str());
			json.EndObject();
			// Fetch runs inside the site's real browser session. Browser cookie
			// scope and redirects apply; credentials are never copied to native HTTP.
			std::string script = "(async(q)=>{const c=new AbortController();"
				"const t=setTimeout(()=>c.abort(),30000);try{"
				"const r=await fetch(q.url,{method:q.body?'POST':'GET',"
				"body:q.body||undefined,headers:q.body?{'Content-Type':'application/x-www-form-urlencoded'}:{},"
				"credentials:'same-origin',cache:'no-store',signal:c.signal});"
				"const html=await r.text();window.chrome.webview.postMessage({id:q.id,status:r.status,url:r.url,html});"
				"}catch(e){window.chrome.webview.postMessage({id:q.id,status:0,url:q.url,html:''});}"
				"finally{clearTimeout(t);}})(" + std::string(buffer.GetString()) + ");";
			const std::weak_ptr<Session> weak = shared_from_this();
			const auto id = entry.first;
			const auto hr = web->ExecuteScript(Wide(script).c_str(),
				Callback<ICoreWebView2ExecuteScriptCompletedHandler>([weak, id](HRESULT result, LPCWSTR) -> HRESULT {
					if (auto s = weak.lock(); s && !s->closed && FAILED(result)) { s->Complete(id, {}); }
					return S_OK;
				}).Get());
			if (FAILED(hr)) { Complete(id, {}); }
		}
	}

	void Resize() {
		RECT rect;
		GetClientRect(window, &rect);
		if (controller) { controller->put_Bounds(rect); }
	}

	void Attach(ICoreWebView2Controller* value) {
		controller = value;
		controller->get_CoreWebView2(&web);
		if (!web) { Fail(Status::Unavailable); return; }
		Resize();
		controller->put_IsVisible(TRUE);
		ComPtr<ICoreWebView2Settings> settings;
		web->get_Settings(&settings);
		if (settings) {
			ComPtr<ICoreWebView2Settings4> settings4;
			if (SUCCEEDED(settings.As(&settings4))) { settings4->put_IsPasswordAutosaveEnabled(FALSE); }
		}
		const std::weak_ptr<Session> weak = shared_from_this();
		web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
			[weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
				if (auto s = weak.lock(); s && !s->closed) {
					LPWSTR uri = nullptr;
					args->get_Uri(&uri);
					const bool allowed = uri && IsSiteUrl(uri);
					CoTaskMemFree(uri);
					if (!allowed) { args->put_Cancel(TRUE); return S_OK; }
					s->documentReady = false;
					s->Cancel(Status::NetworkError);
				}
				return S_OK;
			}).Get(), nullptr);
		web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
			[weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
				if (auto s = weak.lock(); s && !s->closed) {
					BOOL success = FALSE;
					args->get_IsSuccess(&success);
					if (!success) { s->Fail(Status::NetworkError); return S_OK; }
					s->documentReady = true;
					auto probe = std::make_shared<Pending>();
					probe->url = Home;
					probe->probe = true;
					s->Add(probe);
					s->Pump();
				}
				return S_OK;
			}).Get(), nullptr);
		web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
			[weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
				if (auto s = weak.lock(); s && !s->closed) {
					LPWSTR source = nullptr, raw = nullptr;
					args->get_Source(&source);
					const bool allowed = source && IsSiteUrl(source);
					CoTaskMemFree(source);
					if (!allowed || FAILED(args->get_WebMessageAsJson(&raw)) || !raw) { return S_OK; }
					const auto text = Utf8(raw);
					CoTaskMemFree(raw);
					rapidjson::Document json;
					json.Parse(text.c_str());
					if (json.HasParseError() || !json.IsObject()
							|| !json.HasMember("id") || !json["id"].IsString()
							|| !json.HasMember("status") || !json["status"].IsUint()
							|| !json.HasMember("url") || !json["url"].IsString()
							|| !json.HasMember("html") || !json["html"].IsString()) { return S_OK; }
					Response response;
					response.url = Wide(json["url"].GetString());
					response.httpStatus = json["status"].GetUint();
					response.html = json["html"].GetString();
					response.status = IsSiteUrl(response.url)
						? ClassifyResponse(response.httpStatus, response.html) : Status::NetworkError;
					s->Complete(json["id"].GetString(), std::move(response));
				}
				return S_OK;
			}).Get(), nullptr);
		web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
			[](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
				args->put_Handled(TRUE);
				return S_OK;
			}).Get(), nullptr);
		web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
			[weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
				if (auto s = weak.lock(); s && !s->closed) { s->Fail(Status::Unavailable); }
				return S_OK;
			}).Get(), nullptr);
		if (FAILED(web->Navigate(loginRequested ? Login : Home))) { Fail(Status::NetworkError); }
	}

	void Start() {
		initialized = GetTickCount64();
		SetStatus(Status::Starting);
		const std::weak_ptr<Session> weak = shared_from_this();
		const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.c_str(), nullptr,
			Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
				[weak](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
					if (auto s = weak.lock(); s && !s->closed) {
						if (FAILED(result) || !environment) { s->Fail(Status::Unavailable); return S_OK; }
						const HRESULT create = environment->CreateCoreWebView2Controller(s->window,
							Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
								[weak](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
									if (auto s = weak.lock(); s && !s->closed) {
										if (FAILED(result) || !controller) { s->Fail(Status::Unavailable); }
										else { s->Attach(controller); }
									}
									return S_OK;
								}).Get());
						if (FAILED(create)) { s->Fail(Status::Unavailable); }
					}
					return S_OK;
				}).Get());
		if (FAILED(hr)) { Fail(Status::Unavailable); }
	}
};

std::shared_ptr<Session> current;

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto* s = reinterpret_cast<Session*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) {
		s = static_cast<Session*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
	}
	if (s) {
		if (message == WM_SIZE && s->window) { s->Resize(); return 0; }
		if (message == WM_PUMP) { s->Pump(); return 0; }
		if (message == WM_TIMER) { s->CheckTimeouts(); return 0; }
		if (message == WM_CLOSE) { ShowWindow(window, SW_HIDE); return 0; }
		if (message == WM_DPICHANGED) {
			const auto* r = reinterpret_cast<RECT*>(lParam);
			SetWindowPos(window, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER);
			return 0;
		}
	}
	return DefWindowProcW(window, message, wParam, lParam);
}
}

bool IsSiteUrl(const std::wstring& url)
{
	constexpr wchar_t prefix[] = L"https://downloads.khinsider.com/";
	return url.size() >= _countof(prefix)-1 && _wcsnicmp(url.c_str(), prefix, _countof(prefix)-1) == 0;
}

Status ClassifyResponse(unsigned status, const std::string& html)
{
	std::string lower = html;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
	if (status == 401 || lower.find("to access the website's features, you need to be registered and logged in") != std::string::npos
			|| lower.find("<title>please log in") != std::string::npos
			|| (lower.find("name=\"password\"") != std::string::npos && lower.find("name=\"login\"") != std::string::npos)) {
		return Status::LoginRequired;
	}
	if (status == 403 || status == 429 || lower.find("<title>just a moment") != std::string::npos
			|| lower.find("<title>attention required") != std::string::npos) { return Status::Blocked; }
	return status >= 200 && status < 300 ? Status::Ready : Status::NetworkError;
}

void Initialize(HWND owner, HWND notify)
{
	if (std::atomic_load(&current)) { return; }
	auto s = std::make_shared<Session>();
	s->notify = notify;
	std::atomic_store(&current, s);
	PWSTR local = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) { s->Fail(Status::Unavailable); return; }
	s->profile = std::wstring(local) + L"\\MPC-BE\\KHInsider.WebView2";
	CoTaskMemFree(local);
	SHCreateDirectoryExW(nullptr, s->profile.c_str(), nullptr);
	WNDCLASSW cls = {};
	cls.lpfnWndProc = WindowProc;
	cls.hInstance = GetModuleHandleW(nullptr);
	cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
	cls.lpszClassName = L"MPCBE.KHInsider.Session";
	RegisterClassW(&cls);
	s->window = CreateWindowExW(WS_EX_APPWINDOW, cls.lpszClassName,
		L"KHInsider sign in - downloads.khinsider.com", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 900, 720, owner, nullptr, cls.hInstance, s.get());
	if (!s->window) { s->Fail(Status::Unavailable); return; }
	SetTimer(s->window, 1, 1000, nullptr);
	s->Start();
}

void Shutdown()
{
	auto s = std::atomic_exchange(&current, std::shared_ptr<Session>());
	if (!s) { return; }
	s->closed = true;
	s->Cancel(Status::Unavailable);
	if (s->controller) { s->controller->Close(); }
	s->web.Reset();
	s->controller.Reset();
	std::lock_guard<std::mutex> lock(s->mutex);
	if (s->window) { DestroyWindow(s->window); s->window = nullptr; }
}

void ShowLogin()
{
	auto s = std::atomic_load(&current);
	if (!s || !s->window) { return; }
	s->loginRequested = true;
	s->closeAfterLogin = s->status != Status::Ready;
	ShowWindow(s->window, SW_SHOWNORMAL);
	SetForegroundWindow(s->window);
	if (s->status == Status::Unavailable && s->controller) {
		s->controller->Close();
		s->controller.Reset();
		s->web.Reset();
	}
	if (s->web) {
		s->web->Navigate(s->status == Status::Blocked || s->status == Status::Ready ? Home : Login);
	} else if (s->status != Status::Starting) {
		s->Start();
	}
}

Status GetStatus()
{
	auto s = std::atomic_load(&current);
	return s ? s->status.load() : Status::Unavailable;
}

Response Request(const std::wstring& url, const std::string& body)
{
	auto s = std::atomic_load(&current);
	if (!s || !IsSiteUrl(url)) { return {}; }
	const auto status = s->status.load();
	if (status != Status::Ready && status != Status::Starting) { Response r; r.status = status; return r; }
	auto request = std::make_shared<Pending>();
	request->url = url;
	request->body = body;
	const auto id = s->Add(request);
	if (id.empty()) { return {}; }
	std::unique_lock<std::mutex> lock(s->mutex);
	if (!request->cv.wait_for(lock, RequestTimeout, [&] { return request->done; })) {
		s->pending.erase(id);
		return {};
	}
	return std::move(request->response);
}
}
