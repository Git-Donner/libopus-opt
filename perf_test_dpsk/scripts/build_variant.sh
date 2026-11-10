#!/bin/bash
# build_variant.sh <base_src> <out_dir> <variant>
# 从基础源码复制一份，用指定编译选项构建 zhujing 库，把 include+lib 产出到 out_dir。
# variant: v0 | v1 | v2 | v3
set -u
BASE="$1"; OUT="$2"; VARIANT="${3:-v1}"
BUILD="/tmp/zj_build_$VARIANT"
rm -rf "$BUILD"
cp -r "$BASE" "$BUILD"
cd "$BUILD" || exit 1
rm -rf .git .libs autom4te.cache
find . \( -name '*.o' -o -name '*.lo' -o -name '*.a' -o -name '*.so*' \) -exec rm -f {} \; 2>/dev/null

case "$VARIANT" in
  v0) FIX="--enable-fixed-point"; EXTRA=""; CFLAGS=""; LDFLAGS="";;
  v1) FIX="--enable-fixed-point"; EXTRA="--disable-hardening"; CFLAGS="-O3 -mtune=native -fno-semantic-interposition -flto"; LDFLAGS="-flto";;
  v2) FIX="--enable-fixed-point"; EXTRA="--disable-hardening"; CFLAGS="-O3 -mtune=native -fno-semantic-interposition -flto -march=armv8.2-a+dotprod"; LDFLAGS="-flto";;
  v3) FIX="--enable-fixed-point"; EXTRA="--disable-hardening"; CFLAGS="-O3 -mtune=native -fno-semantic-interposition -flto -march=native"; LDFLAGS="-flto";;
  vf) FIX=""; EXTRA=""; CFLAGS=""; LDFLAGS="";;   # 浮点 + 与 Source 同款 stock 编译(-O2)
  vfo) FIX=""; EXTRA="--disable-hardening"; CFLAGS="-O3 -mtune=native -fno-semantic-interposition -flto"; LDFLAGS="-flto";; # 浮点 + 编译优化
esac
COMMON="--enable-rtcd --enable-intrinsics --enable-custom-modes --disable-maintainer-mode"

echo "[$VARIANT] autoreconf ..."
autoreconf -fi >/tmp/v_${VARIANT}_autoreconf.log 2>&1 || { echo "$VARIANT AUTORECONF_FAIL"; tail -5 /tmp/v_${VARIANT}_autoreconf.log; exit 1; }
echo "[$VARIANT] configure ..."
if [ -n "$CFLAGS$LDFLAGS" ]; then
  CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" ./configure $FIX $COMMON $EXTRA >/tmp/v_${VARIANT}_conf.log 2>&1
else
  # 未显式给定 CFLAGS：交由 autoconf 采用默认 -g -O2（= 原始/stock 状态）
  ./configure $FIX $COMMON $EXTRA >/tmp/v_${VARIANT}_conf.log 2>&1
fi
RET=$?
if [ $RET -ne 0 ]; then echo "$VARIANT CONFIGURE_FAIL"; tail -8 /tmp/v_${VARIANT}_conf.log; exit 1; fi
grep -E 'FIXED_POINT|OPUS_ARM_MAY_HAVE_SVE2|OPUS_HAVE_RTCD|ENABLE_HARDENING' config.h | head -5
echo "[$VARIANT] touch + make ..."
touch aclocal.m4 configure config.h.in Makefile.in config.status Makefile config.h stamp-h1 2>/dev/null
make -j8 >/tmp/v_${VARIANT}_make.log 2>&1 || { echo "$VARIANT MAKE_FAIL"; tail -15 /tmp/v_${VARIANT}_make.log; exit 1; }
echo "[$VARIANT] make ok"

rm -rf "$OUT"
mkdir -p "$OUT"
cp -r include "$OUT"/include
mkdir -p "$OUT/.libs"
test -f .libs/libopus.a && cp .libs/libopus.a "$OUT/.libs/libopus.a"
echo "[$VARIANT] output -> $OUT"
ls -l "$OUT/.libs/libopus.a"
echo "VARIANT_DONE $VARIANT"
