# src/jobs/ — M2:ffmpeg 进程编排(引擎定位/单作业执行/队列),UI 无关。
# M3:新增分割执行器 splitrunner(流复制分段/后验/递归重分割/改名/点数)。
# 静态库供主程序与 headless 测试共用;只依赖 core_lib + Qt6::Core,可 headless 测试。
add_library(jobs_lib STATIC
    engine.cpp
    ffmpegjob.cpp
    jobmodel.cpp
    splitrunner.cpp
)
target_include_directories(jobs_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jobs_lib PUBLIC core_lib)  # core_lib 已 PUBLIC Qt6::Core
