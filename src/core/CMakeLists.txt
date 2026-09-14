# src/core/ — M1 核心库:规范解析/命令构造/探测/预检/分割规划。
# 纯逻辑、不依赖 GUI;编成静态库供 tests(黄金向量对拍)与未来 app 共用。
add_library(core_lib STATIC
    spec.cpp
    encode.cpp
    probe.cpp
    precheck.cpp
    splitter.cpp
)
target_include_directories(core_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src            # → "core/spec.h" 等
    ${CMAKE_SOURCE_DIR}/third_party    # → <nlohmann/json.hpp>(vendored)
)
target_link_libraries(core_lib PUBLIC Qt6::Core)  # probe 用 QProcess;其余保持可移植
