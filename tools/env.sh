#!/bin/bash
set -e
TOOLCHAIN_URL="https://occ-oss-prod.oss-cn-hangzhou.aliyuncs.com/resource//1751370399722/Xuantie-900-gcc-elf-newlib-x86_64-V3.2.0-20250627.tar.gz"
# Get the real physical path of the script (handle soft links)
SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"

# Locate the SDK root directory (since the script is in vendor/artinchip/tools/, the root is three levels up)
TOOLCHAIN_DIR=$(cd "$SCRIPT_DIR/../" && pwd)
TOOLCHAIN_TAR=$(basename "$TOOLCHAIN_URL")

TOOLCHAIN_DIR="$TOOLCHAIN_DIR/toolchain"
if [ ! -d "$TOOLCHAIN_DIR" ]; then
    mkdir -p "$TOOLCHAIN_DIR"
fi
        
cd "$TOOLCHAIN_DIR"

if [ ! -f "$TOOLCHAIN_TAR" ]; then
    echo "Downloading toolchain: $TOOLCHAIN_URL"
    if ! curl -L -o "$TOOLCHAIN_TAR" "$TOOLCHAIN_URL"; then
        echo "Error: Failed to download toolchain"
        exit 1
    fi
fi

if [ ! -d "bin" ]; then
    echo "Extracting toolchain..."
    if ! tar -xzf "$TOOLCHAIN_TAR" --strip-components=1; then
        echo "Error: Failed to extract toolchain"
        exit 1
    fi
fi

echo "Toolchain setup completed."