# MultiImpact 700XX を macOS から使う

*English version: [README.en.md](README.en.md)*

NEC のドットインパクトプリンタ **MultiImpact 700XX** 用の CUPS ドライバです。
NEC は本機の macOS 用ドライバを提供していないため、201PL（PC-PR201 系）の制御コードを
直接組み立てる CUPS フィルタを書きました。アプリの印刷ダイアログからそのまま印刷できます。

> A CUPS driver for the NEC MultiImpact 700XX dot-matrix printer on macOS.
> NEC ships no macOS driver for this printer; this one speaks 201PL (PC-PR201) directly.

**現在の版は [v4.0](https://github.com/yukino1230/multiimpact-700xx-macos-driver/releases/tag/v4.0)**（署名・公証済みの pkg を [Releases](https://github.com/yukino1230/multiimpact-700xx-macos-driver/releases) で配布しています）。
**iPhone / iPad とドライバなしの Mac からも印刷できます。** PPD を使わずに AirPrint のプリンタとして
常駐させる経路として、[PAPPL](https://www.msweet.org/pappl/) で書いたプリンタアプリケーション
（給紙口や用紙名が日本語で出ます）を同梱しています。
詳しくは「[PAPPL で作ったプリンタアプリケーション](docs/airprint.md#pappl-で作ったプリンタアプリケーション)」。

> Current release: **v4.0**. It also prints from iPhone/iPad and driverless Macs by running the
> printer as an AirPrint (IPP Everywhere) service without a PPD, using a bundled PAPPL-based
> Printer Application.

**ドットインパクトプリンタ**は複写伝票や連続紙の印刷に今も使われていますが、
メーカーは macOS 用のドライバを出していません。「Mac から印刷できない」
「Windows でしか使えない」と諦めていた方に向けたものです。
対象は **PC-PR201 / PR201 / 201PL** 系のプリンタ言語を話す機種です。

## 特徴

- **印刷ダイアログから普通に印刷できる。** 給紙口・用紙サイズ・印刷品質を選べます
- **外部依存なし。** PDF のラスタライズは macOS 標準の `cgpdftoraster` に任せ、
  CUPS ラスタ → 201PL の変換だけを C で行います。Ghostscript も libcups も要りません
- **用紙定義はテキストファイル1つ。** `forms.conf` に1行足せば用紙が増えます
- **プリンタの状態を印刷キューに出します。** 用紙切れやカバー開のときに理由が表示されます
- **iPhone / iPad からも印刷できます。** AirPrint のプリンタとして常駐させます。
  詳しくは「[iPhone・iPad から印刷する](docs/airprint.md)」

## 対象機種

| | |
|---|---|
| 実機で検証済み | **MultiImpact 700XX**（LAN ボード内蔵、RAW 9100） |
| 文書上は動くはず | **MultiImpact 700XX2 / 700XX2N / 700JX3 / 700JX3N**（**実機未検証**） |
| たぶん動く | その他の 201PL 系（**未検証**） |

700XX2 と 700JX3 については、マニュアルで裏を取ってあります。

- **1冊のマニュアルが XX2 と JX3 の両方をカバーし**、「700XX2」を「700JX3」と読み替えるだけ、
  と明記されています。制御コード表も両者を区別していません
- **このドライバが使うコマンドは、拡張制御コード表で全て「○（有）」**です
  （`ESC J` `ESC F` `ESC T` `ESC M` `ESC H` `ESC m` `ESC a` `ESC b` `ESC >` `ESC ]`
  `ESC d` `ESC v` `ESC L` `ESC /` `ESC e` `ESC f` `ESC c8` `FS 05F` `FS 02ER/EF` `EM`）
- **解像度は「MultiImpact700 の解像度は 160dpi のみ」**と明記されています。
  移植でいちばん怖い解像度の違いが、700 シリーズ内では起きません

ただし**実機で試していません**。同じ表で `ESC n0` / `ESC n1`（NHS/SHS パイカ）が
XX2/JX3 では「×（無）」になっているとおり、**700XX と XX2/JX3 でもコマンドに差はあります**。
このドライバはそれらを使っていませんが、「動くはず」以上のことは言えません。

印字可能範囲は機種ごとに違うので、いずれにせよ `forms.conf` の調整が必要です。

## 動作環境

| | |
|---|---|
| 検証済み | **macOS 27.0** および **26.6.2**（いずれも Apple Silicon、実機で印刷まで確認） |
| たぶん動く | macOS 11 以降（**未検証**） |

26 から 27 へのアップグレードでは、**ドライバ・キュー・PPD がそのまま残り、印刷も問題ありません**
（未署名のフィルタもそのまま動きました）。ただし **CUPS の Web 設定画面が無効化されています**。
キューの既定値は `mi700default` を使ってください（[詳しくは下記](#ブラウザから設定するmacos-26-まで)）。

フィルタは arm64 と x86_64 のユニバーサルバイナリとしてビルドされます。
ビルドには **Xcode Command Line Tools**（`clang`）と **python3** が要ります。

```bash
xcode-select --install
```

> **注意:** Apple は PPD ベースの従来型プリンタドライバを非推奨としており、
> 将来の macOS で動かなくなる可能性があります。このプリンタは AirPrint に対応できないため、
> そうなった場合は CUPS を別途用意するなどの回避が必要になります。
> **macOS 27.0 の時点では問題なく動いています**（実機で印刷まで確認）。

## インストール

**[Releases](https://github.com/yukino1230/multiimpact-700xx-macos-driver/releases/latest) から
`mi700-4.0.pkg` をダウンロードして、ダブルクリックするだけです。**
Developer ID で署名し、Apple の公証も通してあるので、警告は出ません。

確かめたい場合は、こうすると署名が見られます。

```bash
spctl -a -vvv -t install mi700-4.0.pkg
# accepted / source=Notarized Developer ID と出れば正常です
```

リポジトリを clone した場合は、同梱のパッケージをそのまま使えます。

```bash
sudo installer -pkg dist/mi700-4.0.pkg -target /
```

`forms.conf` を編集したときなど、自分でビルドする場合はこうです
（[署名と公証](docs/tools.md#署名と公証配布する人向け)）。

```bash
./driver/build-pkg.sh 4.0
sudo installer -pkg build/mi700-4.0.pkg -target /
```

## プリンタを追加する

**ターミナルは使いません。**「設定」から追加します。

1. 「設定」→「プリンタとスキャナ」→「**プリンタ、スキャナ、またはファクスを追加…**」
2. 上部の **IP** タブを選ぶ
3. **アドレス**: プリンタの IP
4. **プロトコル**: **HP Jetdirect - ソケット**（LPD ではありません）
5. **キュー**: 空欄のまま
6. **名前**: 好きな名前。**ここが印刷ダイアログに表示されます**
7. **ドライバ**: `NEC MultiImpact 700XX` を選ぶ

7 は多くの場合**自動で選ばれます**。PPD が `/Library/Printers/PPDs/Contents/Resources/` に
入るので、macOS が SNMP で聞いた機種名（`hrDeviceDescr`）と PPD の `*ModelName` を
突き合わせてくれるためです。自動で選ばれなければ「ソフトウェアを選択…」から手で選びます。

「場所」欄には SNMP の `sysLocation` が入ります（[表示を整える](docs/network.md#プリンタを追加の表示を整える任意)）。

ターミナル派には1行で済むコマンドもあります。削除は `sudo mi700setup --remove <キュー名>` です。

```bash
sudo mi700setup <プリンタのIP> [キュー名]
```

**プリンタの IP がまだ決まっていない場合は、先に[ネットワークの設定](docs/network.md)を見てください。**
IP が変わるとキューは壊れるので、固定 IP にしておくことを勧めます。

同じ IP に対して名前違いのキューをいくつでも作れるので、**用途ごとに分ける**こともできます
（後述の「[用途ごとにキューを分ける](#用途ごとにキューを分ける)」）。

## 使い方

印刷ダイアログの「給紙」で給紙口を、「MultiImpact 700XX」の欄で排出方向と
印刷品質を選びます。コマンドラインからは次のように指定します。

```bash
lp -d <キュー名> -o PageSize=Cont10x11 -o InputSlot=reartractor -o OutputMode=std-bi file.pdf
```

| オプション | 値 |
|---|---|
| `InputSlot` | `feeder`（シートフィーダ）/ `guide`（シートガイド）/ `reartractor` / `fronttractor` |
| `OutputBin` | `front`（手前排紙）/ `rear`（スタッカ）※カット紙のみ |
| `OutputMode` | `std-bi` / `std-uni` / `draft-bi` / `draft-uni` / `none`（本体設定に従う） |
| `MI700Bottom` | ボトム領域の行数 `0` / `3` / `6`（本体への宣言のみ。印字は止まりません。後述） |

`-uni`（片方向印字）は遅くなりますが、双方向印字によるバンド境界のズレが出ません。

### よく使う設定を既定にする

macOS の「最後に使用した設定」はこのドライバでは復元されません（Apple 側の挙動で、
PPD からは制御できません）。代わりにキューの既定値を設定できます。

```bash
mi700default                                    # 現在の既定値と選べる値を表示
sudo mi700default 給紙=リアトラクタ 用紙=Cont10x45
```

#### ブラウザから設定する（macOS 26 まで）

> **macOS 27 では使えません。** CUPS の Web インターフェースが無効化され、
> `/printers/` も `/admin` も「ウェブインターフェイスが無効になっています」を返すだけになりました。
> **`cupsctl` の設定一覧から `WebInterface` の項目自体が消えている**ので、
> 有効に戻す手段もありません（実機で確認）。macOS 27 以降は `mi700default` を使ってください。

macOS 26 までは、CUPS の管理画面でも同じことができました。

```bash
sudo cupsctl WebInterface=yes
```

<http://localhost:631/printers/> でキューを選び、**Administration → Set Default Options**。
Mac の管理者アカウントで認証を求められます。給紙口・用紙サイズ・排出方向・印刷品質を
選んで保存すれば、`mi700default` と同じ結果になります。用が済んだら
`sudo cupsctl WebInterface=No` で閉じます。

### 用途ごとにキューを分ける

同じ IP に対して、名前違いのキューをいくつでも作れます。**給紙口と用紙を固定したキューを
用途ごとに用意しておくと、印刷ダイアログでプリンタを選ぶだけで設定が決まります。**
「最後に使用した設定」が復元されない以上、これがいちばん確実な方法です。

```bash
sudo mi700setup <IP> MI700_denpyo
sudo mi700default -p MI700_denpyo 給紙=リアトラクタ 用紙=Cont10x45

sudo mi700setup <IP> MI700_a4
sudo mi700default -p MI700_a4 給紙=シートフィーダ 用紙=A4
```

「設定」→「プリンタとスキャナ」から作っても構いません。その場合は作ったあとに
既定値を設定します。

> **キュー名に日本語を使うと内部名が壊れます。** 印刷ダイアログに出る表示名は日本語のまま
> ですが、CUPS 内部のキュー名は `NEC_MultiImpact_700XX_c_7c__c__c_5c__...` のように
> 変換されます。`lp -d` や `mi700default -p` で指定しにくくなるので、**キュー名は ASCII に
> しておくのが無難**です。内部名は `lpstat -p` で確認できます。
>
> 表示名だけ日本語にしたい場合は、ASCII のキュー名で作ってから説明を付け替えます。
>
> ```bash
> sudo lpadmin -p MI700_denpyo -D "MultiImpact 700XX 伝票"
> ```

### プリンタの状態を見る

```bash
mi700status <IP>
```

## 用紙を追加する

`forms.conf` は次のタブ区切りです。

```
名前	表示名	幅mm	高さmm	左mm	右mm	上mm	下mm
```

5〜8列目は**ページ座標での印字不可領域**です。ページの原点は用紙の左上ではなく
「印字ヘッドが到達できる最も左上の位置」なので、その分だけ右と下がはみ出します。
ここを用紙全面にすると**用紙外に印字しようとしてプリンタがエラーで停止します**（後述）。

編集したら PPD を作り直します。

```bash
sudo mi700setup <IP> <キュー名>     # forms.conf が新しければ PPD を再生成する
```

印字位置が怪しいときは、1枚刷って原点を実測できます。

```bash
mi700align Cont10x11 > a.ps      # 10mm 刻みの目盛りパターン
```

読み方は「[付属コマンド](docs/tools.md#mi700align--印字位置を実測する)」に、
連続紙をどう扱っているかは「[調べて分かったこと](docs/findings.md#macos-で連続紙は扱えないのか)」にあります。

## 困ったときは

| 症状 | 見るところ |
|---|---|
| **プリンタがアラームで止まる**（パネルが点滅する） | 用紙外に印字しようとしています。用紙サイズの選び間違いか、`forms.conf` の印字可能範囲がその機体に合っていません。[`mi700align`](docs/tools.md#mi700align--印字位置を実測する) で1枚測ってください |
| **印字位置が全体にずれる**（連続紙） | 位置合わせはトラクタを物理的に動かして行います。ソフトでの補正はわざと入れていません |
| **用紙切れ・カバー開で止まる** | 印刷キューに理由が出ます。`mi700status <IP>` でも見られます |
| **キューが停止する／印刷が始まらない** | プリンタの電源と IP を確認してください。`nc -z -v <IP> 9100` で疎通が見られます。IP が変わるとキューは壊れます（[固定 IP にする](docs/network.md#ip-は固定する)） |
| **アイコンが出ない** | 3.x から上げた場合、キューが古い PPD を抱えたままです。`sudo mi700setup <IP> <キュー名>` で入れ直してください |
| **iPhone から印刷したい** | [iPhone・iPad から印刷する](docs/airprint.md) を見てください |

見慣れない用紙を足すときは、**まず `mi700align` で1枚刷って位置を確かめてください。**
用紙外への印字はプリンタを停止させます。

## もっと詳しく

使うだけなら、ここから先は読まなくて構いません。

| | |
|---|---|
| **[iPhone・iPad から印刷する](docs/airprint.md)** | AirPrint のプリンタとして常駐させる（`mi700app` / `mi700ipp`）。ドライバを入れていない Mac からも印刷できます |
| **[ネットワークと共有](docs/network.md)** | LAN ボードの設定、固定 IP、1台の Mac を経由した共有、Tailscale で LAN の外から |
| **[付属コマンド・移植](docs/tools.md)** | 紙を使わない検証の道具、アイコン、署名と公証、他機種への移植 |
| **[調べて分かったこと](docs/findings.md)** | macOS と連続紙、PPD の先（IPP Everywhere / PAPPL）、実機で確かめた記録 |

## 参考資料

NEC が公開している資料です（本リポジトリには同梱していません）。

- MultiImpact 700 ユーザーズマニュアル
- 201PL リファレンスマニュアル
- ドットプリンタ 用紙設計説明書

プリンタ側のセットアップ（LAN ボードの IP 設定、Windows での動作確認）はこちらに記録があります。

- [NEC MultiImpact 700XX : 古いインパクトプリンターのセットアップ](https://caciocavallo.jp/?p=1116)

- [OpenPrinting: pr201 ドライバ](https://www.openprinting.org/driver/pr201/) — Ghostscript の PR201 出力デバイス

## 免責

個人が実機1台で作ったものです。**無保証**で、NEC とは無関係です。
用紙外への印字はプリンタを停止させます。見慣れない用紙を追加するときは、
まず `mi700align` で1枚刷って位置を確かめてください。

## ライセンス

MIT License です。詳しくは [LICENSE](LICENSE) を見てください。
