# 付属コマンド・アイコン・他機種への移植

*[← README に戻る](../README.md)　/　English: [tools.en.md](tools.en.md)*

日常の印刷には要らない、検証と調整のための道具と、配布や移植のための話です。

## 付属コマンド

いずれもパッケージに含まれ、`/usr/local/bin` に入ります。
普段の印刷は印刷ダイアログで済むので、ここにあるのは**検証と調整のための道具**です。

### mi700print — テキストをそのまま印刷する

UTF-8 のテキストを 201PL に変換します。CUPS を通さないので、ラスタ化を挟まず
プリンタ内蔵フォントで印字されます。速く、罫線のズレも出ません。

```bash
echo "見積書" | mi700print | lp -d <キュー名> -o raw
mi700print -w 100 < memo.txt | lp -d <キュー名> -o raw
```

| | |
|---|---|
| `-w 桁数` | 折り返し桁数（既定 80） |
| `-n` | 末尾の FF を送らない |

日本語は `ESC K` + JIS で送り、改行は CR+LF、ページ末尾に FF を付けます。
**変換できない文字は必ず2バイトの `〓` に落とします**。1バイトの `?` にすると
そこから先の漢字ストリームが全部ずれます。`～` `－` `∥` は CP932 と JIS X 0208 で
割り当てが違うので正規化しますが、**`￥` は JIS 216F に直接あるので正規化しません**。

### pdf2mi700 — CUPS を通さずに PDF/PS を変換する

ドライバを入れなくても使える旧経路です。ラスタ化に Ghostscript が要ります。

```bash
pdf2mi700 --source rear --quality std-uni doc.pdf > out.prn
lp -d <キュー名> -o raw out.prn
```

| | |
|---|---|
| `--source` | `feeder` / `guide` / `reartractor` / `fronttractor` |
| `--eject` | `front` / `rear`（カット紙のみ） |
| `--quality` | `std-bi` / `std-uni` / `draft-bi` / `draft-uni` / `none` |
| `--form-lines N` | 用紙長を行数で明示（既定はページ高さから算出） |
| `--bottom N` | ボトム領域の行数（規格の 25.4mm は 6行） |
| `--no-form-length` | 用紙長を送らず本体の設定に従う |
| `--paper NAME` | 登録済みプリセットの初期化列を使う |
| `--list` | プリセット一覧 |

### mi700decode — .prn を画像に戻す

**紙を使わずに出力を検証できます。** PBM を標準出力に吐くので、`sips` で PNG にできます。

```bash
mi700decode out.prn > out.pbm
sips -s format png out.pbm --out out.png
```

座標系が Windows ドライバと一致することを証明したのもこれです。実機で正しく印字できた
キャプチャをデコードし、こちらのドライバに通し直して、同じ座標にドットが来るか比べました。

### mi700align — 印字位置を実測する

用紙1枚で「ページの原点が用紙端から何mmか」を測るための、10mm 刻みの目盛りパターンです。
**トラクタを動かしたら測り直してください。**

**目盛りはページ座標**（ページ原点からの mm）で、基準線は**印字可能範囲の角**に引かれます。
ページ原点そのものには線を引きません。原点は印字可能範囲の外にあることが多く
（カット紙なら左1.6mm・上8.7mm）、そこに引いた線はラスタライズの時点で消えてしまうためです。

読み方は簡単です。**用紙の端から任意の目盛りまでを測り、その目盛りの数字を引く**と
ページ原点の位置が出ます。紙にも同じ説明と、印字可能範囲の角がページ座標で何mmかが刷られます。

```bash
mi700align                       # 登録済み用紙の一覧
mi700align Cont10x11 > a.ps      # 用紙名を指定
mi700align --size 210x297 > a.ps # 実寸を直接指定(mm)
```

PostScript を出すので、CUPS 経由で印刷するなら PDF に変換してから `lp` に渡します。

