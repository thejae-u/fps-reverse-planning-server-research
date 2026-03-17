#!/bin/bash

# run in proto directory

# vcpkg에서 설치한 protoc 경로 탐색
VCPKG_PROTOC=""

# Detect OS
OS="$(uname)"

if [[ "${OS}" == "Darwin" ]]; then
  if [[ -f "../server/build/macos-debug/vcpkg_installed/arm64-osx/tools/protobuf/protoc" ]]; then
    VCPKG_PROTOC="../server/build/macos-debug/vcpkg_installed/arm64-osx/tools/protobuf/protoc"
  elif [[ -f "../server/build/macos-debug/vcpkg_installed/x64-osx/tools/protobuf/protoc" ]]; then
    VCPKG_PROTOC="../server/build/macos-debug/vcpkg_installed/x64-osx/tools/protobuf/protoc"
  fi
elif [[ "${OS}" == "Linux" ]]; then
  if [[ -f "../server/build/linux-debug/vcpkg_installed/x64-linux/tools/protobuf/protoc" ]]; then
    VCPKG_PROTOC="../server/build/linux-debug/vcpkg_installed/x64-linux/tools/protobuf/protoc"
  fi
fi

if [[ -z "$VCPKG_PROTOC" ]]; then
  VCPKG_PROTOC=$(which protoc)
  if [[ -z "$VCPKG_PROTOC" ]]; then
    echo "Error: protoc not found. Please build the project first or install protobuf."
    exit 1
  fi
  echo "Warning: vcpkg protoc not found. Using system protoc: $VCPKG_PROTOC"
else
  echo "Using vcpkg protoc: $VCPKG_PROTOC"
fi

# proto 소스 디렉토리
PROTO_SRC="." 

# 서버 헤더 및 소스 경로
SERVER_HEADER="../server/src/header"
SERVER_SOURCE="../server/src/sources"

# 클라이언트 헤더 및 소스 경로
CLIENT_HEADER="../client/src/header"
CLIENT_SOURCE="../client/src/sources"

# proto 파일 찾기 (glob)
shopt -s globstar

# 각 proto 파일 컴파일
for proto in "$PROTO_SRC"/**/*.proto; do
  [ -e "$proto" ] || continue
  
  # 서버용 생성
  "$VCPKG_PROTOC" --proto_path="$PROTO_SRC" \
         --cpp_out="$SERVER_HEADER" \
         "$proto"
  
  # .cc 파일을 sources로 이동
  if ls "$SERVER_HEADER"/*.pb.cc 1> /dev/null 2>&1; then
    mv "$SERVER_HEADER"/*.pb.cc "$SERVER_SOURCE/"
  fi
  
  # 클라이언트용 생성
  "$VCPKG_PROTOC" --proto_path="$PROTO_SRC" \
         --cpp_out="$CLIENT_HEADER" \
         "$proto"
  
  # .cc 파일을 sources로 이동
  if ls "$CLIENT_HEADER"/*.pb.cc 1> /dev/null 2>&1; then
    mv "$CLIENT_HEADER"/*.pb.cc "$CLIENT_SOURCE/"
  fi
  
  echo "Generated headers and sources from $proto"
done

echo "All proto files generated!"
