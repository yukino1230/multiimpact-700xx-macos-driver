#!/usr/bin/env python3
"""MultiImpact 700XX 用の ippeveprinter 属性ファイルを生成する。

mkppd.py と同じ forms.conf を読み、PPD の代わりに IPP の属性として書き出す。
PPD が使えなくなったときに ippeveprinter で「IPP Everywhere のプリンタ」として
見せるためのもの。

  mkippattr.py > mi700.conf
  ippeveprinter -D socket://<IP>:9100 -c .../mi700ippcmd -a mi700.conf \\
                "MultiImpact 700XX"

-a と -f は同時に指定できない(-P と -c と同じく Usage を出して止まる)。
受け付ける形式は document-format-supported としてこのファイルに書く。

IPP の長さの単位は 1/100 mm。PPD と違って用紙名に表示名を持たせられないので、
PWG の標準名がある用紙はそれを使い(クライアントが自分の言葉で訳す)、
無いものは custom_<キー>_<幅>x<高さ>mm にする。
"""
import os, sys

CONF = ([os.environ["MI700_FORMS"]] if os.environ.get("MI700_FORMS") else
        [os.path.expanduser("~/Developer/mi700/forms.conf"),
         "/usr/local/share/mi700/forms.conf"])

# forms.conf のキー → PWG 5101.1 の標準名(寸法が一致するものだけ)
PWG = {
    "A3":     "iso_a3_297x420mm",
    "B4":     "jis_b4_257x364mm",
    "A4":     "iso_a4_210x297mm",
    "B5":     "jis_b5_182x257mm",
    "A5":     "iso_a5_148x210mm",
    "Letter": "na_letter_8.5x11in",
}

# 給紙口。macOS は IPP 標準のキーワードしか PPD に取り込まない(reartractor や
# front は黙って捨てられる)ので、標準名に割り当てる。表示は macOS の訳語になる。
#   main → シートフィーダ  manual → シートガイド
#   rear → リアトラクタ    bottom → フロントトラクタ
# 読み替えは contrib/mi700ippcmd が行う
SOURCES = ["main", "manual", "rear", "bottom"]
BINS = ["front", "rear"]


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
            def col(k):
                return float(f[k]) if len(f) > k and f[k].strip() else 0.0
            out.append((f[0], f[1], float(f[2]), float(f[3]),
                        col(4), col(5), col(6), col(7)))
    return out or [("A4", "A4", 210.0, 297.0, 0.0, 0.0, 0.0, 0.0)]


def h(mm):
    """mm → 1/100 mm"""
    return int(round(mm * 100))


def pwgname(key, w, hh):
    if key in PWG:
        return PWG[key]
    return f"custom_{key.lower()}_{w:g}x{hh:g}mm"


def col(name, w, hh, l, r, t, b, source=None):
    m = [f"MEMBER collection media-size {{ MEMBER integer x-dimension {h(w)} "
         f"MEMBER integer y-dimension {h(hh)} }}",
         f"MEMBER integer media-left-margin {h(l)}",
         f"MEMBER integer media-right-margin {h(r)}",
         f"MEMBER integer media-top-margin {h(t)}",
         f"MEMBER integer media-bottom-margin {h(b)}",
         f"MEMBER keyword media-size-name {name}"]
    if source:
        m.append(f"MEMBER keyword media-source {source}")
    return "{ " + " ".join(m) + " }"


def main():
    S = forms()
    D = next((f for f in S if f[0] == "A4"), S[0])
    names = [pwgname(f[0], f[2], f[3]) for f in S]
    dname = pwgname(D[0], D[2], D[3])
    o = []
    def w(s): o.append(s)

    w("# MultiImpact 700XX — ippeveprinter -a 用 (mkippattr.py が生成)")
    w("# 表示名の対応(IPP には用紙の表示名を載せる場所が無いので記録だけ残す)")
    for f, n in zip(S, names):
        w(f"#   {n:<34} {f[1]}")
    w("")
    w('ATTR text printer-make-and-model "NEC MultiImpact 700XX"')
    w('ATTR text printer-info "NEC MultiImpact 700XX"')
    w("")
    # 受け付ける形式。-a を使うと -f は指定できない(ippeveprinter が Usage で止まる)
    # ので、ここに書く。PWG Raster は CUPS ラスタ v2 と同じ容器
    w("ATTR mimeMediaType document-format-supported image/pwg-raster")
    w("ATTR mimeMediaType document-format-default image/pwg-raster")
    # 1bit 白黒・160dpi だけ。rastertomi700 は 1bit 以外を受け付けない
    w("ATTR keyword print-color-mode-supported monochrome")
    w("ATTR keyword print-color-mode-default monochrome")
    w("ATTR keyword pwg-raster-document-type-supported black_1")
    w("ATTR resolution pwg-raster-document-resolution-supported 160dpi")
    w("ATTR resolution printer-resolution-supported 160dpi")
    w("ATTR resolution printer-resolution-default 160dpi")
    w("ATTR keyword sides-supported one-sided")
    w("ATTR keyword sides-default one-sided")
    w("")
    # 用紙。余白は印字可能範囲(推奨印刷範囲)。クライアントはここに描かない
    w("ATTR keyword media-supported " + ",".join(names))
    w("ATTR keyword media-default " + dname)
    w("ATTR keyword media-ready " + dname)
    for key in ("left", "right", "top", "bottom"):
        i = {"left": 4, "right": 5, "top": 6, "bottom": 7}[key]
        vals = sorted({h(f[i]) for f in S})
        w(f"ATTR integer media-{key}-margin-supported " + ",".join(map(str, vals)))
    # 用紙を指定したジョブは media-col の寸法をこれと照合される。書かないと
    # ippeveprinter の既定(letter/legal/a4 だけ)と比べられて
    # "Unsupported media-col collection value" で拒否される
    sizes = list(dict.fromkeys((h(f[2]), h(f[3])) for f in S))
    w("ATTR collection media-size-supported " + ",".join(
        f"{{ MEMBER integer x-dimension {x} MEMBER integer y-dimension {y} }}"
        for x, y in sizes))
    w("ATTR collection media-col-database " +
      ",".join(col(n, *f[2:8]) for f, n in zip(S, names)))
    w("ATTR collection media-col-default " + col(dname, *D[2:8], source="main"))
    w("ATTR collection media-col-ready " + col(dname, *D[2:8], source="main"))
    w("")
    w("ATTR keyword media-source-supported " + ",".join(SOURCES))
    w("ATTR keyword output-bin-supported " + ",".join(BINS))
    w("ATTR keyword output-bin-default front")
    w("")
    # 印刷品質。IPP の3段階に4通りを割り当てる(片方向ドラフトは落ちる)
    #   draft → ドラフト両方向 / normal → 標準両方向 / high → 標準片方向
    w("ATTR enum print-quality-supported 3,4,5")
    w("ATTR enum print-quality-default 4")
    sys.stdout.write("\n".join(o) + "\n")


if __name__ == "__main__":
    main()
