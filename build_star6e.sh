#!/bin/bash
# Build the FUSION msposd (OSD + AI detection boxes in one RGN region) for the
# Wyvern's SSC338Q (star6e).  Run from WSL:  wsl bash build_star6e.sh
#
# This drives msposd's OWN upstream Makefile with the same variables the OpenIPC
# buildroot package uses (general/package/msposd/msposd.mk):
#
#   make CC=<target-gcc> TOOLCHAIN=<staging> DRV=<osdrv>/lib star6e OUTPUT=...
#
# Linking is against the REAL vendor libraries and a real libevent, so DT_NEEDED
# and SONAMEs come out exactly like the stock binary - no stub libraries.
# Run ../tools/setup_devenv.sh once to populate $SDK.
set -e

SDK="${SDK:-$HOME/openipc-dev/sdk}"
TC="${TC:-$HOME/tc/armv7-eabihf--glibc--stable-2023.11-1}"
CC="$TC/bin/arm-buildroot-linux-gnueabihf-gcc"
READELF="$TC/bin/arm-buildroot-linux-gnueabihf-readelf"
STAGING="$SDK/staging"
DRV="$SDK/infinity6e/lib"
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

for p in "$CC" "$STAGING/usr/include/event2/event.h" "$DRV/libmi_rgn.so"; do
    [ -e "$p" ] || { echo "missing: $p (run tools/setup_devenv.sh)"; exit 1; }
done

echo "=== BUILD msposd fusion (star6e, real vendor libs) ==="
make -s clean >/dev/null 2>&1 || true
# Arch flags and the libevent search path ride on CC, the way buildroot's
# TARGET_CC wrapper does. Passing CFLAGS= on the command line would override the
# -D__SIGMASTAR__ defines that the star6e target appends, silently building the
# Goke variant instead.
# -ffunction-sections/-fdata-sections with --gc-sections drops code nothing
# calls. That matters here: the camera's overlay filesystem has well under a
# megabyte free, and this binary links a PNG codec, a TrueType rasteriser and a
# pile of pixel-format converters of which it uses a fraction.
make star6e \
    CC="$CC -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -L$STAGING/usr/lib" \
    TOOLCHAIN="$STAGING" \
    DRV="$DRV" \
    OUTPUT=/tmp/msposd_fusion
echo "build ok"

echo "=== VERIFY ==="
file /tmp/msposd_fusion | cut -c1-95
echo "--- DT_NEEDED (must match the stock binary) ---"
$READELF -d /tmp/msposd_fusion | grep NEEDED
echo "--- highest glibc requirement (camera has 2.38) ---"
$READELF -V /tmp/msposd_fusion 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -Vu | tail -3
cp /tmp/msposd_fusion "$HERE/msposd_fusion_star6e"
ls -l "$HERE/msposd_fusion_star6e"
