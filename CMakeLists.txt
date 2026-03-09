cmake_minimum_required(VERSION 3.20)
project(ShockerLink)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(_WIN32_WINNT=0x0601)

include(FetchContent)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 12.1.0
)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(json)

# httplib — header-only HTTP server/client
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.20.0
)
FetchContent_MakeAvailable(httplib)

add_executable(ShockerLink
    src/main.cpp
    lib/serialib.cpp
)

target_link_libraries(ShockerLink PUBLIC
    yaml-cpp::yaml-cpp
    fmt::fmt-header-only
    nlohmann_json::nlohmann_json
    httplib::httplib
    ws2_32
)

target_include_directories(ShockerLink PRIVATE lib src)