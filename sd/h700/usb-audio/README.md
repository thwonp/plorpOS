# USB audio modules for the RG35XX H and RG35XX SP

Their stock Anbernic kernels (4.9.170, BaseOS targets `rg35xxh` #14 and
`rg35xxsp` #13, the same config) have `# CONFIG_SND_USB_AUDIO is not set`, so a
USB-C DAC enumerates but gets no sound card. The RG SP's kernel has it built in.
`System/launch_frontend.sh` loads these three at boot when
`/sys/module/snd_usb_audio` is missing (plorpos-2wl.15).

- `snd-hwdep.ko`, `snd-usbmidi-lib.ko`, `snd-usb-audio.ko`: vanilla
  linux-4.9.170 `sound/usb/` and `sound/core/hwdep*.c`, built as modules
  against the vendor config with `CONFIG_SND_HWDEP=m`, `CONFIG_SND_USB_AUDIO=m`.
- vermagic `4.9.170 SMP preempt mod_unload modversions aarch64`. The vendor
  `Module.symvers` came from BaseOS's `tools/kernel_abi.py` (BaseOS
  `./fetch-prepared.sh rg35xxh`); all imports CRC-match.
- `build-modules.sh` is the build, run in `ubuntu:20.04` with the kernel
  tarball in `/cache` and `kernel.config` + `Module.symvers` in `/w`.

Checked on an RG35XX SP (2026-11-22): the modules load, and a KA13 and a
CX31993 dongle each became a card that plorpOS routed audio to.

Upstream fix: a BaseOS change for these two targets (plorpos-2wl.17).
