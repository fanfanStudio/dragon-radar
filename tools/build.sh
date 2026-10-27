#!/bin/sh
# Dragon Radar 掌机端交叉编译 (Linux/macOS)
# 依赖: zig (提供 aarch64-linux-gnu 工具链) + SDL2 头/库
#   export SDL_ROOT=/path/to/SDL2   # 需含 include/SDL2/SDL.h 与 lib/libSDL2.a
set -e

ZIG=${ZIG:-zig}
if [ -z "$SDL_ROOT" ]; then
    echo "请先 export SDL_ROOT=<SDL2 目录>"
    exit 1
fi

mkdir -p build
"$ZIG" cc -target aarch64-linux-gnu.2.28 \
    -I"$SDL_ROOT/include" -L"$SDL_ROOT/lib" -lSDL2 -lm -O2 \
    -o build/dragonradar device/main.c

echo "OK -> build/dragonradar"
