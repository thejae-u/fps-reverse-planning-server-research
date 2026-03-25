# client/CMakeLists.txt
cmake_minimum_required (VERSION 3.21)
project(client)

find_package(asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)

if(WIN32)
    set(PLATFORM_LIBS Bcrypt)
elseif(APPLE)
    find_library(CORE_FOUNDATION_LIB CoreFoundation)
    set(PLATFORM_LIBS ${CORE_FOUNDATION_LIB})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UUID REQUIRED uuid)
    set(PLATFORM_LIBS ${UUID_LIBRARIES})
    target_include_directories(client_main PRIVATE ${UUID_INCLUDE_DIRS})
endif()

# Source and Header files
file(GLOB_RECURSE CLIENT_SOURCES "src/sources/*.cpp" "src/sources/*.cc")
file(GLOB_RECURSE CLIENT_HEADERS "src/header/*.hpp" "src/header/*.h")

file(GLOB IMGUI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/imgui_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/backends/imgui_impl_glfw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/backends/imgui_impl_opengl3.cpp"
)

add_executable(client_main ${CLIENT_SOURCES} ${CLIENT_HEADERS} ${IMGUI_SOURCES})
set_target_properties(client_main PROPERTIES OUTPUT_NAME "client")

target_include_directories(client_main PRIVATE
    src/header
    ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/backends
)

target_link_libraries(client_main PRIVATE
    asio::asio
    spdlog::spdlog_header_only
    stduuid
    glfw
    glad::glad
    protobuf::libprotobuf
    ${PLATFORM_LIBS}
)

if(WIN32)
    target_link_libraries(client_main PRIVATE opengl32)
endif()