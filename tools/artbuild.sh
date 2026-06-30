#!/bin/bash
set -e

# Get the real physical path of the script (handle soft links)
SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"

# Locate the SDK root directory (since the script is in contest2026_052_xinfanzhichuang/tools/, the root is three levels up)
SDK_ROOT=$(cd "$SCRIPT_DIR/../../" && pwd)

if [  ! -d "$SCRIPT_DIR/../toolchain" ]; then
    echo "Info: Toolchain directory not found. Setting up toolchain..."
    chmod +x $SCRIPT_DIR/env.sh
    $SCRIPT_DIR/env.sh 
fi
# Install commit-msg hook automatically (solution 3 implementation)
# Ensure that Change-Id can be used in the contest2026_052_xinfanzhichuang project for code review
HOOKS_DIR="$SDK_ROOT/contest2026_052_xinfanzhichuang/.git/hooks"
REPO_HOOK="$SDK_ROOT/.repo/repo/hooks/commit-msg"

if [ -d "$HOOKS_DIR" ] && [ -f "$REPO_HOOK" ] && [ ! -f "$HOOKS_DIR/commit-msg" ]; then
    echo "Info: Installing commit-msg hook for contest2026_052_xinfanzhichuang..."
    cp "$REPO_HOOK" "$HOOKS_DIR/commit-msg"
    chmod +x "$HOOKS_DIR/commit-msg"
fi


# Show help message
function show_help() {
    echo "Usage: ./artbuild.sh [BOARD] [PROJECT] [OPTION]"
    echo ""
    echo "BOARD:"
    echo "  d13x_rivoride_m4-nor    Build for signal board"
    echo "PROJECT:"
    echo "  nsh         Build the project for NSH"
    echo "  nsh_cxx     Build the project for NSH with C++ 14.1 support"
    echo "  xts_opus    Build the project for XTS OPUS"
    echo "Options:"
    echo "  (none)      Build the project and pack the image (default)"
    echo "  rebuild     Clean (distclean), build the project, and pack the image"
    echo "  pack        Only pack the image"
    echo "  help        Show this help message"
    echo "  [custom]    Pass custom options directly to build.sh (e.g., clean, menuconfig)"
    echo ""
    echo "Example:"
    echo "  ./artbuild.sh d13x_rivoride_m4-nor nsh           # Build and pack for d13x chip (default)"
    echo "  ./artbuild.sh d13x_rivoride_m4-nor nsh build     # Build "
    echo "  ./artbuild.sh d13x_rivoride_m4-nor nsh rebuild   # Full clean, build, and pack for d13x chip"
    echo "  ./artbuild.sh d13x_rivoride_m4-nor nsh distclean # Only run distclean via build.sh for d13x chip"
    echo "  ./artbuild.sh d13x_rivoride_m4-nor nsh clean     # Only run clean via build.sh for d13x chip"
}


