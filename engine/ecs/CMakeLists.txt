include(FetchContent)

FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.13.2
)
FetchContent_MakeAvailable(entt)

add_library(aige_ecs INTERFACE)
target_include_directories(aige_ecs INTERFACE ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_ecs INTERFACE EnTT::EnTT)
target_compile_features(aige_ecs INTERFACE cxx_std_20)
