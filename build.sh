#!/bin/bash

# run in project root

# OS 감지 및 프리셋 설정
OS="$(uname)"
case "${OS}" in
    Linux*)     PRESET=linux-debug;;
    Darwin*)    PRESET=macos-debug;;
    *)          echo "지원되지 않는 OS입니다: ${OS}"; exit 1;;
esac

echo "Building for ${OS} using preset ${PRESET}..."

# Server 빌드
echo "Building server..."
cmake --preset ${PRESET} -S server
cmake --build server/build/${PRESET}

# Client 빌드
echo "Building client..."
cmake --preset ${PRESET} -S client
cmake --build client/build/${PRESET}

echo "Build finished!"
