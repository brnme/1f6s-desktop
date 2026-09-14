# packaging/ — 打包与分发

AppImage 打包脚本、静态 ffmpeg 引擎源矩阵、图标与许可文本都在这里。

## 用法

```bash
./packaging/make_appimage.sh          # 本地:podman 起 ubuntu:20.04 容器构建(glibc 2.31 基线)
./packaging/make_appimage.sh --in-container   # 已在 ubuntu:20.04 容器内时直接执行(CI 走这条)
```

产物:`dist/1f6s-desktop-<版本>-x86_64.AppImage` 与 `dist/SHA256SUMS`(dist/ 不入库)。
脚本幂等:每次清掉 build-appimage/ 与 AppDir 重建;网络变量可覆盖
(`APT_MIRROR`、`PIP_INDEX_URL`,本地默认阿里源,CI workflow 传官方源)。

流程:apt 装依赖 → pip 装 aqtinstall/cmake/ninja → aqt 装 Qt 6.2.4(qtbase+icu)
→ cmake Release 构建 → ctest → 组装 AppDir → linuxdeploy(-plugin-qt) 产 AppImage。

AppDir 布局(与 `src/main.cpp` 的 assets 兜底路径、`src/jobs/engine.cpp` 的
`findBesideApp` 引擎定位吻合):

```
usr/bin/1f6s-desktop        主程序(linuxdeploy 收集 Qt 动态库与插件)
usr/bin/ffmpeg, ffprobe     静态引擎,与主程序同目录(运行时优先命中)
usr/assets/                 spec/i18n(运行时 applicationDirPath()/../assets)
usr/share/doc/1f6s-desktop/ THIRD_PARTY.md + licenses/(GPLv3/LGPLv3/MIT 合规)
usr/share/applications/, usr/share/icons/   .desktop 与 hicolor 图标
```

## 文件

- `engines.json` — 静态 ffmpeg 引擎源矩阵(四平台,url/sha256/实测记录)。
  linux 双架构主源 eugeneware/ffmpeg-static(johnvansickle 静态构建镜像,
  2026-09-14 实测 7.0.2-static,x264/x265/aac 编码冒烟通过,153MB 较 BtbN 342MB
  瘦身 55%);BtbN 降为备选注记。darwin 主选 evermeet/osxexperts 不变。
- `engines/linux-amd64/` — 已校验固化的 linux 二进制(gitignore,仅本地);
  缺失时 make_appimage.sh 会按 engines.json 下载并做 sha256+encoders 校验。
- `make_icon.py` — 图标生成(纯 stdlib;深色圆角方块 + 1f6s 字样,与暗色 UI 同调)。
- `icon/` — 生成的 SVG 与 PNG(入库,打包直接用)。
- `1f6s-desktop.desktop`、`Info.plist.in`(macOS .app 用)、`licenses/`(许可文本)。

CI:`.github/workflows/build.yml`(linux 容器复用本脚本;macos 用 aqt+macdeployqt
出 dmg)。
