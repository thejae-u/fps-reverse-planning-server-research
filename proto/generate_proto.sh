#!/bin/zsh

# Script directory
SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR"

# Protoc discovery
VCPKG_PROTOC=""
if [[ -f "../server/build/vcpkg_installed/arm64-osx/tools/protobuf/protoc" ]]; then
  VCPKG_PROTOC="../server/build/vcpkg_installed/arm64-osx/tools/protobuf/protoc"
elif [[ -f "../server/build/vcpkg_installed/x64-osx/tools/protobuf/protoc" ]]; then
  VCPKG_PROTOC="../server/build/vcpkg_installed/x64-osx/tools/protobuf/protoc"
elif [[ -f "../client/build/vcpkg_installed/arm64-osx/tools/protobuf/protoc" ]]; then
  VCPKG_PROTOC="../client/build/vcpkg_installed/arm64-osx/tools/protobuf/protoc"
elif [[ -f "../client/build/vcpkg_installed/x64-osx/tools/protobuf/protoc" ]]; then
  VCPKG_PROTOC="../client/build/vcpkg_installed/x64-osx/tools/protobuf/protoc"
else
  VCPKG_PROTOC=$(which protoc)
fi

if [[ -z "$VCPKG_PROTOC" ]]; then
  echo "Error: protoc not found. Please install protobuf or ensure vcpkg has it."
  exit 1
fi

echo "Using protoc: $VCPKG_PROTOC"

# proto source directory
PROTO_SRC="." 

# Server paths
SERVER_HEADER="../server/src/header"
SERVER_SOURCE="../server/src/sources"

# Client paths
CLIENT_HEADER="../client/src/header"
CLIENT_SOURCE="../client/src/sources"

# Ensure directories exist
mkdir -p "$SERVER_HEADER" "$SERVER_SOURCE" "$CLIENT_HEADER" "$CLIENT_SOURCE"

# Find proto files
PROTO_FILES=(*.proto)

if [[ ${#PROTO_FILES[@]} -eq 0 ]]; then
  echo "No .proto files found in $PROTO_SRC"
  exit 1
fi

# Compile each proto file
for proto in "${PROTO_FILES[@]}"; do
  echo "Processing $proto..."
  
  # Generate for Server
  "$VCPKG_PROTOC" --proto_path="$PROTO_SRC" \
         --cpp_out="$SERVER_HEADER" \
         "$proto"
  
  # Move .pb.cc to sources
  # Note: Some protoc versions might generate .pb.cc in the same dir as .pb.h
  # We use find to be safe and handle potential subdirectories if any
  find "$SERVER_HEADER" -name "*.pb.cc" -exec mv {} "$SERVER_SOURCE/" \;
  
  # Generate for Client
  "$VCPKG_PROTOC" --proto_path="$PROTO_SRC" \
         --cpp_out="$CLIENT_HEADER" \
         "$proto"
  
  # Move .pb.cc to sources
  find "$CLIENT_HEADER" -name "*.pb.cc" -exec mv {} "$CLIENT_SOURCE/" \;
  
  echo "Generated headers and sources from $proto"
done

echo "All proto files generated!"
