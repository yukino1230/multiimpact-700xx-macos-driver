#!/bin/bash
# PAPPL 版をユニバーサル(arm64 + x86_64)で配るための下ごしらえ
#   driver/build-pappl-deps.sh
#
# PAPPL は TLS に OpenSSL(か GnuTLS)を要求し、Homebrew の OpenSSL は arm64 専用
# なので、そのままだと Apple Silicon 専用の実行ファイルしか作れない。
# ここで OpenSSL を両アーキテクチャでビルドして lipo で1つにまとめ、その上で
# PAPPL をユニバーサルにビルドする。できたものは build/deps/ に残り、
# build-pappl.sh が自動で見つける。
#
#   MI700_OPENSSL_TGZ  OpenSSL のソース(既定: Homebrew のキャッシュを使う)
#   MI700_PAPPL_SRC    PAPPL のソース(既定: build/pappl-1.4.12)
#
# 30〜60分かかる。1回やれば済む。
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/../build"
DEPS="$BUILD/deps"
ARCHS="arm64 x86_64"
JOBS=$(sysctl -n hw.ncpu)

TGZ="${MI700_OPENSSL_TGZ:-}"
if [ -z "$TGZ" ]; then
  command -v brew >/dev/null || { echo "OpenSSL のソースが要ります(MI700_OPENSSL_TGZ で指定)"; exit 1; }
  TGZ=$(brew --cache --build-from-source openssl@3)
  [ -f "$TGZ" ] || brew fetch --build-from-source openssl@3 >/dev/null
fi
[ -f "$TGZ" ] || { echo "OpenSSL のソースが見つかりません: $TGZ"; exit 1; }
PAPPL_SRC="${MI700_PAPPL_SRC:-$BUILD/pappl-1.4.12}"
[ -f "$PAPPL_SRC/configure" ] || { echo "PAPPL のソースが見つかりません: $PAPPL_SRC"; exit 1; }

mkdir -p "$DEPS"
echo "==> OpenSSL: $TGZ"

for arch in $ARCHS; do
  pfx="$DEPS/openssl-$arch"
  if [ -f "$pfx/lib/libssl.a" ]; then echo "    $arch は済み"; continue; fi
  src="$DEPS/src-openssl-$arch"
  rm -rf "$src"; mkdir -p "$src"
  tar xzf "$TGZ" -C "$src" --strip-components 1
  echo "    $arch をビルド(数分かかります)"
  ( cd "$src"
    ./Configure "darwin64-$arch-cc" no-shared no-tests no-docs no-legacy \
                --prefix="$pfx" --libdir=lib >/dev/null
    make -j"$JOBS" >/dev/null
    make install_sw >/dev/null )
  rm -rf "$src"
done

echo "==> OpenSSL を lipo で1つにまとめる"
UNI="$DEPS/openssl-universal"
rm -rf "$UNI"; mkdir -p "$UNI/lib/pkgconfig"
cp -R "$DEPS/openssl-arm64/include" "$UNI/include"
for lib in libssl.a libcrypto.a; do
  lipo -create -output "$UNI/lib/$lib" "$DEPS/openssl-arm64/lib/$lib" "$DEPS/openssl-x86_64/lib/$lib"
done
for pc in "$DEPS/openssl-arm64/lib/pkgconfig/"*.pc; do
  sed "s|$DEPS/openssl-arm64|$UNI|g" "$pc" > "$UNI/lib/pkgconfig/$(basename "$pc")"
done
lipo -archs "$UNI/lib/libssl.a"

echo "==> PAPPL をユニバーサルでビルド"
P="$DEPS/pappl"
if [ ! -d "$P" ]; then
  mkdir -p "$P"
  ( cd "$PAPPL_SRC" && tar cf - . ) | ( cd "$P" && tar xf - )
  ( cd "$P" && make clean >/dev/null 2>&1 || true )
fi
( cd "$P"
  PKG_CONFIG_PATH="$UNI/lib/pkgconfig" \
  CFLAGS="-arch arm64 -arch x86_64" LDFLAGS="-arch arm64 -arch x86_64" \
    ./configure --disable-shared --disable-libjpeg --disable-libpng --with-tls=openssl >/dev/null
  make -j"$JOBS" -C pappl libpappl.a >/dev/null )
lipo -archs "$P/pappl/libpappl.a"

echo
echo "できました:"
echo "  $P/pappl/libpappl.a"
echo "  $UNI/lib/libssl.a  $UNI/lib/libcrypto.a"
echo "このあと driver/build-pappl.sh を実行すると、ユニバーサルの mi700pappl ができます。"
