#!/usr/bin/env bash
# 1f6s-desktop Linux AppImage 打包脚本 —— 本地与 CI 同一入口。
#
# 用法(两种模式,核心构建逻辑完全一致):
#   1) 宿主直接跑(需 podman,默认在 ubuntu:20.04 容器内构建,保证 glibc 2.31 基线):
#        ./packaging/make_appimage.sh
#   2) 容器内执行(供 CI `container: ubuntu:20.04` 或手工进容器后调用):
#        ./packaging/make_appimage.sh --in-container
#
# 产物: dist/1f6s-desktop-<版本>-x86_64.AppImage 与 dist/SHA256SUMS
#
# 实现注意:仓库可能位于 NTFS(fuseblk)等不支持 chown/chmod 的挂载上,
# 容器内一律把源码拷到容器文件系统 /work 构建完整 POSIX 语义,仅把产物
# (AppImage/SHA256SUMS,普通 cp,不 chmod)拷回 /src/dist。
#
# 可用环境变量(两种模式通用;CI 网络好,本地按需覆盖):
#   APT_MIRROR      ubuntu apt 源主机,默认 mirrors.aliyun.com(本地网络);CI 设 archive.ubuntu.com
#   PIP_INDEX_URL   pip 源,默认阿里镜像(本地网络);CI 设 https://pypi.org/simple
#   QT_VERSION      Qt 版本,默认 6.2.4
#   SKIP_TESTS=1    跳过 ctest(不建议;CI 里从不跳)
#   PODMAN_IMAGE    宿主模式容器镜像,默认 docker.m.daocloud.io/library/ubuntu:20.04;
#                   本地可指向预装好 Qt 的加速镜像(如 podman commit 出的 1f6s-desktop-build:qt6.2.4)
set -euo pipefail

SCRIPT_PATH="$(readlink -f "$0")"
ROOT="$(dirname "$SCRIPT_PATH")/.."
WORK=/work          # 容器内工作区(容器文件系统,POSIX 语义完整)

QT_VERSION="${QT_VERSION:-6.2.4}"
APP_VERSION="$(sed -n 's/^project(1f6s-desktop VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[ -n "$APP_VERSION" ] || { echo "无法从 CMakeLists.txt 解析版本号" >&2; exit 1; }
ARCH=x86_64
APPIMAGE_NAME="1f6s-desktop-${APP_VERSION}-${ARCH}.AppImage"

log() { echo "==> $*"; }

# 可断点续传的下载(弱网环境:GitHub 直连常断流,失败重试最多 5 次;
# 注:ubuntu 20.04 curl 7.68 无 --retry-all-errors,靠外层循环+断点续传兜底)
fetch() { # fetch <url> <输出文件>
    local url="$1" out="$2" i
    for i in 1 2 3 4 5; do
        curl -sSL -C - -o "$out" "$url" && return 0
        echo "    下载失败(第 $i 次): $url" >&2
        sleep 3
    done
    return 1
}