> **落とし穴:** Ghostscript に渡すファイル名を `align.ps` にしないでください。
> gs は自分のライブラリを先に探すので、**同名の gs 付属の `align.ps` を拾います**。

### mi700diff — キャプチャを命令単位で比較する

```bash
mi700diff a.prn b.prn
```

Windows 側で**設定を1つだけ変えて**採った2つを渡すと、その設定に対応する制御コードが
差分として出ます。給紙口や排紙方向のコードはこの方法で割り出しました。

### mi700preset — キャプチャをプリセットに登録する

```bash
mi700preset capture.prn rear     # 登録
mi700preset --list               # 一覧
```

登録すると `pdf2mi700 --paper <名前>` で初期化列を再利用できます。

> `.prn` は Windows 側でプリンタのポートを `FILE:` に変えて印刷すると採れます。
> **実際に印刷した内容がそのまま入るので、公開する前に `mi700decode` で中身を確認してください。**

## アイコン

印刷ダイアログや「プリンタとスキャナ」、iPhone のプリンタ一覧に出るアイコンを同梱しています。
実機（水平型 700 シリーズ）をデフォルメした絵で、[`tools/make_icon.py`](../tools/make_icon.py) で描いています。

| 経路 | 仕組み | 置き場所 |
|---|---|---|
| PPD | `*APPrinterIconPath` | `/usr/local/share/mi700/MI700XX.icns` |
| PAPPL 版 | `printer-icons`（48 / 128 / 512 の PNG） | `/usr/local/share/mi700/icons/` |

描き直すときは Pillow が要ります。

```bash
python3 tools/make_icon.py --all driver/icons   # PNG 3枚と .icns を作り直す
python3 tools/make_icon.py icon.png             # 512px を1枚だけ(確認用)
```

## 署名と公証（配布する人向け）

`build-pkg.sh` は **Developer ID 証明書があれば自動で署名します**。無ければ未署名のまま
作られるだけで、ビルドは失敗しません。自分で使うぶんには署名は要りません。

公証まで通すには、`notarytool` の資格情報を一度キーチェーンに入れておきます。

```bash
xcrun notarytool store-credentials mi700 --apple-id <Apple ID> --team-id <Team ID>
MI700_NOTARY=mi700 ./driver/build-pkg.sh 4.0
```

フィルタを Hardened Runtime 付きで署名 → pkg を署名 → 公証 → `stapler staple` まで行います。
証明書を明示したい場合は `MI700_SIGN_APP` / `MI700_SIGN_PKG` で指定できます。

なお **`git clone` してビルドしたパッケージは、署名がなくても Gatekeeper に止められません。**
quarantine 属性が付くのはブラウザでダウンロードしたファイルだけだからです。

`mi700setup` が `socket://<IP>:9100` のキューを作ります。

## 他機種に移植する

201PL 系のプリンタなら移植できる見込みがあります。実際に効いてくる箇所と、
**紙を無駄にしない進め方**を書いておきます。

### 1. プリンタ言語を確かめる

```bash
snmpget -v1 -c public <IP> 1.3.6.1.2.1.25.3.2.1.3.1   # 機種名
```

LAN ボードの Web UI にも出ることがあります（うちの個体は `/opt.html` に `201PL` と表示されました）。
本体の設定状態印刷でも確認できます。ESC/P や NPDL なら別物なので、このドライバは使えません。

### 2. Windows のキャプチャを採る

**これが一番の情報源です。** プリンタのポートを `FILE:` に変えて印刷すると `.prn` が採れます。

**設定を1つだけ変えて2つ採り**、`mi700diff` にかけてください。その設定に対応する制御コードが
差分として出ます。給紙口・排出方向・印刷品質のコードは、すべてこの方法で割り出しました。

### 3. 紙を使わずに検証する

`mi700decode` でキャプチャを画像に戻し、**その画像を自分のドライバに通し直して**、
同じ座標に同じドットが来るかを比べます。座標系が一致していることは、これで紙1枚使わずに
証明できます。実機に出すのはその後で十分です。

