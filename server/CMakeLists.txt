cmake_minimum_required (VERSION 3.22)

# 지원되는 경우 MSVC 컴파일러에 대해 핫 다시 로드 사용하도록 설정합니다.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("reverse-planning")

file(GLOB_RECURSE SOURCES "src/sources/*.cpp" "src/sources/*.c" "src/sources/*.cc")
file(GLOB_RECURSE HEADERS "src/header/*.h" "src/header/*.hpp")

# 이 프로젝트의 실행 파일에 소스를 추가합니다.
add_executable(main ${SOURCES} ${HEADERS})

# Precompiled Header 추가
target_precompile_headers(main PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/header/pch.h>"
)

target_include_directories(main
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src/header  # 루트 기준 절대경로
        ${CMAKE_CURRENT_BINARY_DIR}            # 동적 생성 헤더 경로
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sources  # 소스 내부 헤더용
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET main PROPERTY CXX_STANDARD 20)
endif()

# TODO: 필요한 경우 테스트를 추가하고 대상을 설치합니다.

find_package(protobuf CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)

# Protobuf 동적 소스코드 생성 설정
protobuf_generate(
    TARGET main
    LANGUAGE cpp
    IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../proto"
    PROTOS "${CMAKE_CURRENT_SOURCE_DIR}/../proto/Packet.proto"
           "${CMAKE_CURRENT_SOURCE_DIR}/../proto/Internal.proto"
)

add_definitions(-DUUID_SYSTEM_GENERATOR)

# 플랫폼 별 uuid system generator의 사용 설정
if(WIN32)
    set(PLATFORM_LIBS Bcrypt)
elseif(APPLE)
    find_library(CORE_FOUNDATION_LIB CoreFoundation)
    set(PLATFORM_LIBS ${CORE_FOUNDATION_LIB})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UUID REQUIRED uuid)
    set(PLATFORM_LIBS ${UUID_LIBRARIES})
    target_include_directories(main PRIVATE ${UUID_INCLUDE_DIRS})
endif()

target_link_libraries(main PRIVATE 
    protobuf::libprotobuf
    asio::asio 
    spdlog::spdlog_header_only 
    stduuid 
    ${PLATFORM_LIBS}
)
