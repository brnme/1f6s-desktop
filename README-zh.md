# 1f6s Desktop

[![build](https://github.com/brnme/1f6s-desktop/actions/workflows/build.yml/badge.svg)](https://github.com/brnme/1f6s-desktop/actions/workflows/build.yml)

[English](README.md) | [中文](README-zh.md)

[1f6s 压缩倡议](https://github.com/brnme/1f6s)的桌面客户端：在本地把超长录屏压成能塞进邮件附件、能过网课平台上传限制的小体积 MP4。ffmpeg 以子进程方式在本地运行，录像全程不离开电脑。

压缩参数与[网站端](https://1f6s.com)完全一致：八级规范从网站仓同步到 `assets/spec/`，命令构造由网站仓 Python 代码生成的 261 组黄金向量逐 token 对拍验证——同样输入，桌面端与网站端构造出的 ffmpeg 命令完全相同。

## 功能亮点

- **三种选参方式**
  - *等级*——直接选 L1–L8 八级之一（默认 L4）。
  - *场景*——按视频用途选（邮件、即时通讯、培训、网课 MOOC、演示、合规存档、离线观看、平台上传、归档），自动套用对应等级。
  - *微调*——在基线等级上改分辨率（720p/480p/360p）、抽帧间隔、灰度、音频码率。
- **批量队列**——一次加多个文件，每行实时显示从 ffmpeg 解析的进度；排队中可移除、运行中可取消。输出写在源文件同目录 `<stem>_1f6s.mp4`，永不覆盖原文件。
- **分割**——把超大录像按服务上传档位（2048 / 5120 / 8192 MB，或自定义）切段，并做超时预检，提前拒绝注定转不完的输入。
- **English 与中文界面**，设置页可切换（另有低优先级、线程上限、预设速度、自定义 ffmpeg/ffprobe 路径）。
- **规范更新检查**——关于页静默询问 `1f6s.com` 是否发布了新版等级规范。

## 八级规范速览

| 等级 | 名称 | 说明 |
| --- | --- | --- |
| L1 | 标准级 | 彩色，480p；99 分钟约 30–35 MB |
| L2 | 黑白级 | 黑白画面 |
| L3 | 高压缩级 | |
| L4 | 智能调优级 | 默认；99 分钟约 16–19 MB |
| L5 | 音频极限级 | |
| L6 | 长间隔级 | 360p，每 10 秒保留 1 帧 |
| L7 | 精确体积级 | 两遍编码，锁定目标体积（1–500 MB） |
| L8 | H.265 极限级 | 99 分钟约 6–8 MB |

权威定义在 `assets/spec/levels.json`，上表只是速览。

## 下载

v0.1.0 三平台预构建包在 [Releases](https://github.com/brnme/1f6s-desktop/releases) 页（附总 `SHA256SUMS`）；[网站下载页](https://1f6s.com/zh/download)提供相同链接。

| 包名 | 基线 |
| --- | --- |
| `1f6s-desktop-0.1.0-x86_64.AppImage` | Linux，glibc 2.31+（Ubuntu 20.04+） |
| `1f6s-desktop-0.1.0-x86_64.dmg` | macOS 10.15+（Intel） |
| `1f6s-desktop-0.1.0-win64.zip` | Windows 10+（x64），便携版——解压即用 |

Windows zip 与 Linux AppImage 均内置 ffmpeg/ffprobe，macOS dmg 同样内置。二进制未做代码签名：macOS 首次启动请右键 →「打开」；Windows 若 SmartScreen 弹窗，点「更多信息 → 仍要运行」。

## 从源码构建

需要 Qt 6.2+、CMake ≥ 3.16、Ninja。ffmpeg/ffprobe 需在 `PATH` 上（或在应用设置页填明确路径）。

```bash
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/Qt/6.2.4/gcc_64
cmake --build build
ctest --test-dir build   # 可选；缺引擎时依赖 ffmpeg 的用例自动跳过
./build/1f6s-desktop
```

从网站仓同步规范、重新生成黄金向量的工具：

```bash
tools/sync_spec.sh     # 从网站仓同步等级/预检/档位参数 → assets/spec/
tools/gen_vectors.py   # 重新生成黄金向量 → vectors/vectors.json
```

## 第三方组件

- **Qt 6.2**——LGPL v3，动态链接，未修改。
- **nlohmann/json**——MIT，以头文件形式内置于 `third_party/`。
- **FFmpeg**——GPL 构建，以独立子进程调用（无库级链接）。各平台来源与 SHA-256 记录在 `packaging/engines.json`；源码链接与许可文本见 [THIRD_PARTY.md](THIRD_PARTY.md)。

本仓库自身代码的许可证尚未确定。

## 相关仓库

- [brnme/1f6s](https://github.com/brnme/1f6s)——压缩倡议与规范。
- [brnme/1f6s-service-site](https://github.com/brnme/1f6s-service-site)——网站端服务，等级参数的唯一真相源。
- [1f6s.com](https://1f6s.com)——与本客户端共用规范的在线服务。
