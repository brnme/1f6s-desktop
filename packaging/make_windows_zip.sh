#!/usr/bin/env bash
# make_windows_zip.sh — CI(Windows runner,git-bash)组装便携 zip:
#   取 VS Release 构建 exe → windeployqt 部署 Qt 与 VC 运行库(--compiler-runtime,
#   免装 VC++ Redistributable)→ 内置静态引擎(exe 同级,engine.cpp 按名定位)
#   → assets(exe 同级,布局见 i18n::assetCandidates)与再分发合规文档
#   → offscreen 冒烟(拦"包里缺 assets/引擎"类问题)→ Compress-Archive 打 zip。
# 本地无 Windows 环境,与 mac 组装步骤同定位:只在 CI 跑,仓库内留档可审计。
# 用法: make_windows_zip.sh <build-dir> <qt-root> <engines-dir> <out-dist-dir>
#   <build-dir>   VS 生成器构建树(exe 在 Release/ 子目录;Ninja 布局也能找到)
#   <qt-root>     如 $HOME/Qt/6.2.4/msvc2019_64(windeployqt 与 qoffscreen.dll 来源)
#   <engines-dir> 含 ffmpeg.exe/ffprobe.exe 的目录(下载已校验,见 build.yml)
#   <out-dist-dir> 产物 zip 输出目录
set -euo pipefail

BUILD_DIR=$1
QT_ROOT=$2
ENGINES_DIR=$3
OUT_DIST=$4

VER=$(sed -n 's/^project(1f6s-desktop VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
[ -n "$VER" ] || { echo "无法从 CMakeLists.txt 解析版本号" >&2; exit 1; }

# VS 生成器产物在 Release/ 下;兼容 Ninja 的平铺布局
EXE="$BUILD_DIR/Release/1f6s-desktop.exe"
[ -f "$EXE" ] || EXE="$BUILD_DIR/1f6s-desktop.exe"
[ -f "$EXE" ] || { echo "找不到 1f6s-desktop.exe(找过 $BUILD_DIR{,/Release})" >&2; exit 1; }

PKG=app/1f6s-desktop-$VER-win64
rm -rf app
mkdir -p "$PKG" "$OUT_DIST"

cp "$EXE" "$PKG/1f6s-desktop.exe"

# Qt + VC 运行库部署:
#   --compiler-runtime  随包拷 VC 运行库 DLL(便携;Win10+ 自带 UCRT,缺的是 vcruntime/msvcp140)
#   --no-translations   不带 Qt 翻译(应用自带 i18n,不用 Qt 自身翻译)
#   --no-opengl-sw      不带软件 OpenGL 兜底(Widgets 光栅绘制用不上,省 ~20MB)
"$QT_ROOT/bin/windeployqt.exe" --release --compiler-runtime --no-translations \
    --no-opengl-sw "$PKG/1f6s-desktop.exe"

# 内置静态引擎:exe 同级(运行时 findBesideApp 按 ffmpeg.exe/ffprobe.exe 定位)
cp "$ENGINES_DIR/ffmpeg.exe" "$ENGINES_DIR/ffprobe.exe" "$PKG/"

# assets:exe 同级(第三个候选目录,见 i18n::assetCandidates)
cp -r assets "$PKG/assets"

# 再分发合规:THIRD_PARTY.md、许可文本、引擎指纹(mac 包同款结构)
mkdir -p "$PKG/doc/licenses"
cp THIRD_PARTY.md "$PKG/doc/"
cp packaging/licenses/* "$PKG/doc/licenses/"
( cd "$PKG" && sha256sum ffmpeg.exe ffprobe.exe > doc/engines-windows.sha256 )

# offscreen 冒烟:qoffscreen.dll 只在冒烟时放入,验完即删(不随包分发)
cp "$QT_ROOT/plugins/platforms/qoffscreen.dll" "$PKG/platforms/"
bash packaging/smoke_launch.sh "$PWD/$PKG/1f6s-desktop.exe" 90
rm "$PKG/platforms/qoffscreen.dll"

# Compress-Archive 保留顶层目录(解压得到单个文件夹);路径转 Windows 形式
powershell.exe -NoProfile -Command \
    "Compress-Archive -Path '$(cygpath -w "$PWD/$PKG")' -DestinationPath '$(cygpath -w "$PWD/$OUT_DIST/1f6s-desktop-$VER-win64.zip")' -Force"

ls -la "$OUT_DIST"
