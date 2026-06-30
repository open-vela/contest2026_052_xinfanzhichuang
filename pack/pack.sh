#!/bin/bash

CUR_DIR=$(pwd)
SDK_ROOT=$(realpath $CUR_DIR/../..)

# 1. auto infer board name and chip name from .config
# get board name from .config (example: d12x_demo68-nor)
BOARD_NAME=$1
if [ -z "$BOARD_NAME" ]; then
    if [ -f $SDK_ROOT/nuttx/.config ]; then
        BOARD_NAME=$(grep CONFIG_ARCH_BOARD_CUSTOM_NAME $SDK_ROOT/nuttx/.config | cut -d'=' -f2 | tr -d '"')
    fi
fi
# get chip name from .config (example: d12x)
CHIP_NAME=$2
if [ -z "$CHIP_NAME" ]; then
    if [ -f $SDK_ROOT/nuttx/.config ]; then
        if grep -q "CONFIG_ARCH_CHIP_D12X=y" $SDK_ROOT/nuttx/.config; then
            CHIP_NAME="d12x"
        else
            CHIP_NAME=$(grep -E "CONFIG_ARCH_CHIP_[A-Z0-9]+" $SDK_ROOT/nuttx/.config | grep "=y" | head -n 1 | cut -d'_' -f4 | tr '[:upper:]' '[:lower:]' | cut -d'=' -f1)
        fi
    fi
fi

# 2. default fallback
BOARD_NAME=${BOARD_NAME:-demo68-nor}
CHIP_NAME=${CHIP_NAME:-d13x}

echo ">>> Packing for Board: $BOARD_NAME"

BOARD_DIR=$SDK_ROOT/contest2026_052_xinfanzhichuang/boards/$BOARD_NAME
PRJ_OUT=$(realpath $CUR_DIR/prebuilt)
TOOLDIR=$SDK_ROOT/contest2026_052_xinfanzhichuang/tools/scripts
TOOLCHAIN_DIR=${SDK_ROOT}/contest2026_052_xinfanzhichuang/toolchain/bin/riscv64-unknown-elf-
OBJ_COPY=${TOOLCHAIN_DIR}objcopy
ELF_SIZE=${TOOLCHAIN_DIR}size

echo ">>> Starting $CHIP_NAME image packing..."

# 2. sync latest config files and resources from boards directory
if [ -d $BOARD_DIR/pack ]; then
    echo ">>> Syncing board specific pack config from $BOARD_DIR/pack"
    cp -rv $BOARD_DIR/pack/* $PRJ_OUT/
fi

# 3. prepare base files (ELF/Manifest) and convert to nuttx.bin
if [ -f $SDK_ROOT/nuttx/nuttx.elf ]; then
    cp -f $SDK_ROOT/nuttx/nuttx.elf $PRJ_OUT/$CHIP_NAME.elf
    cp -f $SDK_ROOT/nuttx/nuttx.manifest $PRJ_OUT/nuttx.manifest

    # convert latest ELF to nuttx.bin to use in .its
    echo ">>> Generating nuttx.bin from $CHIP_NAME.elf..."
    $OBJ_COPY -S -O binary -R .note -R .note.gnu.build-id -R .comment $PRJ_OUT/$CHIP_NAME.elf $PRJ_OUT/nuttx.bin

    # display ELF size in console
    $ELF_SIZE $PRJ_OUT/$CHIP_NAME.elf
fi
# 4. prepare system partition file list (SDK resource installation)
python3 $TOOLDIR/gen_partition_file_list.py -c $PRJ_OUT/image_cfg.json -o $PRJ_OUT/partition_file_list.h

# 5. prepare file system content (SDK resource installation)
python3 $TOOLDIR/fsinstall.py --sdkout $PRJ_OUT --clean rodata/,data/
if [ -d $SDK_ROOT/contest2026_052_xinfanzhichuang/boards/$BOARD_NAME/pack/rodata/ ]; then
python3 $TOOLDIR/fsinstall.py --sdkout $PRJ_OUT --src $SDK_ROOT/contest2026_052_xinfanzhichuang/boards/$BOARD_NAME/pack/rodata/ --dst rodata/
python3 $TOOLDIR/fsinstall.py --sdkout $PRJ_OUT --src $SDK_ROOT/contest2026_052_xinfanzhichuang/boards/$BOARD_NAME/pack/data/ --dst data/
else
python3 $TOOLDIR/fsinstall.py --sdkout $PRJ_OUT --src $SDK_ROOT/contest2026_052_xinfanzhichuang/pack/resources/rodata/ --dst rodata/
python3 $TOOLDIR/fsinstall.py --sdkout $PRJ_OUT --src $SDK_ROOT/contest2026_052_xinfanzhichuang/pack/resources/data/ --dst data/
fi
# 6. generate file system image (FATFS and LittleFS)
pushd $PRJ_OUT > /dev/null
python3 $TOOLDIR/makefatfs.py --fullpart --volab default --cluster 8 --sector 512 --tooldir $TOOLDIR --inputdir rodata --outfile $PRJ_OUT/rodata.fatfs
python3 $TOOLDIR/makelittlefs.py --pagesize 256 --blocksize 4096 --tooldir $TOOLDIR --inputdir data/ --outfile $PRJ_OUT/data.lfs
popd > /dev/null

# 7. generate private resources (PBP/Partition combination)
python3 $TOOLDIR/mk_private_resource.py -v -l $PRJ_OUT/pbp_cfg.json,$PRJ_OUT/partition.json -o $PRJ_OUT/pbp_cfg.bin

# 8. generate final .img image from core generation script
# image filename is dynamically generated
IMG_NAME="${BOARD_NAME}_v1.0.0.img"
python3 $TOOLDIR/mk_image.py -v -c $PRJ_OUT/image_cfg.json -d $PRJ_OUT

echo ">>> Image generated successfully at: $PRJ_OUT/$IMG_NAME"





