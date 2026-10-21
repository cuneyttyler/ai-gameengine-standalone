#include "Win32Util.h"

#include <shobjidl.h>
#include <wil/com.h>

namespace aige {

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                     nullptr);
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length, nullptr,
                        nullptr);
    return utf8;
}

std::vector<std::filesystem::path> ShowImportDialog(HWND owner) {
    std::vector<std::filesystem::path> result;

    wil::com_ptr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return result;
    }

    COMDLG_FILTERSPEC filters[] = {
        {L"Images and audio", L"*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.wav;*.mp3;*.flac"},
        {L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.tga"},
        {L"Audio", L"*.wav;*.mp3;*.flac"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetTitle(L"Import assets");

    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);

    if (FAILED(dialog->Show(owner))) {
        return result; // cancelled
    }

    wil::com_ptr<IShellItemArray> items;
    if (FAILED(dialog->GetResults(&items))) {
        return result;
    }
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; ++i) {
        wil::com_ptr<IShellItem> item;
        if (FAILED(items->GetItemAt(i, &item))) {
            continue;
        }
        wil::unique_cotaskmem_string path;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            result.emplace_back(path.get());
        }
    }
    return result;
}

std::optional<std::filesystem::path> ShowFolderDialog(HWND owner, const std::wstring& title) {
    wil::com_ptr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    dialog->SetTitle(title.c_str());
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt; // cancelled
    }
    wil::com_ptr<IShellItem> item;
    wil::unique_cotaskmem_string path;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        return std::nullopt;
    }
    return std::filesystem::path(path.get());
}

namespace {
std::optional<std::filesystem::path> ResultOf(IFileDialog* dialog, HWND owner) {
    if (FAILED(dialog->Show(owner))) return std::nullopt; // cancelled
    wil::com_ptr<IShellItem> item;
    wil::unique_cotaskmem_string path;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
    return std::filesystem::path(path.get());
}
} // namespace

std::optional<std::filesystem::path> ShowOpenFileDialog(HWND owner, const std::wstring& title, const std::wstring& typeName,
                                                        const std::wstring& pattern) {
    wil::com_ptr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return std::nullopt;
    dialog->SetTitle(title.c_str());
    const COMDLG_FILTERSPEC filters[] = {{typeName.c_str(), pattern.c_str()}, {L"All files", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    return ResultOf(dialog.get(), owner);
}

std::optional<std::filesystem::path> ShowSaveFileDialog(HWND owner, const std::wstring& title, const std::wstring& typeName,
                                                        const std::wstring& pattern, const std::wstring& defaultName) {
    wil::com_ptr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return std::nullopt;
    dialog->SetTitle(title.c_str());
    const COMDLG_FILTERSPEC filters[] = {{typeName.c_str(), pattern.c_str()}};
    dialog->SetFileTypes(1, filters);
    dialog->SetFileName(defaultName.c_str());
    dialog->SetDefaultExtension(L"json");
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    return ResultOf(dialog.get(), owner);
}

bool StartEditorProcess(const std::wstring& arguments) {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    // --after: the new editor waits for this one to exit first, so the two
    // never start and stop WebView2 on the same data folder at once.
    std::wstring command = L"\"" + std::wstring(exe) + L"\" " + arguments + L" --after " +
                           std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

} // namespace aige
