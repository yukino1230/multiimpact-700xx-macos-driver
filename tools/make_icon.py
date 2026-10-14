#!/usr/bin/env python3
"""MultiImpact 700XX をデフォルメしたプリンタアイコンを描く。

  python3 tools/make_icon.py driver/icons/icon.png      # 512px の PNG(確かめる用)
  python3 tools/make_icon.py --all driver/icons         # 48/128/512 の PNG と .icns を作る

実機(水平型 700 シリーズ)の特徴を残してある:
  - 横に長く背の低い筐体。左3分の2が濃い色のカバー、右端が一段高い操作部のタワー
  - タワーの前面に小さな液晶と青いボタン
  - 前面から連続紙(左右にスプロケット穴)が出てくる

PNG は PAPPL のプリンタアプリが printer-icons として配り、.icns は PPD の
*APPrinterIconPath が指す。Pillow が要る。
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

N  = 1024
SS = 3                      # 3倍で描いて縮める(輪郭を滑らかにする)

BODY      = (232, 229, 223)   # 筐体(オフホワイト)
BODY_DARK = (205, 201, 193)   # 筐体の陰
BODY_ED   = (168, 164, 156)   # 筐体の輪郭(白背景で沈まないように)
COVER     = (58, 63, 70)      # 上カバー(濃いグレー)
COVER_HI  = (74, 80, 88)
BASE      = (120, 120, 124)   # 脚
LCD       = (46, 110, 170)
LCD_HI    = (122, 186, 232)
BTN       = (64, 118, 180)
PAPER     = (252, 252, 250)
PAPER_ED  = (176, 176, 170)
INK       = (120, 124, 130)
HOLE      = (86, 90, 96)
SHADOW    = (0, 0, 0, 60)


def draw_icon():
    big = N * SS
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    def S(*v): return [round(x * SS) for x in v]

    # 影(本体の下)
    sh = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse(S(120, 740, 904, 860), fill=SHADOW)
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(14 * SS)))

    # 脚
    for x0, x1 in ((170, 260), (760, 850)):
        d.rounded_rectangle(S(x0, 700, x1, 762), radius=12 * SS, fill=BASE)

    # 本体(横に長い箱)
    d.rounded_rectangle(S(88, 420, 936, 724), radius=36 * SS, fill=BODY, outline=BODY_ED, width=3 * SS)
    # 下側の陰
    d.rounded_rectangle(S(88, 652, 936, 724), radius=36 * SS, fill=BODY_DARK)
    d.rectangle(S(88, 652, 936, 684), fill=BODY_DARK)

    # 上カバー(濃い色・左3分の2)。リブを数本入れる
    d.rounded_rectangle(S(126, 236, 748, 462), radius=34 * SS, fill=BODY, outline=BODY_ED, width=3 * SS)  # カバーの座
    d.rounded_rectangle(S(150, 252, 724, 446), radius=26 * SS, fill=COVER)
    d.rounded_rectangle(S(150, 252, 724, 300), radius=26 * SS, fill=COVER_HI)
    d.rectangle(S(150, 280, 724, 300), fill=COVER_HI)
    for x in range(196, 690, 92):
        d.rounded_rectangle(S(x, 320, x + 58, 418), radius=14 * SS, fill=COVER_HI)

    # 右の操作部タワー(一段高い)
    d.rounded_rectangle(S(766, 290, 936, 724), radius=36 * SS, fill=BODY, outline=BODY_ED, width=3 * SS)
    # 液晶
    d.rounded_rectangle(S(790, 404, 900, 474), radius=10 * SS, fill=LCD)
    d.rounded_rectangle(S(802, 416, 888, 462), radius=6 * SS, fill=LCD_HI)
    # ボタン
    for i, x in enumerate((790, 834, 878)):
        d.ellipse(S(x, 508, x + 26, 534), fill=BTN)
    d.ellipse(S(834, 352, 860, 378), fill=(150, 196, 120))   # 電源/オンランプ

    # 前面の用紙スロット(水平インサータ)
    d.rounded_rectangle(S(150, 486, 720, 528), radius=14 * SS, fill=BODY_DARK)

    # 連続紙(前面から手前へ)。左右にスプロケット穴
    paper = [S(236, 560)[0:2] + S(688, 560)[0:2], ]
    d.polygon([*S(250, 548), *S(690, 548), *S(742, 876), *S(198, 876)], fill=PAPER)
    d.line([*S(250, 548), *S(198, 876)], fill=PAPER_ED, width=3 * SS)
    d.line([*S(690, 548), *S(742, 876)], fill=PAPER_ED, width=3 * SS)
    d.line([*S(198, 876), *S(742, 876)], fill=PAPER_ED, width=3 * SS)
    # スプロケット穴(左右の縁に沿って)
    for i in range(7):
        t = (i + 0.6) / 7.2
        yl = 548 + (876 - 548) * t
        xl = 250 + (198 - 250) * t
        xr = 690 + (742 - 690) * t
        r = 9 + 5 * t
        d.ellipse(S(xl + 16 - r, yl - r, xl + 16 + r, yl + r), fill=HOLE)
        d.ellipse(S(xr - 16 - r, yl - r, xr - 16 + r, yl + r), fill=HOLE)
    # 印字(グレーの行)
    for i in range(4):
        t = (i + 1.1) / 5.6
        y = 548 + (876 - 548) * t
        x0 = 250 + (198 - 250) * t + 52 + 6 * t
        x1 = 690 + (742 - 690) * t - 52 - 6 * t
        if i == 3:
            x1 = x0 + (x1 - x0) * 0.55
        d.rounded_rectangle(S(x0, y - 9 - 3 * t, x1, y + 9 + 3 * t), radius=6 * SS, fill=INK)
    # ミシン目(紙の途中)
    t = 0.62
    y = 548 + (876 - 548) * t
    x0 = 250 + (198 - 250) * t + 26
    x1 = 690 + (742 - 690) * t - 26
    n = 22
    for i in range(n):
        xa = x0 + (x1 - x0) * (i / n)
        xb = x0 + (x1 - x0) * ((i + 0.55) / n)
        d.line([*S(xa, y), *S(xb, y)], fill=PAPER_ED, width=3 * SS)

    return img.resize((N, N), Image.LANCZOS)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    allout = "--all" in sys.argv
    img = draw_icon()
    if not allout:
        out = args[0] if args else "icon.png"
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        img.resize((512, 512), Image.LANCZOS).save(out, optimize=True)
        print("できました:", out)
        return

    outdir = args[0] if args else "driver/icons"
    os.makedirs(outdir, exist_ok=True)
    for size in (48, 128, 512):
        p = os.path.join(outdir, f"icon-{size}.png")
        img.resize((size, size), Image.LANCZOS).save(p, optimize=True)
        print("できました:", p)
    # .icns (PPD の *APPrinterIconPath 用)
    with tempfile.TemporaryDirectory() as tmp:
        iconset = os.path.join(tmp, "MI700XX.iconset")
        os.makedirs(iconset)
        for size in (16, 32, 128, 256, 512):
            img.resize((size, size), Image.LANCZOS).save(os.path.join(iconset, f"icon_{size}x{size}.png"))
            img.resize((size * 2, size * 2), Image.LANCZOS).save(os.path.join(iconset, f"icon_{size}x{size}@2x.png"))
        icns = os.path.join(outdir, "MI700XX.icns")
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o", icns], check=True)
        print("できました:", icns)


if __name__ == "__main__":
    main()