### 4. 変える場所

| ファイル | 何を |
|---|---|
| `driver/mkppd.py` | `*Resolution`（dpi）、`*ModelName` / `*Product`、`InputSlot` の選択肢 |
| `driver/rastertomi700.c` | `BAND`（ピン数）、`TDPI`（`ESC T` の単位）、初期化列、給紙口・排出のコード |
| `forms.conf` | 用紙寸法と**印字可能範囲** |
| `driver/mi700setup` | 既定のキュー名 |

`*ModelName` と `*Product` は**ドライバの自動選択に使われます**。SNMP の `hrDeviceDescr` が
返す文字列と揃えておくと、macOS の「プリンタを追加」で IP を入れるだけでドライバが当たります。

### ハマりやすいところ

こちらが実際に外した順です。

- **解像度。** 横は `ESC F` の1ドット、縦は `ESC J` のバンド内1ドットです。700XX は
  縦横とも 160dpi でしたが、24ピン機は 180dpi のこともあります。全角文字が正方形になるか、
  既知の寸法の矩形を刷って実測するかで確かめます。
  **このドライバは正方ピクセルを前提にしています**（横は CUPS ラスタの1画素＝1ドット）。
  縦横で解像度が違う機種はフィルタの修正が要ります
- **バンド内のビット順。** 700XX は LSB が上端でした。逆にすると帯ごとに上下が反転します。
  `mi700decode` のビット順を4通り試して、読める向きを選ぶのが早道です
- **`ESC T` の単位。** 1/120 インチでした。1/180 と誤ると縦が 1.5 倍に伸びます
- **印字可能範囲。** ここを用紙全面にすると**プリンタがエラーで停止します**。
  メーカーの用紙設計説明書にある**推奨印刷範囲**から計算してください。最大印刷範囲には
  用紙ジャムやヘッド故障の注意書きが付いています
- **9ピン機。** `ESC J` は24ドット前提なので、8ドットの機種はビットイメージの
  組み立てから作り直しになります

## 構成

```
driver/rastertomi700.c   CUPS ラスタ -> 201PL のフィルタ（C、依存なし）
driver/mi700enc.c        201PL の組み立て（フィルタと PAPPL 版で共用）
driver/mkppd.py          forms.conf から Shift-JIS の PPD を生成する
driver/build-pkg.sh      配布パッケージを作る
driver/mkippattr.py      forms.conf から ippeveprinter の属性ファイルを生成する
driver/mi700ipp          ippeveprinter を launchd に登録する（AirPrint 版）
driver/mi700pappl.c      PAPPL 版のプリンタアプリケーション
driver/build-pappl.sh    PAPPL 版をビルドする
driver/mi700app          PAPPL 版を launchd に登録し、キューを作る
contrib/mi700ippcmd      ippeveprinter から呼ばれる変換コマンド
driver/mi700setup        キューを登録する
driver/mi700default      キューの既定値を設定する
driver/mi700status       SNMP でプリンタの状態を見る
driver/icons/            プリンタのアイコン(PNG 3種と .icns)
docs/                    詳しい話(この一覧の文書)
forms.conf               用紙定義
tools/mi700print         UTF-8 テキストを ESC K + JIS で印刷する
tools/pdf2mi700          PDF/PS を Ghostscript 経由で変換する（CUPS を通さない旧経路）
tools/mi700decode        .prn を PBM に戻す（紙を使わずに検証できる）
tools/mi700align         印字位置の確認パターンを作る
tools/mi700diff          キャプチャ同士を比較する
tools/mi700preset        キャプチャをプリセットとして登録する
tools/make_icon.py       アイコンを描く(Pillow が要る)
```

`tools/` の使い方は[付属コマンド](#付属コマンド)にあります。

PPD は `*LanguageEncoding: JIS83-RKSJ`、つまり **Shift-JIS** で出力します。
UTF-8 にすると印刷ダイアログの表示が文字化けします。
