set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq gcc-aarch64-linux-gnu make bc flex bison libssl-dev perl xz-utils libelf-dev >/dev/null 2>&1
tar -xJf /cache/linux-4.9.170.tar.xz -C /tmp
cd /tmp/linux-4.9.170
cp /w/kernel.config .config
printf 'CONFIG_SND_HWDEP=m\nCONFIG_SND_USB_AUDIO=m\n' >> .config
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
make HOSTCFLAGS="-O2 -fcommon" olddefconfig modules_prepare > /w/out/prepare.log 2>&1 || { tail -20 /w/out/prepare.log; exit 1; }
cp /w/Module.symvers Module.symvers
rm -rf /w/mod; mkdir -p /w/mod
cp sound/usb/*.c sound/usb/*.h sound/core/hwdep.c sound/core/hwdep_compat.c /w/mod/
cat > /w/mod/Makefile <<'M'
snd-usb-audio-objs := card.o clock.o endpoint.o format.o helper.o mixer.o mixer_quirks.o mixer_scarlett.o pcm.o proc.o quirks.o stream.o
snd-usbmidi-lib-objs := midi.o
snd-hwdep-objs := hwdep.o
obj-m += snd-hwdep.o snd-usbmidi-lib.o snd-usb-audio.o
M
make HOSTCFLAGS="-O2 -fcommon" M=/w/mod modules > /w/out/build.log 2>&1 || { grep -E "error|Error" /w/out/build.log | head -20; exit 1; }
grep -i "undefined\|WARNING: " /w/out/build.log | grep -v missing-attributes | head -20
aarch64-linux-gnu-strip --strip-debug /w/mod/*.ko
cp /w/mod/*.ko /w/out/
cp /w/mod/Module.symvers /w/out/Module.symvers.new 2>/dev/null || true
ls -la /w/out/*.ko
