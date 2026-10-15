#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace aige {

// UTF-8 <-> UTF-16 for the IPC boundary (JSON is UTF-8, WebView2 and Win32
// are UTF-16). Asset and project paths can contain any characters.
std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);

// The native multi-select "Import assets" dialog. Returns nothing if the
// user cancels.
std::vector<std::filesystem::path> ShowImportDialog(HWND owner);

} // namespace aige
