# client/CMakeLists.txt
cmake_minimum_required(VERSION 3.22)

# 지원되는 경우 MSVC 컴파일러에 대해 핫 다시 로드 사용하도록 설정합니다.
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif ()

project(client)

# C++ 표준 지정
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 외부 라이브러리(패키지) 찾기
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)

# 플랫폼 별 라이브러리 설정
if(WIN32)
    set(PLATFORM_LIBS opengl32 Bcrypt)
elseif(APPLE)
    find_library(CORE_FOUNDATION_LIB CoreFoundation)
    find_package(OpenGL REQUIRED)
    set(PLATFORM_LIBS ${CORE_FOUNDATION_LIB} OpenGL::GL)
else()
    find_package(OpenGL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UUID REQUIRED uuid)
    set(PLATFORM_LIBS OpenGL::GL ${UUID_LIBRARIES})
    set(PLATFORM_INCLUDES ${UUID_INCLUDE_DIRS})
endif()

# 소스 및 헤더 파일 자동 수집
file(GLOB_RECURSE CLIENT_SOURCES "src/sources/*.cpp" "src/sources/*.c" "src/sources/*.cc")
file(GLOB_RECURSE CLIENT_HEADERS "src/header/*.h" "src/header/*.hpp")

# ImGui 소스 수집 (thirdparty/imgui)
file(GLOB IMGUI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/imgui_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/backends/imgui_impl_glfw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/backends/imgui_impl_opengl3.cpp"
)

# 실행 파일 타깃 설정
add_executable(client_main ${CLIENT_SOURCES} ${CLIENT_HEADERS} ${IMGUI_SOURCES})
set_target_properties(client_main PROPERTIES OUTPUT_NAME "client")

# Protobuf 동적 소스코드 생성 설정
protobuf_generate(
    TARGET client_main
    LANGUAGE cpp
    IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../proto"
    PROTOS "${CMAKE_CURRENT_SOURCE_DIR}/../proto/Packet.proto"
           "${CMAKE_CURRENT_SOURCE_DIR}/../proto/Internal.proto"
)

# Include 디렉터리 설정
target_include_directories(client_main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/header
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sources
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui/backends
    ${PLATFORM_INCLUDES}
)

# 라이브러리 링크
target_link_libraries(client_main PRIVATE
    cpr::cpr
    nlohmann_json::nlohmann_json
    glfw
    glad::glad
    spdlog::spdlog_header_only
    asio::asio
    protobuf::libprotobuf
    stduuid
    ${PLATFORM_LIBS}
)