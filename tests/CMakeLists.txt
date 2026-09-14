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

# --- M2 jobs 层(headless 集成:真实 ffmpeg 现场合成/转码,缺失时 QSKIP) ---
add_executable(test_jobs test_jobs.cpp)
target_link_libraries(test_jobs PRIVATE jobs_lib Qt6::Test)
target_compile_definitions(test_jobs PRIVATE SPEC_DIR="${SPEC_DIR}")
add_test(NAME test_jobs COMMAND test_jobs)

# --- M3 分割执行(headless 集成:12s 密集关键帧视频 → 3×4s 分割全链路) ---
add_executable(test_splitter_run test_splitter_run.cpp)
target_link_libraries(test_splitter_run PRIVATE jobs_lib Qt6::Test)
add_test(NAME test_splitter_run COMMAND test_splitter_run)

# --- M3 队列语义(headless 集成:FIFO 串行/取消/移除) ---
add_executable(test_queue test_queue.cpp)
target_link_libraries(test_queue PRIVATE jobs_lib Qt6::Test)
target_compile_definitions(test_queue PRIVATE SPEC_DIR="${SPEC_DIR}")
add_test(NAME test_queue COMMAND test_queue)
