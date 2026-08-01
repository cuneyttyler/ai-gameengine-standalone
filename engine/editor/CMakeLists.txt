include(FetchContent)

# WebView2 SDK and WIL (Windows Implementation Library, used for
# wil::com_ptr/wil::unique_cotaskmem_string) are normally consumed via
# Visual Studio's NuGet integration — since we're a plain CMake project, we
# fetch the .nupkg directly (it's just a zip archive) the same way every
# other vendored dependency here is fetched, renaming the download so
# CMake's archive extraction recognizes the format.
FetchContent_Declare(
    webview2sdk
    URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.4078.44
    DOWNLOAD_NAME webview2sdk.zip
)
FetchContent_Declare(
    wil
    URL https://www.nuget.org/api/v2/package/Microsoft.Windows.ImplementationLibrary/1.0.260126.7
    DOWNLOAD_NAME wil.zip
)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(webview2sdk wil nlohmann_json)

add_library(webview2 INTERFACE)
target_include_directories(webview2 INTERFACE ${webview2sdk_SOURCE_DIR}/build/native/include)
target_link_libraries(webview2 INTERFACE ${webview2sdk_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib)

add_library(wil INTERFACE)
target_include_directories(wil INTERFACE ${wil_SOURCE_DIR}/include)

add_library(aige_editor STATIC
    ViewportWindow.h
    ViewportWindow.cpp
    WebViewHost.h
    WebViewHost.cpp
)
target_include_directories(aige_editor PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_editor PUBLIC aige_platform webview2 wil nlohmann_json::nlohmann_json)
target_compile_features(aige_editor PUBLIC cxx_std_20)
# We call the explicit W-suffixed Win32 APIs everywhere, but predefined
# resource macros (IDC_ARROW, etc.) are still UNICODE-macro-sensitive.
target_compile_definitions(aige_editor PUBLIC UNICODE _UNICODE)