# ---------------------------------------------------------------------------
# 容器内构建主流程
# ---------------------------------------------------------------------------
build_in_container() {
    echo "[1/7] apt 安装构建依赖 (APT_MIRROR=${APT_MIRROR:-mirrors.aliyun.com})"
    if [ "${APT_MIRROR:-}" != "archive.ubuntu.com" ]; then
        # ubuntu:20.04 镜像默认 archive.ubuntu.com + security.ubuntu.com
        sed -i "s|http://archive.ubuntu.com/ubuntu|http://${APT_MIRROR:-mirrors.aliyun.com}/ubuntu|g; \
                s|http://security.ubuntu.com/ubuntu|http://${APT_MIRROR:-mirrors.aliyun.com}/ubuntu|g" \
            /etc/apt/sources.list
    fi
    apt-get update -qq
    # python3-dev:aqtinstall 的依赖 pyzstd 需要编译(20.04 无预编译 wheel);
    # ffmpeg 仅 ctest 用(20.04 universe 的 4.2 含 x264/x265);
    # xcb 运行库:linuxdeploy 会把它们打进 AppImage,须先在容器内可见
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        build-essential curl ca-certificates file python3 python3-dev python3-pip \
        libgl1-mesa-dev pkg-config ffmpeg \
        libx11-6 libx11-xcb1 libxcb1 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
        libxcb-randr0 libxcb-render0 libxcb-render-util0 libxcb-shape0 libxcb-sync1 \
        libxcb-util1 libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 libxcb-glx0 \
        libxkbcommon0 libxkbcommon-x11-0 \
        libfontconfig1 libdbus-1-3

    echo "[2/7] pip 安装 aqtinstall/cmake/ninja (PIP_INDEX_URL=${PIP_INDEX_URL:-https://mirrors.aliyun.com/pypi/simple})"
    python3 -m pip install -q --upgrade pip \
        -i "${PIP_INDEX_URL:-https://mirrors.aliyun.com/pypi/simple}"
    # aqtinstall<3.2 兼容 ubuntu 20.04 的 python3.8
    python3 -m pip install -q "aqtinstall<3.2" cmake ninja \
        -i "${PIP_INDEX_URL:-https://mirrors.aliyun.com/pypi/simple}"

    echo "[3/7] aqt 安装 Qt ${QT_VERSION} gcc_64 (qtbase + icu;幂等,已装则跳过)"
    if [ -d "/opt/qt/${QT_VERSION}/gcc_64/bin" ]; then
        echo "    /opt/qt/${QT_VERSION} 已存在,跳过下载"
    else
        # 先走官方源,失败回退阿里 Qt 镜像(国内网络;-b 是 install-qt 的子命令选项)
        aqt install-qt linux desktop "$QT_VERSION" gcc_64 \
            --archives qtbase icu --outputdir /opt/qt || {
            echo "    官方源失败/超时,回退 mirrors.aliyun.com/qt"
            aqt install-qt linux desktop "$QT_VERSION" gcc_64 \
                --archives qtbase icu --outputdir /opt/qt \
                -b https://mirrors.aliyun.com/qt/
        }
    fi
    QT_ROOT="/opt/qt/${QT_VERSION}/gcc_64"
    export PATH="$QT_ROOT/bin:$PATH"
    export QMAKE="$QT_ROOT/bin/qmake"

    echo "[4/7] 拷贝源码到容器工作区 $WORK/src(NTFS 等挂载不支持 chmod,构建须在容器 fs 内)"
    mkdir -p "$WORK"
    rm -rf "$WORK/src"
    mkdir -p "$WORK/src"
    ( cd "$ROOT" && tar \
        --exclude='./.git' \
        --exclude='./build' \
        --exclude='./build-*' \
        --exclude='./dist' \
        --exclude='./packaging/engines' \
        --exclude='./packaging/AppDir' \
        -cf - . ) | tar -xf - -C "$WORK/src"

    echo "[5/7] CMake 配置 + 构建 + ctest (Release, Ninja)"
    cd "$WORK/src"
    cmake -B build-appimage -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_ROOT"
    cmake --build build-appimage
    if [ "${SKIP_TESTS:-0}" = "1" ]; then
        echo "    SKIP_TESTS=1,跳过 ctest"
    else
        QT_QPA_PLATFORM=offscreen ctest --test-dir build-appimage --output-on-failure
    fi

    echo "[6/7] 组装 AppDir + linuxdeploy 产出 AppImage"
    # 引擎:优先只读引用仓库内已固化二进制(install 复制到 /work 的目标是容器 fs,
    # fchmod 作用于目标,不受 NTFS 影响);否则下载到可写的 /work/engines
    LOCAL_ENG="$ROOT/packaging/engines/linux-amd64"
    if [ -x "$LOCAL_ENG/ffmpeg" ] && [ -x "$LOCAL_ENG/ffprobe" ]; then
        echo "    使用仓库内已固化引擎: $LOCAL_ENG"
        ENGINES_DIR="$LOCAL_ENG"
    else
        ensure_engines "$WORK/engines/linux-amd64"
    fi
    APPDIR="$WORK/src/packaging/AppDir"
    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr/bin" \
             "$APPDIR/usr/share/applications" \
             "$APPDIR/usr/share/icons/hicolor/128x128/apps" \
             "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
             "$APPDIR/usr/share/icons/hicolor/scalable/apps" \
             "$APPDIR/usr/share/doc/1f6s-desktop/licenses"
    install -m 755 build-appimage/1f6s-desktop "$APPDIR/usr/bin/"
    install -m 755 "$ENGINES_DIR/ffmpeg"   "$APPDIR/usr/bin/ffmpeg"
    install -m 755 "$ENGINES_DIR/ffprobe"  "$APPDIR/usr/bin/ffprobe"
    cp -r "$WORK/src/assets" "$APPDIR/usr/assets"
    install -m 644 "$WORK/src/THIRD_PARTY.md" "$APPDIR/usr/share/doc/1f6s-desktop/"
    install -m 644 "$WORK/src"/packaging/licenses/* \
        "$APPDIR/usr/share/doc/1f6s-desktop/licenses/"
    install -m 644 "$WORK/src/packaging/1f6s-desktop.desktop" \
        "$APPDIR/usr/share/applications/1f6s-desktop.desktop"
    install -m 644 "$WORK/src/packaging/icon/1f6s-desktop-128.png" \
        "$APPDIR/usr/share/icons/hicolor/128x128/apps/1f6s-desktop.png"
    install -m 644 "$WORK/src/packaging/icon/1f6s-desktop-256.png" \
        "$APPDIR/usr/share/icons/hicolor/256x256/apps/1f6s-desktop.png"
    install -m 644 "$WORK/src/packaging/icon/1f6s-desktop.svg" \
        "$APPDIR/usr/share/icons/hicolor/scalable/apps/1f6s-desktop.svg"

    # linuxdeploy(-plugin-qt) 是 AppImage,容器内无 FUSE 用自解压模式。
    # 插件须按 linuxdeploy 的发现规则命名(linuxdeploy-plugin-qt,同目录)。
    TOOLS_DIR=/opt/appimage-tools
    mkdir -p "$TOOLS_DIR"
    if [ ! -x "$TOOLS_DIR/linuxdeploy.AppImage" ] || [ ! -x "$TOOLS_DIR/linuxdeploy-plugin-qt" ]; then
        fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" \
            "$TOOLS_DIR/linuxdeploy.AppImage"
        fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
            "$TOOLS_DIR/linuxdeploy-plugin-qt"
        chmod +x "$TOOLS_DIR"/linuxdeploy.AppImage "$TOOLS_DIR"/linuxdeploy-plugin-qt
        sha256sum "$TOOLS_DIR"/linuxdeploy.AppImage "$TOOLS_DIR"/linuxdeploy-plugin-qt
        # 自检:AppImage 头部与可执行性(截断文件在这里暴露)
        "$TOOLS_DIR/linuxdeploy.AppImage" --appimage-extract-and-run --version >/dev/null
    fi

    # 两段式:先只做部署(不带 --output),补丁完再打包。
    # 若直接 --output appimage,appimage 插件会在同一次调用内完成 mksquashfs,
    # 之后的 AppDir 修改都进不了包。
    export APPIMAGE_EXTRACT_AND_RUN=1
    export LINUXDEPLOY_OUTPUT_VERSION="$APP_VERSION"
    "$TOOLS_DIR/linuxdeploy.AppImage" \
        --appdir "$APPDIR" \
        -d "$APPDIR/usr/share/applications/1f6s-desktop.desktop" \
        -i "$APPDIR/usr/share/icons/hicolor/128x128/apps/1f6s-desktop.png" \
        -i "$APPDIR/usr/share/icons/hicolor/256x256/apps/1f6s-desktop.png" \
        --plugin qt

    # 部署后补丁(此时 Qt 插件已就位,AppImage 尚未打包):
    # a) offscreen 平台插件 —— linuxdeploy 只收 xcb;offscreen 供无头冒烟/自动化
    #    (须可执行:dlopen 需要 PROT_EXEC,644 会被 Qt 插件扫描静默跳过)
    # b) OpenSSL 1.1 —— Qt 6.2 编译期对 1.1,运行时按需 dlopen;新宿主机的 3.x
    #    会退化成 cert-only 后端(TLS 不可用),带上 20.04 的 1.1 保住完整 TLS
    install -m 755 "$QT_ROOT/plugins/platforms/libqoffscreen.so" \
        "$APPDIR/usr/plugins/platforms/" 2>/dev/null || \
        echo "    警告:libqoffscreen.so 未找到,跳过 offscreen 插件"
    for lib in libssl.so.1.1 libcrypto.so.1.1; do
        if [ -e "/usr/lib/x86_64-linux-gnu/$lib" ]; then
            cp -L "/usr/lib/x86_64-linux-gnu/$lib" "$APPDIR/usr/lib/"
        fi
    done

    "$TOOLS_DIR/linuxdeploy.AppImage" --appdir "$APPDIR" --output appimage
    # linuxdeploy 输出文件名由其内部逻辑派生(可能取 .desktop 的 Name),
    # 通配找到产物后统一重命名为 dist 下的规范名
    mkdir -p "$WORK/src/dist"
    PRODUCT_APPIMAGE="$(ls "$WORK/src/"*.AppImage 2>/dev/null | head -1)"
    [ -n "$PRODUCT_APPIMAGE" ] || { echo "未找到 linuxdeploy 产物 AppImage" >&2; exit 1; }
    mv "$PRODUCT_APPIMAGE" "$WORK/src/dist/${APPIMAGE_NAME}"

    echo "[7/7] 生成 SHA256SUMS 并拷回仓库 dist/"
    ( cd "$WORK/src/dist" && sha256sum "$APPIMAGE_NAME" > SHA256SUMS )
    mkdir -p "$ROOT/dist"
    cp "$WORK/src/dist/${APPIMAGE_NAME}" "$ROOT/dist/"
    cp "$WORK/src/dist/SHA256SUMS" "$ROOT/dist/"
    ls -la "$ROOT/dist"
    log "AppImage 完成: dist/${APPIMAGE_NAME}"
}

# ---------------------------------------------------------------------------
# 静态 ffmpeg 引擎下载就位(ensure_engines <目标目录>,须可写):
# 按 packaging/engines.json 下载并做 sha256 + encoders 校验。
# ---------------------------------------------------------------------------
ensure_engines() {
    local dest="$1"
    echo "    按 packaging/engines.json 下载引擎到 $dest(eugeneware/ffmpeg-static 镜像)…"
    mkdir -p "$dest"
    python3 - "$ROOT/packaging/engines.json" "$dest" <<'PY'
import gzip, hashlib, json, pathlib, subprocess, sys
spec, dest = json.load(open(sys.argv[1])), pathlib.Path(sys.argv[2])
e = spec["linux-amd64"]
for kind, url_key, sha_key in (("ffmpeg", "url_gz", "sha256_ffmpeg"),
                               ("ffprobe", "url_ffprobe_gz", "sha256_ffprobe")):
    gz = dest / f"{kind}.gz"
    subprocess.run(["curl", "-sSL", "--retry", "3",
                    "--retry-delay", "3", "-o", str(gz), e[url_key]], check=True)
    raw = dest / kind
    raw.write_bytes(gzip.decompress(gz.read_bytes()))
    gz.unlink()
    digest = hashlib.sha256(raw.read_bytes()).hexdigest()
    assert digest == e[sha_key], f"{kind} sha256 不符: {digest} != {e[sha_key]}"
    raw.chmod(0o755)
    print(f"    {kind}: sha256 校验通过 ({digest[:16]}…)")
PY
    # 编码器冒烟:与运行时 verifyEncoders 同一清单。
    # 先把输出捕获进变量再 grep——直接 `ffmpeg | grep -q` 会因 grep -q 提前退出
    # 使 ffmpeg 收到 SIGPIPE,在 pipefail 下炸掉脚本(CI 实测 exit 141)。
    local encoders version
    encoders="$("$dest/ffmpeg" -hide_banner -encoders 2>/dev/null)"
    grep -qE "\blibx264\b" <<<"$encoders"
    grep -qE "\blibx265\b" <<<"$encoders"
    grep -qE "\baac\b" <<<"$encoders"
    version="$("$dest/ffmpeg" -version 2>/dev/null)"
    printf '%s\n' "$version" | sed -n 1p
    ENGINES_DIR="$dest"
}

# ---------------------------------------------------------------------------
# 宿主编排:podman 起 ubuntu:20.04 容器跑同脚本 --in-container
# ---------------------------------------------------------------------------
run_in_podman() {
    local IMAGE="${PODMAN_IMAGE:-docker.m.daocloud.io/library/ubuntu:20.04}"
    log "宿主编排模式:podman 容器 $IMAGE 内构建(glibc 2.31 基线)"
    podman image inspect "$IMAGE" >/dev/null 2>&1 || {
        log "镜像不存在,先拉取(源可过 PODMAN_IMAGE 覆盖)"
        podman pull "$IMAGE"
    }
    # 网络环境变量透传进容器;linuxdeploy 工具缓存卷(首次下载后离线可复用)
    local -a ENV_ARGS=( -e "APT_MIRROR=${APT_MIRROR:-mirrors.aliyun.com}" )
    [ -n "${PIP_INDEX_URL:-}" ]  && ENV_ARGS+=( -e "PIP_INDEX_URL=$PIP_INDEX_URL" )
    [ -n "${SKIP_TESTS:-}" ]     && ENV_ARGS+=( -e "SKIP_TESTS=$SKIP_TESTS" )
    local -a VOL_ARGS=()
    [ -d "${HOME}/.cache/1f6s-appimage-tools" ] && \
        VOL_ARGS+=( -v "${HOME}/.cache/1f6s-appimage-tools:/opt/appimage-tools" )
    log "容器内构建开始(仓库挂载到 /src,产物写回 dist/)"
    podman run --rm \
        -v "$ROOT:/src" \
        -w /src \
        "${VOL_ARGS[@]}" \
        "${ENV_ARGS[@]}" \
        "$IMAGE" \
        bash packaging/make_appimage.sh --in-container
    log "容器内构建结束"
    host_smoke
}

# 宿主冒烟:offscreen 启动 3 秒自动退出(1F6S_SMOKE_EXIT_MS 是 main.cpp 内置钩子)
host_smoke() {
    log "宿主冒烟:QT_QPA_PLATFORM=offscreen 启动 AppImage(3 秒自退)"
    local appimage="$ROOT/dist/$APPIMAGE_NAME"
    [ -f "$appimage" ] || { echo "未找到 $appimage" >&2; return 1; }
    local -a PRE=()
    # 宿主无 FUSE 时退化为自解压运行
    if ! command -v fusermount >/dev/null 2>&1 && ! command -v fusermount3 >/dev/null 2>&1; then
        PRE=(--appimage-extract-and-run)
    fi
    # env 前缀传参:1F6S 开头的变量名不是合法 bash 标识符,不能直接前缀赋值
    QT_QPA_PLATFORM=offscreen env 1F6S_SMOKE_EXIT_MS=3000 \
        "$appimage" "${PRE[@]}"
    local rc=$?
    [ "$rc" -eq 0 ] || { echo "冒烟失败 exit=$rc" >&2; return "$rc"; }
    log "冒烟通过(exit 0)"
}

# ---------------------------------------------------------------------------
case "${1:-}" in
    --in-container) build_in_container ;;
    "")             run_in_podman ;;
    *) echo "用法: $0 [--in-container]" >&2; exit 2 ;;
esac
