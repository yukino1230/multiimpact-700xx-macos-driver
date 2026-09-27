#!/bin/bash
# PAPPL 版(mi700pappl)をビルドする
#   driver/build-pappl.sh [PAPPL のソースを展開した場所]
#
# PAPPL は Homebrew に無いので、ソースから用意する:
#   curl -LO https://github.com/michaelrsweet/pappl/releases/download/v1.4.12/pappl-1.4.12.tar.gz
#   tar xzf pappl-1.4.12.tar.gz && cd pappl-1.4.12
#   ./configure --disable-shared --disable-libjpeg --disable-libpng && make
# (libjpeg/libpng は画像ファイルの直接印刷にしか使わないので外してよい。
#  Homebrew のライブラリは arm64 専用なので、ユニバーサルにしたい場合は
#  Makedefs の -arch を揃えること)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
PAPPL="${1:-$HERE/../build/pappl-1.4.12}"
OUT="${MI700_OUT:-$HERE/../build/mi700pappl}"

[ -f "$PAPPL/pappl/libpappl.a" ] || { echo "PAPPL が見つかりません: $PAPPL"; exit 1; }
LIBS=$(sed -n 's/^LIBS[^=]*=//p' "$PAPPL/Makedefs" | head -1)

# PAPPL は TLS に OpenSSL か GnuTLS を要求する(macOS の Security.framework は使えない)。
# 既定では Homebrew の dylib にリンクされ、他の Mac に配れないので、静的な .a があれば
# そちらを使う。これで残る依存は macOS 標準のものだけになる(otool -L で確認できる)
SSL="${MI700_OPENSSL:-$(brew --prefix openssl@3 2>/dev/null || echo /opt/homebrew/opt/openssl@3)}"
if [ -f "$SSL/lib/libssl.a" ] && [ -f "$SSL/lib/libcrypto.a" ]; then
  LIBS=$(printf '%s' "$LIBS" | sed -e 's/-lssl//g' -e 's/-lcrypto//g')
  LIBS="$LIBS $SSL/lib/libssl.a $SSL/lib/libcrypto.a"
  echo "OpenSSL を静的にリンクします: $SSL"
else
  echo "警告: $SSL に静的な OpenSSL がありません。Homebrew の dylib に依存した実行ファイルになります"
fi

mkdir -p "$(dirname "$OUT")"
clang -O2 -Wall -o "$OUT" "$HERE/mi700pappl.c" "$HERE/mi700enc.c" \
      -I "$PAPPL" $LIBS "$PAPPL/pappl/libpappl.a" \
      -framework CoreFoundation -framework Security
echo "できました: $OUT"
echo "  MI700_FORMS=$HERE/../forms.conf $OUT server -o server-port=8631"
echo "  $OUT add -d MI700 -v socket://<IP>:9100 -m mi700"
