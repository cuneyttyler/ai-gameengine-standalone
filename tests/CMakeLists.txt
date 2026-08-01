include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(aige_tests
    platform_test.cpp
)
target_link_libraries(aige_tests PRIVATE aige_platform gtest_main)

include(GoogleTest)
gtest_discover_tests(aige_tests)
