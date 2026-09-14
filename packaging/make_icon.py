#!/usr/bin/env python3
"""生成 1f6s-desktop 应用图标(无第三方依赖,纯 stdlib)。

产出(写入 --outdir):
  1f6s-desktop.svg            矢量源(深色圆角方块 + "1f6s" 字样)
  1f6s-desktop-{128,256}.png  位图(手写 PNG 编码器 + 5x7 位图字体放大)

设计基调与主程序暗色 UI(main.cpp darkPalette)一致:
  背景 #1e1f22 圆角方块,前景 #cfd2d6 文字,点缀 #3d6b9e 高亮条。
"""
import argparse
import struct
import zlib

BG = (0x1E, 0x1F, 0x22)
FG = (0xCF, 0xD2, 0xD6)
ACCENT = (0x3D, 0x6B, 0x9E)

# 5x7 位图字体:'1','f','6','s'
GLYPHS = {
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "f": [".###.", "#....", "#....", "####.", "#....", "#....", "#...."],
    "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."],
    "s": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
}


def text_bitmap(text, tracking=1):
    """text → (rows of bool), 字宽 = 5*n + tracking*(n-1)"""
    cols = 5 * len(text) + tracking * (len(text) - 1)
    rows = [[False] * cols for _ in range(7)]
    for i, ch in enumerate(text):
        glyph = GLYPHS[ch]
        x0 = i * (5 + tracking)
        for y, line in enumerate(glyph):
            for x, c in enumerate(line):
                if c == "#":
                    rows[y][x0 + x] = True
    return rows


def rounded_rect_mask(w, h, radius):
    """圆角矩形 alpha 蒙版(0/255)。"""
    mask = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            # 距四个圆心的距离
            dx = min(x, w - 1 - x) - (radius - 1)
            dy = min(y, h - 1 - y) - (radius - 1)
            if dx >= 0 or dy >= 0:
                mask[y][x] = 255
            else:
                d2 = dx * dx + dy * dy
                mask[y][x] = 255 if d2 <= radius * radius else 0
    return mask


def render(size=256):
    """渲染 size×size RGBA 像素(暗色圆角方块 + 居中 1f6s + 底部高亮条)。"""
    bm = text_bitmap("1f6s")
    th = len(bm)          # 7
    tw = len(bm[0])       # 23
    # 字素放大倍数:文字目标占画布宽 ~60%
    scale = max(2, int(size * 0.60 / tw))
    w_px, h_px = tw * scale, th * scale
    x0 = (size - w_px) // 2
    y0 = (size - h_px) // 2 - int(size * 0.03)
    bar_h = max(2, int(size * 0.012))          # 底部 accent 条高度
    bar_w = w_px
    bar_y = y0 + h_px + int(size * 0.06)

    mask = rounded_rect_mask(size, size, int(size * 0.22))
    px = [[BG + (mask[y][x],) for x in range(size)] for y in range(size)]
    for y in range(h_px):
        gy = y // scale
        for x in range(w_px):
            if bm[gy][x // scale]:
                py, pxx = y0 + y, x0 + x
                if 0 <= py < size and 0 <= pxx < size:
                    px[py][pxx] = FG + (255,)
    for y in range(bar_h):
        for x in range(bar_w):
            py, pxx = bar_y + y, x0 + x
            if 0 <= py < size and 0 <= pxx < size:
                px[py][pxx] = ACCENT + (255,)
    return px


def write_png(path, px):
    h, w = len(px), len(px[0])
    raw = b"".join(
        b"\x00" + b"".join(bytes(p) for p in row) for row in px
    )
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


SVG = """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <!-- 1f6s-desktop 图标:暗色圆角方块 + "1f6s" + 底部高亮条(与主程序暗色 UI 同调) -->
  <rect width="256" height="256" rx="56" fill="#1e1f22"/>
  <text x="128" y="128" text-anchor="middle" dominant-baseline="central"
        font-family="DejaVu Sans Mono, Menlo, Consolas, monospace"
        font-size="88" font-weight="bold" letter-spacing="2"
        fill="#cfd2d6">1f6s</text>
  <rect x="40" y="172" width="176" height="6" rx="3" fill="#3d6b9e"/>
</svg>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    args = ap.parse_args()
    import os
    os.makedirs(args.outdir, exist_ok=True)
    with open(os.path.join(args.outdir, "1f6s-desktop.svg"), "w") as f:
        f.write(SVG)
    for size in (128, 256):
        write_png(os.path.join(args.outdir, f"1f6s-desktop-{size}.png"), render(size))
    print("icon written to", args.outdir)


if __name__ == "__main__":
    main()
