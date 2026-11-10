#!/usr/bin/env bash
# build_mbedtls.sh — mbedTLS (static) for gta2-online's wss:// (port/native/net/tls_conn.cpp).
#
# The consoles' OpenSSL differ (3 on ROCKNIX, 1.1 on AmberELEC...), so gta2-online carries its TLS
# inside. The release archive is downloaded once into downloads/ (never committed), checked
# against the sha256 published with the release, and built into <prefix> with clang, apart
# from the game's sources (their mandatory flags are checked in compile_commands.json).
#
#   scripts/native/build_mbedtls.sh <prefix> <Debug|Release>
# Prints nothing more when <prefix> already holds this version.
set -euo pipefail

VERSION="3.6.6"
SHA256="8fb65fae8dcae5840f793c0a334860a411f884cc537ea290ce1c52bb64ca007a"
URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$VERSION/mbedtls-$VERSION.tar.bz2"

PREFIX="$1"
TYPE="${2:-Release}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARCHIVE="$ROOT/downloads/mbedtls-$VERSION.tar.bz2"
# MBEDTLS_PLATFORM_ZEROIZE_ALT: built against glibc >= 2.25, mbedTLS would call explicit_bzero,
# and gta2-online would need GLIBC 2.25 where the game needs 2.17: tls_conn.cpp has the function.
FLAGS="-ffunction-sections -fdata-sections -DMBEDTLS_PLATFORM_ZEROIZE_ALT"
STAMP="$PREFIX/.mbedtls-$VERSION-$TYPE-$(echo "$FLAGS" | sha1sum | cut -c1-8)"

[[ -f "$STAMP" ]] && exit 0

mkdir -p "$ROOT/downloads"
if [[ ! -f "$ARCHIVE" ]]; then
  echo "==> mbedTLS $VERSION : téléchargement"
  curl -fsSL -o "$ARCHIVE.part" "$URL"
  mv "$ARCHIVE.part" "$ARCHIVE"
fi
echo "$SHA256  $ARCHIVE" | sha256sum -c --quiet - \
  || { echo "xx $ARCHIVE : sha256 inattendu (attendu $SHA256)" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
tar xjf "$ARCHIVE" -C "$WORK"
echo "==> mbedTLS $VERSION : compilation ($TYPE)"
# Sections per function: the game's --gc-sections then drops what TLS does not use.
cmake -S "$WORK/mbedtls-$VERSION" -B "$WORK/build" -G Ninja \
  -DCMAKE_BUILD_TYPE="$TYPE" \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_C_FLAGS="$FLAGS" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DGEN_FILES=OFF \
  -DUSE_STATIC_MBEDTLS_LIBRARY=ON -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
  -DMBEDTLS_FATAL_WARNINGS=OFF >/dev/null
cmake --build "$WORK/build" >/dev/null
rm -rf "$PREFIX"
cmake --install "$WORK/build" >/dev/null
touch "$STAMP"
