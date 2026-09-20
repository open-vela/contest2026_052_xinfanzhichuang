#!/bin/bash

SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"

ARTBUILD_SCRIPT="$SCRIPT_DIR/tools/artbuild.sh"

if [ ! -f "$ARTBUILD_SCRIPT" ]; then
    echo "Error: $ARTBUILD_SCRIPT not found!"
    exit 1
fi

exec "$ARTBUILD_SCRIPT" "$@"