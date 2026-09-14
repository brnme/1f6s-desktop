# THIRD_PARTY.md — 第三方组件与再分发合规说明

本仓(1f6s-desktop)分发的二进制涉及以下第三方组件。对外再分发包时,
本文件须随包附带,且各组件的许可文本/源码链接要求见对应小节。

## 1. Qt 6.2(Qt Toolkit)

- 许可:**GNU LGPL v3**(本仓按动态链接方式使用,`find_package(Qt6)` 链接
  共享库,未静态链入任何闭源发行物)。
- 来源:https://www.qt.io/ (官方源码 https://download.qt.io/official_releases/qt/)
- 合规要点(LGPL v3):
  - 必须允许用户替换/重链 Qt 库(动态链接 + 不修改 Qt 本体即满足);
  - 分发时附 LGPL v3 许可文本(打包脚本负责放入 `licenses/`);
  - 本仓对 Qt 未做任何修改。

## 2. nlohmann/json(header-only)

- 许可:**MIT License**,(c) 2013-2022 Niels Lohmann。
- 来源:https://github.com/nlohmann/json
- 随源码 vendored 于 `third_party/nlohmann/json.hpp`(仅头文件,随程序分发时
  需附其 MIT 许可声明,打包脚本负责)。

## 3. FFmpeg(随包分发的静态引擎,GPL 构建)

- 许可:各引擎均为 **GPL v3 构建**(`--enable-gpl`;部分 `--enable-version3`)。
- 调用形态:以**独立子进程**调用(命令行传参),主程序与 ffmpeg 之间只有
  进程边界,无库级链接;GPL 不因此传染主程序,但**再分发 ffmpeg 二进制时
  必须履行 GPLv3 义务**:随包提供/指明对应源码链接与完整许可文本,并附
   Changes 说明(未修改则注明原样分发)。打包脚本负责,见 `packaging/engines.json`。
- 各平台分发源(即对应源码可获取处,矩阵与校验记录见 `packaging/engines.json`):
  - linux-amd64:https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.1-latest-linux64-gpl-8.1.tar.xz
    (ffmpeg.org 官方下载页推荐的 Linux 静态构建源;GPL v3,含 libx264/libx265/原生 aac)
  - linux-arm64:https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.1-latest-linuxarm64-gpl-8.1.tar.xz(同上,arm64)
  - darwin-amd64(Intel Mac):https://evermeet.cx/ffmpeg/(release/zip 与 ffprobe/zip;源码与 GPG 签名同站提供)
  - darwin-arm64(Apple Silicon):https://www.osxexperts.net/(ffmpeg9arm.zip / ffprobe9arm.zip;页面公布逐文件 SHA256。注意该站标注 "for educational purposes only",商用再分发前需法务确认,见 engines.json 记录)
- FFmpeg 本体源码:https://git.ffmpeg.org/ffmpeg.git ;许可文本
  https://www.gnu.org/licenses/gpl-3.0.html
- 本仓 `packaging/engines/linux-amd64/` 内固化的二进制为上述 BtbN 资产的
  原样抽取产物(未修改),sha256 记录在 `packaging/engines.json`。

## 4. 1f6s 压缩规范(spec)

- `assets/spec/*.json` 由网站仓 1f6s-service-site 的 `spec/` 同步而来
  (`tools/sync_spec.sh`),参数唯一真相源在网站仓;随包分发时视为本项目
  数据文件,不属于第三方代码。

## 汇总:分发包必须附带

1. 本文件;
2. LGPL v3 许可文本(Qt)、MIT 许可声明(nlohmann/json)、GPL v3 许可文本(ffmpeg);
3. 上表各 ffmpeg 引擎的源码获取链接(或等价书面源码要约)。
