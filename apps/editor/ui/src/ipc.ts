// Thin wrapper around window.chrome.webview, WebView2's native messaging
// bridge (engine/editor/WebViewHost.cpp on the C++ side). Host-to-web
// messages arrive pre-parsed (the host posts via PostWebMessageAsJson, so
// event.data is already an object); web-to-host messages are sent as a
// JSON string (the host receives them via TryGetWebMessageAsString and
// parses them itself).

export interface HostMessage {
  type: string
  [key: string]: unknown
}

declare global {
  interface Window {
    chrome?: {
      webview?: {
        postMessage: (message: string) => void
        addEventListener: (type: 'message', listener: (event: MessageEvent<unknown>) => void) => void
      }
    }
  }
}

export function sendToHost(message: HostMessage): void {
  const webview = window.chrome?.webview
  if (!webview) {
    console.warn('[ipc] window.chrome.webview unavailable (not running inside the editor host)', message)
    return
  }
  webview.postMessage(JSON.stringify(message))
}

export function onHostMessage(handler: (message: HostMessage) => void): void {
  const webview = window.chrome?.webview
  if (!webview) {
    return
  }
  webview.addEventListener('message', (event) => {
    handler(event.data as HostMessage)
  })
}
