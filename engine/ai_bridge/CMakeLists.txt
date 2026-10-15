include(FetchContent)

# Header-only HTTP client for talking to the local AI service.
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.60.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake
)
FetchContent_MakeAvailable(httplib)

add_library(aige_ai_bridge STATIC
    AiClient.h
    AiClient.cpp
    ContextBuilder.h
    ContextBuilder.cpp
    Proposal.h
    Proposal.cpp
    DryRun.h
    DryRun.cpp
    UserPrefs.h
    UserPrefs.cpp
    AiConnection.h
    AiConnection.cpp
    ReviewPolicy.h
    ReviewPolicy.cpp
    TaskRunner.h
    TaskRunner.cpp
)
target_include_directories(aige_ai_bridge PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_include_directories(aige_ai_bridge PUBLIC ${httplib_SOURCE_DIR})
target_link_libraries(aige_ai_bridge PUBLIC aige_editing aige_assets aige_gameplay nlohmann_json::nlohmann_json)
target_compile_features(aige_ai_bridge PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(aige_ai_bridge PUBLIC ws2_32)
endif()
