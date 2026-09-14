# M1 测试:黄金向量对拍(核心验收)+ 各核心模块单元/集成测试。
# 全部注册进 ctest;core_lib 来自 src/core/。
set(SPEC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets/spec")
set(VECTORS_JSON "${CMAKE_CURRENT_SOURCE_DIR}/../vectors/vectors.json")

# --- 黄金向量对拍:261 case(189 commands + 24 error + 48 estimate) ---
add_executable(test_vectors test_vectors.cpp)
target_link_libraries(test_vectors PRIVATE core_lib Qt6::Test)
target_compile_definitions(test_vectors PRIVATE
    VECTORS_JSON="${VECTORS_JSON}"
    SPEC_DIR="${SPEC_DIR}")
add_test(NAME test_vectors COMMAND test_vectors)

# --- 分割规划(纯函数手算用例) ---
add_executable(test_splitter test_splitter.cpp)
target_link_libraries(test_splitter PRIVATE core_lib Qt6::Test)
add_test(NAME test_splitter COMMAND test_splitter)

# --- 预检因子(真实 assets/spec/precheck_factors.json + 构造用例) ---
add_executable(test_precheck test_precheck.cpp)
target_link_libraries(test_precheck PRIVATE core_lib Qt6::Test)
target_compile_definitions(test_precheck PRIVATE SPEC_DIR="${SPEC_DIR}")
add_test(NAME test_precheck COMMAND test_precheck)

# --- ffprobe 探测(集成:系统 ffmpeg 现场合成,缺失时 QSKIP) ---
add_executable(test_probe test_probe.cpp)
target_link_libraries(test_probe PRIVATE core_lib Qt6::Test)
add_test(NAME test_probe COMMAND test_probe)
