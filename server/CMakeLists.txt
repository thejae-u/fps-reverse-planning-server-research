cmake_minimum_required(VERSION 3.22)

# 지원되는 경우 MSVC 컴파일러에 대해 핫 다시 로드 사용하도록 설정합니다.
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif ()

# 솔루션명
project("reverse-planning")

# 소스, 헤더 자동 수집
file(GLOB_RECURSE SOURCES "src/sources/*.cpp" "src/sources/*.c" "src/sources/*.cc")
file(GLOB_RECURSE HEADERS "src/header/*.h" "src/header/*.hpp")

# 실행 파일 타깃 설정
add_executable(main ${SOURCES} ${HEADERS})

# Precompiled Header 설정
target_precompile_headers(main PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/header/pch.h>"
)

# include directory 설정
target_include_directories(main
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/header  # 루트 기준 절대경로
        ${CMAKE_CURRENT_BINARY_DIR}            # 동적 생성 헤더 경로
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sources  # 소스 내부 헤더용
)

# C++ 표준 지정
target_compile_features(main PRIVATE cxx_std_20)

# 외부 라이브러리(패키지) 찾기
find_package(protobuf CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Protobuf 동적 소스코드 생성 설정
protobuf_generate(
        TARGET main
        LANGUAGE cpp
        IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../proto"
        PROTOS "${CMAKE_CURRENT_SOURCE_DIR}/../proto/Packet.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/../proto/Internal.proto"
)

# uuid 매크로 정의
target_compile_definitions(main PRIVATE UUID_SYSTEM_GENERATOR)

# 플랫폼 별 uuid system generator의 사용 설정
if (WIN32)
    set(PLATFORM_LIBS Bcrypt)
elseif (APPLE)
    find_library(CORE_FOUNDATION_LIB CoreFoundation)
    set(PLATFORM_LIBS ${CORE_FOUNDATION_LIB})
else ()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UUID REQUIRED uuid)
    set(PLATFORM_LIBS ${UUID_LIBRARIES})
    target_include_directories(main PRIVATE ${UUID_INCLUDE_DIRS})
endif ()

# 라이브러리 링크
target_link_libraries(main PRIVATE
        protobuf::libprotobuf
        asio::asio
        spdlog::spdlog_header_only
        stduuid
        nlohmann_json::nlohmann_json
        ${PLATFORM_LIBS}
)
