# 1f6s-desktop

[1f6s](https://github.com/brnme/1f6s)(1帧6秒压缩倡议)在线压缩服务的桌面客户端:本地调用 ffmpeg,把超长录屏按八级规范(L1-L8)压成小体积 MP4,参数与网站端共用同一份规范表(`assets/spec/`,自网站仓同步),并以黄金向量(`vectors/vectors.json`)保证两端命令构造逐 token 一致。

技术栈:C++17 + Qt 6.2 + CMake/Ninja。发布基线 Linux glibc 2.31 / macOS 10.14。

## 快速开始

```bash
# 本机已通过 aqt 安装 Qt 6.2.4 与 cmake/ninja
export PATH="$HOME/tools/aqt/bin:$PATH"
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/home/hanlie/tools/Qt/6.2.4/gcc_64
cmake --build build
./build/1f6s-desktop
```

## 工具链脚本

```bash
tools/sync_spec.sh     # 从网站仓同步规范参数 → assets/spec/(levels/precheck/档位上限)
tools/gen_vectors.py   # 用网站仓 venv 重新生成黄金向量 → vectors/vectors.json
```

里程碑:M0 骨架(当前) → M1 规范解析+对拍测试 → M2 ffmpeg 作业调度 → M3 打包 → M4 i18n/ polish → M5 CI 发布。
