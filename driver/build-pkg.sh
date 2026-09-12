#!/bin/bash
# MultiImpact 700XX ドライバの配布パッケージを作る
#   ~/Developer/mi700/driver/build-pkg.sh [バージョン]
#
# 署名と公証は「できるならする」。Developer ID 証明書が無い環境では
# 未署名のパッケージができるだけで、ビルドは失敗しない。
#   MI700_NOTARY=<notarytool のプロファイル名>   公証まで行う
#   MI700_SIGN_APP= / MI700_SIGN_PKG=            証明書を明示する(既定は自動検出)
set -euo pipefail
VER="${1:-1.1}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD=~/Developer/mi700/build
ROOT="$BUILD/payload"
export COPYFILE_DISABLE=1                       # ._ ファイルを作らせない

# Developer ID 証明書を探す(指定があればそれを使う)
find_id() { security find-identity -v ${2:-} 2>/dev/null \
              | sed -n "s/.*\"\($1: [^\"]*\)\".*/\1/p" | head -1; }
SIGN_APP="${MI700_SIGN_APP:-$(find_id 'Developer ID Application' '-p codesigning')}"
SIGN_PKG="${MI700_SIGN_PKG:-$(find_id 'Developer ID Installer')}"
NOTARY="${MI700_NOTARY:-}"

rm -rf "$BUILD"; mkdir -p "$ROOT" "$BUILD/scripts"
mkdir -p "$ROOT/usr/local/libexec/cups/filter" "$ROOT/usr/local/bin" \
         "$ROOT/usr/local/share/mi700/presets" \
         "$ROOT/Library/Printers/PPDs/Contents/Resources"

echo "==> CUPS フィルタをビルド(ユニバーサル)"
clang -O2 -Wall -arch arm64 -arch x86_64 \
      -o "$ROOT/usr/local/libexec/cups/filter/rastertomi700" "$HERE/rastertomi700.c"
chmod 755 "$ROOT/usr/local/libexec/cups/filter/rastertomi700"

FILTER="$ROOT/usr/local/libexec/cups/filter/rastertomi700"
if [ -n "$SIGN_APP" ]; then
  echo "==> フィルタに署名: $SIGN_APP"
  # 公証には Hardened Runtime と署名時刻が要る
  codesign --force --options runtime --timestamp --sign "$SIGN_APP" "$FILTER"
  codesign --verify --strict "$FILTER"
else
  echo "==> Developer ID Application が無いので未署名のままにします"
fi

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
PKG="$BUILD/mi700-$VER.pkg"
pkgbuild --root "$ROOT" --scripts "$BUILD/scripts" \
         --identifier jp.yukino.mi700 --version "$VER" \
         --ownership recommended --install-location / \
         "$PKG"

if [ -n "$SIGN_PKG" ]; then
  echo "==> パッケージに署名: $SIGN_PKG"
  productsign --sign "$SIGN_PKG" "$PKG" "$PKG.signed"
  mv "$PKG.signed" "$PKG"
  pkgutil --check-signature "$PKG" | head -3
else
  echo "==> Developer ID Installer が無いので未署名のままにします"
fi

if [ -n "$NOTARY" ]; then
  [ -n "$SIGN_PKG" ] || { echo "公証には Developer ID Installer の署名が要ります"; exit 1; }
  echo "==> 公証に出します(数分かかります)"
  xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY" --wait
  xcrun stapler staple "$PKG"        # 結果を pkg に貼って、オフラインでも通るようにする
  xcrun stapler validate "$PKG"
elif [ -n "$SIGN_PKG" ]; then
  echo "==> 公証はしていません。MI700_NOTARY=<プロファイル名> を付けると公証まで行います"
fi

echo
echo "できました: $PKG"
