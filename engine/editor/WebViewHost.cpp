#include "editor/WebViewHost.h"

#include <wrl.h>

#include <filesystem>
#include <stdexcept>

using Microsoft::WRL::Callback;

namespace aige {

WebViewHost::WebViewHost(HWND parent) : parent_(parent) {
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(result)) {
                    return result;
                }
                OnEnvironmentCreated(environment);
                return S_OK;
            })
            .Get());
    if (FAILED(hr)) {
        throw std::runtime_error("CreateCoreWebView2EnvironmentWithOptions failed");
    }
}

WebViewHost::~WebViewHost() {
    if (controller_) {
        controller_->Close();
    }
}

void WebViewHost::OnEnvironmentCreated(ICoreWebView2Environment* environment) {
    environment_ = environment;

    environment_->CreateCoreWebView2Controller(
        parent_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                     [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                         if (FAILED(result) || controller == nullptr) {
                             return result;
                         }
                         OnControllerCreated(controller);
                         return S_OK;
                     })
                     .Get());
}

void WebViewHost::OnControllerCreated(ICoreWebView2Controller* controller) {
    controller_ = controller;
    controller_->get_CoreWebView2(&webview_);

    wil::com_ptr<ICoreWebView2Settings> settings;
    webview_->get_Settings(&settings);
    settings->put_IsScriptEnabled(TRUE);
    settings->put_AreDefaultScriptDialogsEnabled(TRUE);
    settings->put_IsWebMessageEnabled(TRUE);

    EventRegistrationToken token;
    webview_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (!messageHandler_) {
                    return S_OK;
                }
                // The web side posts window.chrome.webview.postMessage(JSON.stringify(...))
                // — a plain string, not an object — so TryGetWebMessageAsString gives us
                // that exact JSON text back, ready to parse (get_WebMessageAsJson would
                // instead give a JSON-encoded *string literal*, i.e. double-escaped).
                wil::unique_cotaskmem_string message;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&message))) {
                    messageHandler_(std::wstring(message.get()));
                }
                return S_OK;
            })
            .Get(),
        &token);

    ApplyPendingState();
}

void WebViewHost::ApplyPendingState() {
    if (!controller_) {
        return;
    }

    if (pendingBounds_) {
        RECT bounds{pendingBounds_->x, pendingBounds_->y, pendingBounds_->x + pendingBounds_->width,
                    pendingBounds_->y + pendingBounds_->height};
        controller_->put_Bounds(bounds);
        pendingBounds_.reset();
    }

    for (const auto& mapping : pendingMappings_) {
        MapVirtualHostToFolder(mapping.hostName, mapping.folderPath);
    }
    pendingMappings_.clear();

    if (pendingVirtualHost_) {
        auto webview3 = webview_.try_query<ICoreWebView2_3>();
        if (webview3) {
            webview3->SetVirtualHostNameToFolderMapping(pendingVirtualHost_->hostName.c_str(),
                                                         pendingVirtualHost_->folderPath.c_str(),
                                                         COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
        std::wstring url = L"https://" + pendingVirtualHost_->hostName + L"/" + pendingVirtualHost_->relativePath;
        webview_->Navigate(url.c_str());
        pendingVirtualHost_.reset();
    }

    if (pendingHtml_) {
        webview_->NavigateToString(pendingHtml_->c_str());
        pendingHtml_.reset();
    }
}

void WebViewHost::SetBounds(int x, int y, int width, int height) {
    if (!controller_) {
        pendingBounds_ = PendingBounds{x, y, width, height};
        return;
    }
    RECT bounds{x, y, x + width, y + height};
    controller_->put_Bounds(bounds);
}

void WebViewHost::NavigateToVirtualHost(const std::wstring& hostName, const std::wstring& folderPath,
                                         const std::wstring& relativePath) {
    if (!webview_) {
        pendingVirtualHost_ = PendingVirtualHost{hostName, folderPath, relativePath};
        return;
    }
    auto webview3 = webview_.try_query<ICoreWebView2_3>();
    if (webview3) {
        webview3->SetVirtualHostNameToFolderMapping(hostName.c_str(), folderPath.c_str(),
                                                     COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    }
    std::wstring url = L"https://" + hostName + L"/" + relativePath;
    // WebView2 caches what the virtual host serves, so after a UI rebuild it
    // could keep showing the old index.html (and with it the old, hash-named
    // scripts). The file's modification time in the URL makes every build a
    // new address.
    std::error_code ec;
    auto stamp = std::filesystem::last_write_time(std::filesystem::path(folderPath) / relativePath, ec);
    if (!ec) {
        url += L"?v=" + std::to_wstring(stamp.time_since_epoch().count());
    }
    webview_->Navigate(url.c_str());
}

void WebViewHost::MapVirtualHostToFolder(const std::wstring& hostName, const std::wstring& folderPath) {
    if (!webview_) {
        pendingMappings_.push_back({hostName, folderPath, L""});
        return;
    }
    auto webview3 = webview_.try_query<ICoreWebView2_3>();
    if (webview3) {
        // ALLOW (not DENY_CORS): the editor page on another virtual host
        // loads images from here.
        webview3->SetVirtualHostNameToFolderMapping(hostName.c_str(), folderPath.c_str(),
                                                     COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    }
}

void WebViewHost::NavigateToHtml(const std::wstring& html) {
    if (!webview_) {
        pendingHtml_ = html;
        return;
    }
    webview_->NavigateToString(html.c_str());
}

void WebViewHost::PostMessageToPage(const std::wstring& json) {
    if (!webview_) {
        return;
    }
    webview_->PostWebMessageAsJson(json.c_str());
}

void WebViewHost::SetMessageHandler(std::function<void(std::wstring)> handler) {
    messageHandler_ = std::move(handler);
}

} // namespace aige
