#!/bin/bash
# MultiImpact 700XX ドライバの配布パッケージを作る
#   ~/Developer/mi700/driver/build-pkg.sh [バージョン]
set -euo pipefail
VER="${1:-1.1}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD=~/Developer/mi700/build
ROOT="$BUILD/payload"
export COPYFILE_DISABLE=1                       # ._ ファイルを作らせない

rm -rf "$BUILD"; mkdir -p "$ROOT" "$BUILD/scripts"
mkdir -p "$ROOT/usr/local/libexec/cups/filter" "$ROOT/usr/local/bin" \
         "$ROOT/usr/local/share/mi700/presets" \
         "$ROOT/Library/Printers/PPDs/Contents/Resources"

echo "==> CUPS フィルタをビルド(ユニバーサル)"
clang -O2 -Wall -arch arm64 -arch x86_64 \
      -o "$ROOT/usr/local/libexec/cups/filter/rastertomi700" "$HERE/rastertomi700.c"
chmod 755 "$ROOT/usr/local/libexec/cups/filter/rastertomi700"

echo "==> PPD を生成"
MI700_FORMS=~/Developer/mi700/forms.conf python3 "$HERE/mkppd.py" > "$BUILD/MI700XX.ppd"
# ビルド時はフィルタがまだ /usr/local に無いので、その指摘だけ除外して判定する
if cupstestppd "$BUILD/MI700XX.ppd" 2>&1 | grep -E "FAIL|失敗" | grep -v "rastertomi700"; then
  echo "PPD に問題があります"; exit 1
fi
install -m 644 "$BUILD/MI700XX.ppd" "$ROOT/usr/local/share/mi700/MI700XX.ppd"
gzip -c "$BUILD/MI700XX.ppd" > "$ROOT/Library/Printers/PPDs/Contents/Resources/MI700XX.ppd.gz"
chmod 644 "$ROOT/Library/Printers/PPDs/Contents/Resources/MI700XX.ppd.gz"

echo "==> 付属ツールを配置"
install -m 644 ~/Developer/mi700/forms.conf "$ROOT/usr/local/share/mi700/forms.conf"
install -m 755 "$HERE/mkppd.py"    "$ROOT/usr/local/share/mi700/mkppd.py"
for f in ~/Developer/mi700/presets/*.prn; do
  [ -e "$f" ] && install -m 644 "$f" "$ROOT/usr/local/share/mi700/presets/"
done
for t in pdf2mi700 mi700print mi700decode mi700align mi700diff mi700preset; do
  [ -x "$HERE/../tools/$t" ] && install -m 755 "$HERE/../tools/$t" "$ROOT/usr/local/bin/$t"
done
install -m 755 "$HERE/mi700setup"   "$ROOT/usr/local/bin/mi700setup"
install -m 755 "$HERE/mi700default" "$ROOT/usr/local/bin/mi700default"
install -m 755 "$HERE/mi700status"  "$ROOT/usr/local/bin/mi700status"

echo "==> 拡張属性を除去"
xattr -cr "$ROOT" 2>/dev/null || true
find "$ROOT" -name '._*' -delete

cat > "$BUILD/scripts/postinstall" <<'EOS'
#!/bin/bash
# CUPS に新しい PPD を認識させる
killall -HUP cupsd 2>/dev/null || true
exit 0
EOS
chmod 755 "$BUILD/scripts/postinstall"

echo "==> パッケージを作成"
pkgbuild --root "$ROOT" --scripts "$BUILD/scripts" \
         --identifier jp.yukino.mi700 --version "$VER" \
         --ownership recommended --install-location / \
         "$BUILD/mi700-$VER.pkg"
echo
echo "できました: $BUILD/mi700-$VER.pkg"
