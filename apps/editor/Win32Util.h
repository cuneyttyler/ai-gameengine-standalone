#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
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

// The native folder picker (projects, development_plan.md 4.2). Nothing if
// the user cancels.
std::optional<std::filesystem::path> ShowFolderDialog(HWND owner, const std::wstring& title);

// One file to open, or where to save one (its type's name and pattern, e.g. L"AIGE ontology",
// L"*.aige-ontology.json"). Nothing if the user cancels.
std::optional<std::filesystem::path> ShowOpenFileDialog(HWND owner, const std::wstring& title, const std::wstring& typeName,
                                                        const std::wstring& pattern);
std::optional<std::filesystem::path> ShowSaveFileDialog(HWND owner, const std::wstring& title, const std::wstring& typeName,
                                                        const std::wstring& pattern, const std::wstring& defaultName);

// Starts another editor process with `arguments` (e.g. --project "<dir>",
// or --welcome). False if it couldn't be started.
bool StartEditorProcess(const std::wstring& arguments);

} // namespace aige
