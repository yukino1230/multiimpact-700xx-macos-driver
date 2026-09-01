#!/bin/bash
# NEC MultiImpact 700XX 用 macOS ドライバのインストール
#   sudo ~/Developer/mi700/driver/install.sh --ip <プリンタのIP> [--queue MultiImpact700XX]
#   sudo ~/Developer/mi700/driver/install.sh --uninstall
set -euo pipefail

IP=""
QUEUE="MultiImpact700XX"
UNINSTALL=0
while [ $# -gt 0 ]; do
  case "$1" in
    --ip)        IP="$2"; shift 2 ;;
    --queue)     QUEUE="$2"; shift 2 ;;
    --uninstall) UNINSTALL=1; shift ;;
    *) echo "不明な引数: $1"; exit 1 ;;
  esac
done

[ "$(id -u)" = "0" ] || { echo "sudo で実行してください"; exit 1; }
[ "$UNINSTALL" = 1 ] || [ -n "$IP" ] || { echo "--ip でプリンタのIPを指定してください"; exit 1; }
USER_HOME="$(eval echo ~"${SUDO_USER:-$USER}")"
SRC="$USER_HOME/Developer/mi700/driver"
BIN="$USER_HOME/bin"

if [ "$UNINSTALL" = "1" ]; then
  echo "==> キューを削除: $QUEUE"
  lpadmin -x "$QUEUE" 2>/dev/null || echo "    (キューは存在しませんでした)"
  rm -f /usr/local/libexec/cups/filter/pdftomi700
  rm -f /usr/local/bin/pdf2mi700 /usr/local/bin/mi700print /usr/local/bin/mi700decode
  rm -rf /usr/local/share/mi700
  echo "完了しました。"
  exit 0
fi

echo "==> 前提を確認"
for t in pdf2mi700 mi700print mi700decode; do
  [ -x "$BIN/$t" ] || { echo "    $BIN/$t がありません"; exit 1; }
done
[ -x "$SRC/pdftomi700" ] || { echo "    $SRC/pdftomi700 がありません"; exit 1; }
command -v gs >/dev/null || { echo "    Ghostscript(gs) がありません"; exit 1; }
echo "    OK"

echo "==> ディレクトリを作成"
install -d -o root -g wheel -m 755 /usr/local/bin
install -d -o root -g wheel -m 755 /usr/local/libexec/cups/filter
install -d -o root -g wheel -m 755 /usr/local/share/mi700/presets

echo "==> 変換ツールを設置"
for t in pdf2mi700 mi700print mi700decode; do
  install -o root -g wheel -m 755 "$BIN/$t" "/usr/local/bin/$t"
  echo "    /usr/local/bin/$t"
done

echo "==> CUPS フィルタを設置"
install -o root -g wheel -m 755 "$SRC/pdftomi700" /usr/local/libexec/cups/filter/pdftomi700

echo "==> 用紙定義とプリセットを設置"
[ -f "$USER_HOME/Developer/mi700/forms.conf" ] && \
  install -o root -g wheel -m 644 "$USER_HOME/Developer/mi700/forms.conf" /usr/local/share/mi700/forms.conf
for f in "$USER_HOME/Developer/mi700/presets"/*.prn; do
  [ -e "$f" ] || continue
  install -o root -g wheel -m 644 "$f" "/usr/local/share/mi700/presets/$(basename "$f")"
done
echo "    $(ls /usr/local/share/mi700/presets/*.prn 2>/dev/null | wc -l | tr -d ' ') 件のプリセット"

echo "==> PPD を生成（現在の forms.conf を反映）"
PPD=/usr/local/share/mi700/MI700XX.ppd
sudo -u "${SUDO_USER:-$USER}" python3 "$SRC/mkppd.py" > "$PPD"
chown root:wheel "$PPD"; chmod 644 "$PPD"
if cupstestppd "$PPD" | grep -qE "FAIL|失敗"; then
  cupstestppd "$PPD" | grep -E "FAIL|失敗"
  echo "PPD に問題があります"; exit 1
fi
echo "    OK ($(grep -c '^\*PageSize ' "$PPD") 種類の用紙サイズ)"

echo "==> フィルタの動作を確認"
printf '%%!PS\n/Helvetica findfont 12 scalefont setfont 50 700 moveto (t) show showpage\n' > /tmp/mi700_probe.ps
gs -q -dNOPAUSE -dBATCH -sDEVICE=pdfwrite -sOutputFile=/tmp/mi700_probe.pdf /tmp/mi700_probe.ps
if ! /usr/local/libexec/cups/filter/pdftomi700 1 t t 1 "MI700Source=cut" /tmp/mi700_probe.pdf > /tmp/mi700_probe.prn 2>/dev/null \
   || [ ! -s /tmp/mi700_probe.prn ]; then
  echo "    フィルタが動作しません"; exit 1
fi
echo "    OK ($(wc -c < /tmp/mi700_probe.prn | tr -d ' ') バイト生成)"
rm -f /tmp/mi700_probe.*

echo "==> プリンタキューを登録: $QUEUE"
lpadmin -p "$QUEUE" -E -v "socket://$IP:9100" -P "$PPD" \
        -D "NEC MultiImpact 700XX" -L "LAN" -o printer-is-shared=false

echo
echo "完了しました。印刷ダイアログに「$QUEUE」が出ます。"
echo "  用紙サイズを増やす: ~/Developer/mi700/forms.conf に1行足して、このスクリプトを再実行"
echo "  アンインストール  : sudo $0 --uninstall"
