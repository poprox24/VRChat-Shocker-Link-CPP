cmake_minimum_required(VERSION 3.20)
project("Shocker Link")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0601 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(FetchContent)

FetchContent_Declare(yaml-cpp GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git GIT_TAG yaml-cpp-0.9.0)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 12.1.0)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0)
FetchContent_MakeAvailable(json)

FetchContent_Declare(httplib GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git GIT_TAG v0.20.0)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(implot GIT_REPOSITORY https://github.com/epezent/implot.git GIT_TAG v0.17)
FetchContent_MakeAvailable(implot)

FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

set(SHOCKER_COMMON_SOURCES
    src/main.cpp
    lib/serialib.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

find_package(CURL 7.86 REQUIRED)

if(WIN32)
    list(APPEND SHOCKER_COMMON_SOURCES
        src/resource.rc
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    add_executable(ShockerLink WIN32 ${SHOCKER_COMMON_SOURCES})
    set_target_properties(ShockerLink PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME "Shocker_Link")

    # === MinGW-specific safety flags ===
    target_compile_definitions(ShockerLink PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(ShockerLink PRIVATE -Wno-narrowing -Wno-shift-count-overflow -Wno-template-body)

    target_link_libraries(ShockerLink PUBLIC
        yaml-cpp::yaml-cpp fmt::fmt-header-only nlohmann_json::nlohmann_json httplib::httplib CURL::libcurl
        ws2_32 comdlg32 shell32 dwmapi wininet glfw opengl32
    )
else()
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
    find_package(PNG REQUIRED)

    list(APPEND SHOCKER_COMMON_SOURCES
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    add_executable(ShockerLink ${SHOCKER_COMMON_SOURCES})
    set_target_properties(ShockerLink PROPERTIES OUTPUT_NAME "Shocker_Link")

    target_link_libraries(ShockerLink PUBLIC
        yaml-cpp::yaml-cpp fmt::fmt-header-only nlohmann_json::nlohmann_json httplib::httplib
        glfw OpenGL::GL Threads::Threads CURL::libcurl PNG::PNG
    )
endif()

target_include_directories(ShockerLink PRIVATE
    lib src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
)