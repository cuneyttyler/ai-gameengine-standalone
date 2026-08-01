#pragma once

#include <windows.h>

#include <WebView2.h>
#include <wil/com.h>

#include <functional>
#include <optional>
#include <string>

namespace aige {

// Wraps WebView2 environment/controller/webview creation and the
// host<->page JSON messaging bridge — the web-side counterpart of
// technical.md's "one consistent JSON message schema end-to-end" idea,
// just over WebView2's messaging transport instead of the AI service's
// HTTP one.
//
// WebView2's own setup is asynchronous (environment creation completes via
// a callback fired later on the same message loop), so the constructor
// returns before the control necessarily exists. SetBounds/Navigate*/
// PostMessageToPage calls made before that are queued and replayed once
// ready — callers don't need to check IsReady() themselves.
class WebViewHost {
public:
    explicit WebViewHost(HWND parent);
    ~WebViewHost();

    WebViewHost(const WebViewHost&) = delete;
    WebViewHost& operator=(const WebViewHost&) = delete;

    bool IsReady() const { return controller_ != nullptr; }

    // Repositions/resizes the WebView2 control within its parent.
    void SetBounds(int x, int y, int width, int height);

    // Maps a virtual hostname to a local folder (WebView2's recommended way
    // to serve local content — avoids file:// ES-module/CORS restrictions)
    // and navigates to it. Used once the Vue build exists (milestone 1.1b).
    void NavigateToVirtualHost(const std::wstring& hostName, const std::wstring& folderPath,
                                const std::wstring& relativePath);

    // Navigates directly to an inline HTML string — used by the 1.1a
    // de-risking spike, before any Vue build exists.
    void NavigateToHtml(const std::wstring& html);

    void PostMessageToPage(const std::wstring& json);
    void SetMessageHandler(std::function<void(std::wstring)> handler);

private:
    void OnEnvironmentCreated(ICoreWebView2Environment* environment);
    void OnControllerCreated(ICoreWebView2Controller* controller);
    void ApplyPendingState();

    HWND parent_;
    wil::com_ptr<ICoreWebView2Environment> environment_;
    wil::com_ptr<ICoreWebView2Controller> controller_;
    wil::com_ptr<ICoreWebView2> webview_;
    std::function<void(std::wstring)> messageHandler_;

    // Queued until the controller/webview exist.
    struct PendingBounds {
        int x, y, width, height;
    };
    std::optional<PendingBounds> pendingBounds_;
    std::optional<std::wstring> pendingHtml_;
    struct PendingVirtualHost {
        std::wstring hostName;
        std::wstring folderPath;
        std::wstring relativePath;
    };
    std::optional<PendingVirtualHost> pendingVirtualHost_;
};

} // namespace aige
