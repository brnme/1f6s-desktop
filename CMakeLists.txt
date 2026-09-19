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
project(1f6s-desktop VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# MSVC:源码全 UTF-8(中文注释/文案),默认按本地代码页解码会报 C4819/乱码
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

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
# M2:进程编排(引擎定位/ffmpeg 作业/队列),UI 无关,tests 亦用。
add_subdirectory(src/jobs)

add_executable(1f6s-desktop
    src/main.cpp
    src/app/i18n.cpp
    src/app/mainwindow.cpp
    src/app/compresspage.cpp
    src/app/splitpage.cpp
    src/app/settingspage.cpp
    src/app/aboutpage.cpp
)

target_include_directories(1f6s-desktop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# 开发态 assets 路径;打包态运行时回退见 i18n::assetCandidates(src/app/i18n.cpp)。
# APP_VERSION:项目版本,关于页展示(i18n.cpp 与 aboutpage.cpp 也读取该定义)。
target_compile_definitions(1f6s-desktop PRIVATE
    ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(1f6s-desktop PRIVATE Qt6::Widgets Qt6::Network core_lib jobs_lib)

# Windows:exe 嵌入图标与版本资源;WIN32_EXECUTABLE 用 GUI 子系统(不弹控制台黑窗)
if(WIN32)
    enable_language(RC)
    configure_file(packaging/1f6s-desktop.rc.in ${CMAKE_CURRENT_BINARY_DIR}/1f6s-desktop.rc @ONLY)
    target_sources(1f6s-desktop PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/1f6s-desktop.rc)
    set_target_properties(1f6s-desktop PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

enable_testing()
add_subdirectory(tests)
