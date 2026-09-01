#!/usr/bin/env python3
"""MultiImpact 700XX 用の PPD を生成する。

用紙サイズは ~/Developer/mi700/forms.conf (または /usr/local/share/mi700/forms.conf)
に1行足すだけで増える。出力は Shift-JIS（PPD 仕様で UTF-8 は使えないため）。
"""
import os, sys

# ユーザ側を先に読む(同名は先勝ちなので、~/Developer/mi700 がシステム設定を上書きする)
# MI700_FORMS を指定した場合はそのファイルだけを読む(パッケージのビルド用。
# 既にインストール済みの forms.conf から古い用紙が混ざるのを防ぐ)
CONF = ([os.environ["MI700_FORMS"]] if os.environ.get("MI700_FORMS") else
        [os.path.expanduser("~/Developer/mi700/forms.conf"),
         "/usr/local/share/mi700/forms.conf"])
MM = 72 / 25.4
MAXW, MAXH = 980, 10000        # 136桁=13.6inch=979pt / 連続紙用に縦は長く
FILTER = "/usr/local/libexec/cups/filter/rastertomi700"


def forms():
    out, seen = [], set()
    for c in CONF:
        if not os.path.exists(c):
            continue
        for line in open(c, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            f = line.split("\t")
            if len(f) < 4 or f[0] in seen:
                continue
            seen.add(f[0])
            # 5〜8列目 = ページ座標での印字不可領域 左/右/上/下 mm (省略可)
            def col(k):
                return float(f[k]) * MM if len(f) > k and f[k].strip() else 0.0
            out.append((f[0], f[1],
                        round(float(f[2]) * MM, 2), round(float(f[3]) * MM, 2),
                        round(col(4), 2), round(col(5), 2),
                        round(col(6), 2), round(col(7), 2)))
    return out or [("A4", "A4", 595.28, 841.89, 0.0, 0.0, 0.0, 0.0)]


def main():
    S = forms()
    # 既定の用紙。A4 があればそれ、無ければ先頭。アプリが用紙を指定しない場合や
    # プリンタ追加直後の印刷ダイアログはこの用紙で開く。
    D = next((f for f in S if f[0] == "A4"), S[0])
    p = []
    def w(s): p.append(s)
    w('*PPD-Adobe: "4.3"')
    w('*FormatVersion: "4.3"')
    w('*FileVersion: "1.0"')
    w('*LanguageEncoding: JIS83-RKSJ')
    w('*LanguageVersion: Japanese')
    w('*Manufacturer: "NEC"')
    w('*ModelName: "NEC MultiImpact 700XX"')
    w('*ShortNickName: "MultiImpact 700XX"')
    w('*NickName: "NEC MultiImpact 700XX (mi700)"')
    w('*PCFileName: "MI700XX.PPD"')
    w('*Product: "(MultiImpact 700XX)"')
    w('*PSVersion: "(3010.000) 0"')
    w('*LanguageLevel: "3"')
    w('*ColorDevice: False')
    w('*DefaultColorSpace: Gray')
    w('*FileSystem: False')
    w('*Throughput: "1"')
    w('*cupsVersion: 2.3')
    w('*cupsModelNumber: 0')
    w('*cupsManualCopies: True')
    # PDF -> CUPSラスタ は macOS 標準の cgpdftoraster が行う(Ghostscript 不要)
    w(f'*cupsFilter: "application/vnd.cups-raster 0 {FILTER}"')
    w('')
    # ラスタの形式を明示(1bit 黒)。CUPS がこの PostScript を解釈してヘッダを作る
    w('*OpenUI *ColorModel/カラーモード: PickOne')
    w('*DefaultColorModel: Gray1')
    w('*ColorModel Gray1/白黒: '
      '"<</cupsColorSpace 3/cupsBitsPerColor 1/cupsBitsPerPixel 1>>setpagedevice"')
    w('*CloseUI: *ColorModel')
    w('')
    w('*OpenUI *Resolution/解像度: PickOne')
    w('*DefaultResolution: 160dpi')
    w('*Resolution 160dpi/160 dpi: "<</HWResolution[160 160]>>setpagedevice"')
    w('*CloseUI: *Resolution')
    w('')
    for key in ("PageSize", "PageRegion"):
        w(f'*OpenUI *{key}/用紙サイズ: PickOne')
        w(f'*Default{key}: {D[0]}')
        for n, lbl, ww, hh, lm, rm, tm, bm in S:
            w(f'*{key} {n}/{lbl}: "<</PageSize[{ww:g} {hh:g}]>>setpagedevice"')
        w(f'*CloseUI: *{key}')
        w('')
    w(f'*DefaultImageableArea: {D[0]}')
    # 実際に印字できる範囲。ページ原点が用紙左上ではないぶん右と下が削られる。
    # ここを用紙全面にすると用紙外に印字しようとしてプリンタがエラーで停止する。
    for n, lbl, ww, hh, lm, rm, tm, bm in S:
        w(f'*ImageableArea {n}/{lbl}: "{lm:g} {bm:g} {ww-rm:g} {hh-tm:g}"')
    w('')
    w(f'*DefaultPaperDimension: {D[0]}')
    for n, lbl, ww, hh, lm, rm, tm, bm in S:
        w(f'*PaperDimension {n}/{lbl}: "{ww:g} {hh:g}"')
    w('')
    w(f'*HWMargins: {D[4]:g} {D[7]:g} {D[5]:g} {D[6]:g}')
    w('*VariablePaperSize: True')
    w(f'*MaxMediaWidth: "{MAXW}"')
    w(f'*MaxMediaHeight: "{MAXH}"')
    w('*CustomPageSize True: "pop pop pop pop pop"')
    w(f'*ParamCustomPageSize Width: 1 points 36 {MAXW}')
    w(f'*ParamCustomPageSize Height: 2 points 36 {MAXH}')
    w('*ParamCustomPageSize WidthOffset: 3 points 0 0')
    w('*ParamCustomPageSize HeightOffset: 4 points 0 0')
    w('*ParamCustomPageSize Orientation: 5 int 0 0')
    w('')
    # macOS の印刷ダイアログは PPD の標準オプション名(InputSlot/OutputBin/OutputMode)を
    # 認識して専用の欄に出す。独自名にすると「プリンタの機能」の中に隠れてしまう。
    w('*OpenUI *InputSlot/給紙口: PickOne')
    w('*OrderDependency: 10 AnySetup *InputSlot')
    w('*DefaultInputSlot: feeder')
    w('*InputSlot feeder/シートフィーダ (自動吸入): ""')
    w('*InputSlot guide/シートガイド (手差し): ""')
    # 値の名前が IPP の標準キーワード(rear など)と一致すると macOS が独自の訳語
    # (「リアトレイ」)に置き換えてしまうため、衝突しない名前にする
    w('*InputSlot reartractor/リアトラクタ (連続紙): ""')
    w('*InputSlot fronttractor/フロントトラクタ (連続紙): ""')
    w('*CloseUI: *InputSlot')
    w('')
    # macOS が独立表示するのは InputSlot だけ。残りは1つのグループにまとめて
    # 「プリンタの機能」で機能セットを切り替えずに全部見えるようにする。
    w('*OpenGroup: MI700/MultiImpact 700XX')
    w('')
    w('*OpenUI *OutputBin/カット紙の排出方向: PickOne')
    w('*OrderDependency: 20 AnySetup *OutputBin')
    w('*DefaultOutputBin: front')
    w('*OutputBin front/手前側 (シートガイド): ""')
    w('*OutputBin rear/奥側 (スタッカ): ""')
    w('*CloseUI: *OutputBin')
    w('')
    w('*OpenUI *OutputMode/印刷品質: PickOne')
    w('*OrderDependency: 30 AnySetup *OutputMode')
    w('*DefaultOutputMode: std-bi')
    w('*OutputMode std-bi/標準 (両方向印字): ""')
    w('*OutputMode std-uni/標準 (片方向印字): ""')
    w('*OutputMode draft-bi/ドラフト (両方向印字): ""')
    w('*OutputMode draft-uni/ドラフト (片方向印字): ""')
    w('*OutputMode none/指定なし (本体設定に従う): ""')
    w('*CloseUI: *OutputMode')
    w('')
    w('*OpenUI *MI700Bottom/ボトム領域 (ミシン目回避): PickOne')
    w('*OrderDependency: 40 AnySetup *MI700Bottom')
    w('*DefaultMI700Bottom: 0')
    w('*MI700Bottom 0/なし: ""')
    w('*MI700Bottom 6/6行 (25.4mm・規格推奨): ""')
    w('*MI700Bottom 3/3行 (12.7mm): ""')
    w('*CloseUI: *MI700Bottom')
    w('')
    w('*CloseGroup: MI700')
    sys.stdout.buffer.write(("\n".join(p) + "\n").encode("shift_jis", "replace"))


if __name__ == "__main__":
    main()
