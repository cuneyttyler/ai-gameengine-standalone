#include "install/InstallPath.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace aige {

namespace fs = std::filesystem;

fs::path ProgramDir() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (n > 0 && n < std::size(buffer)) return fs::path(std::wstring(buffer, n)).parent_path();
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

fs::path InstalledOr(const fs::path& built, const std::string& relative) {
    static const fs::path dir = ProgramDir();
    std::error_code ec;
    const fs::path installed = dir / fs::u8path(relative);
    if (fs::is_directory(installed, ec)) return installed;
    return built;
}

} // namespace aige