if [ $# -lt 2 ] || [ "$1" == "help" ] || [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
    show_help
    exit 1
fi

board=$1
pro=$2
if [ $# -ge 3 ]; then
opt=${@:3}
fi

# Define board configuration path for the project
BOARD_CONFIG_DIR="contest2026_052_xinfanzhichuang/boards/$board/configs/$pro"
CHIP_NAME=$(echo $BOARD_CONFIG_DIR | cut -d'/' -f3)
IMAGE_NAME="${CHIP_NAME}_v1.0.0.img"

echo "Building for board: $board, project: $pro, option: $opt"
echo  "BOARD_CONFIG_DIR: $BOARD_CONFIG_DIR, CHIP_NAME: $CHIP_NAME, IMAGE_NAME: $IMAGE_NAME"

# Check if help requests are made
if [ "$opt" == "help" ] || [ "$opt" == "-h" ] || [ "$opt" == "--help" ]; then
    show_help
    exit 0
fi
# If no option is provided, default to build + pack
if [ -z "$opt" ]; then
   echo "No option provided. Defaulting to: build + pack"
   cd "$SDK_ROOT"
   ./nuttx_build.sh $BOARD_CONFIG_DIR -j8
   if [ $? -ne 0 ]; then
       echo "Build failed. Exiting."
       exit 1
   fi
   cd "$SDK_ROOT/contest2026_052_xinfanzhichuang/pack"
   ./pack.sh $board
   cp ./prebuilt/$IMAGE_NAME "$SDK_ROOT/"
   exit 0
fi

# Add build option: execute build only
if echo "$opt" | grep -qw "build"; then
   echo "No option provided. Defaulting to: build only"
   cd "$SDK_ROOT"
   ./nuttx_build.sh $BOARD_CONFIG_DIR $opt
   exit 0
fi

# Add clean option: execute distclean with path recovery
if echo "$opt" | grep -qw "distclean"; then
   echo "Cleaning: distclean with path recovery"
   cd "$SDK_ROOT"
   ./nuttx_build.sh $BOARD_CONFIG_DIR distclean 
   # 1. Remove all conflicting files
   rm -rf nuttx/arch/risc-v/src/chip
   rm -rf nuttx/include/arch
   rm -f nuttx/Make.defs
   rm -rf contest2026_052_xinfanzhichuang/pack/prebuilt/*

fi

# Add rebuild option: execute distclean + build + pack
if echo "$opt" | grep -qw "rebuild"; then
   echo "Rebuilding: distclean + build + pack"
   cd "$SDK_ROOT"
   
   # 1. Remove all conflicting files
   rm -rf nuttx/arch/risc-v/src/chip
   rm -rf nuttx/include/arch
   rm -f nuttx/Make.defs

   # 2. Parse defconfig to get chip absolute path (adapt SDK_ROOT)
   DEFCONFIG="$SDK_ROOT/$BOARD_CONFIG_DIR/defconfig"
   if [ -f "$DEFCONFIG" ]; then
       CHIP_VAL=$(grep "^CONFIG_ARCH_CHIP_CUSTOM_DIR=" "$DEFCONFIG" | cut -d'=' -f2 | tr -d '"')
       CHIP_REL=${CHIP_VAL#../} 
       CHIP_ABS="$SDK_ROOT/$CHIP_REL"

       if [ -d "$CHIP_ABS" ]; then
           echo "Runing pre-link for chip: $CHIP_ABS"
           mkdir -p nuttx/arch/risc-v/src
           ln -sf "$CHIP_ABS" nuttx/arch/risc-v/src/chip
       else
           echo "Warning: Chip dir from defconfig not found: $CHIP_ABS"
       fi
   fi

   # 3. Parse board configuration to get Make.defs path (adapt SDK_ROOT)
   MAKEDEFS_SRC="$SDK_ROOT/$BOARD_CONFIG_DIR/../../scripts/Make.defs"

   if [ -f "$MAKEDEFS_SRC" ]; then
       ln -sf "$MAKEDEFS_SRC" "$SDK_ROOT/nuttx/Make.defs"
   fi

   ./nuttx_build.sh $BOARD_CONFIG_DIR/ distclean -j8
   ./nuttx_build.sh $BOARD_CONFIG_DIR/ -j8
   if [ $? -ne 0 ]; then
       echo "Build failed. Exiting."
       exit 1
   fi
   cd "$SDK_ROOT/contest2026_052_xinfanzhichuang/pack"
   ./pack.sh $board
   cp ./prebuilt/$IMAGE_NAME "$SDK_ROOT/"
   exit 0
fi

echo "Building with option: $opt"

if [ "$opt"x != "packx" ]; then
   cd "$SDK_ROOT"
   ./nuttx_build.sh $BOARD_CONFIG_DIR $opt
else
   cd "$SDK_ROOT/contest2026_052_xinfanzhichuang/pack"
   ./pack.sh $board
   cp ./prebuilt/$IMAGE_NAME "$SDK_ROOT/"
   echo "cp image completed,dir = $SDK_ROOT/"
fi
