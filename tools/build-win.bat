@echo off
rem ============================================================
rem  Dragon Radar 掌机端交叉编译 (Windows)
rem  目标: aarch64 Linux (TrimUI Brick Pro / tg5040)
rem
rem  依赖: zig (提供 aarch64-linux-gnu 工具链 + libc) 和 SDL2 头/库
rem  请先设置两个环境变量(或直接改下面的默认值):
rem     ZIG       zig.exe 的完整路径
rem     SDL_ROOT  含 include/ 和 lib/ 的 SDL2 目录
rem               需要: %SDL_ROOT%\include\SDL2\SDL.h
rem                     %SDL_ROOT%\lib\libSDL2.a (或 .so)
rem ============================================================
setlocal

if "%ZIG%"=="" set ZIG=zig
if "%SDL_ROOT%"=="" (
    echo [错误] 请先设置 SDL_ROOT 环境变量指向 SDL2 目录
    exit /b 1
)

if not exist "build" mkdir build

"%ZIG%" cc -target aarch64-linux-gnu.2.28 ^
    -I"%SDL_ROOT%\include" -L"%SDL_ROOT%\lib" -lSDL2 -lm -O2 ^
    -o "build\dragonradar" "device\main.c"

if errorlevel 1 (
    echo [失败] 编译出错
    exit /b 1
)

echo [成功] 输出 build\dragonradar
endlocal
