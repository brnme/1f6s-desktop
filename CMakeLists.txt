# 1f6s-desktop — 1f6s 在线压缩服务的桌面客户端
#
# 本机 Qt 通过 aqt 安装在固定路径,cmake/ninja 在 ~/tools/aqt/bin/。
# 配置示例(本机开发):
#
#   export PATH="$HOME/tools/aqt/bin:$PATH"
#   cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/home/hanlie/tools/Qt/6.2.4/gcc_64
#   cmake --build build
#
# 生产/CI 的 CMAKE_PREFIX_PATH 由预设工具链或 -D 覆盖,勿写死在代码里。
cmake_minimum_required(VERSION 3.16)
project(1f6s-desktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network Test)
# 注:本机 GL 只有运行库、缺 -dev 头文件/符号链接,find_package(Qt6) 需显式提示
# (免装依赖的变通,见 git 提交记录):
#   -DOPENGL_INCLUDE_DIR=$HOME/tools/glstub/include
#   -DOPENGL_GLX_INCLUDE_DIR=$HOME/tools/glstub/include
#   -DOPENGL_opengl_LIBRARY=$HOME/tools/glstub/lib/libOpenGL.so
#   -DOPENGL_glx_LIBRARY=$HOME/tools/glstub/lib/libGLX.so
#   -DOPENGL_gl_LIBRARY=$HOME/tools/glstub/lib/libGL.so
# (~/tools/glstub 内是对 /usr/lib/x86_64-linux-gnu/lib{GL,GLX,OpenGL}.so.0 的符号链接)

# M1:核心库(规范解析/命令构造/探测/预检/分割规划),主程序与 tests 共用。
add_subdirectory(src/core)

add_executable(1f6s-desktop
    src/main.cpp
    src/app/mainwindow.cpp
    # M2: src/jobs/*(ffmpeg 作业调度)由后续里程碑填充
)

target_include_directories(1f6s-desktop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(1f6s-desktop PRIVATE Qt6::Widgets Qt6::Network core_lib)

enable_testing()
add_subdirectory(tests)
