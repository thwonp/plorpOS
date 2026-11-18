#!/usr/bin/env python3
"""No "Hit any key to stop autoboot" wait on the RG DS image (plorpos-rgds.12).

ROCKNIX's u-boot counts down two seconds before it loads the kernel. Its
environment is built in (nowhere to save one), so the image's copy of u-boot
is changed: bootdelay=2 becomes bootdelay=0 in the default environment, and
the sha256 the loader before it checks is recomputed.

u-boot is a FIT at sector 16384: a device-tree header naming each part's
offset, size and hash, then the parts. Anything not exactly as expected leaves
the image alone and says so.

Usage: mk/rgds-uboot-nodelay.py <image>
"""
import hashlib, struct, sys

FIT = 16384 * 512
OLD, NEW = b"bootdelay=2\0", b"bootdelay=0\0"


def props(fit, node):
    """The properties of one node of a flattened device tree, by path."""
    off_dt, off_str = struct.unpack(">II", fit[8:16])
    p, path, out = off_dt, [], {}
    while True:
        tag = struct.unpack(">I", fit[p:p + 4])[0]
        p += 4
        if tag == 1:                                   # a node begins
            end = fit.index(b"\0", p)
            path.append(fit[p:end].decode())
            p = (end + 4) & ~3
        elif tag == 2:                                 # and ends
            path.pop()
        elif tag == 3:                                 # a property
            size, name = struct.unpack(">II", fit[p:p + 8])
            if "/".join(path) == node:
                key = fit[off_str + name:fit.index(b"\0", off_str + name)].decode()
                out[key] = (p + 8, fit[p + 8:p + 8 + size])
            p = (p + 8 + size + 3) & ~3
        elif tag == 9:
            return out


def main(path):
    with open(path, "r+b") as f:
        f.seek(FIT)
        magic, total = struct.unpack(">II", f.read(8))
        if magic != 0xD00DFEED or total > 65536:
            return "no u-boot FIT at sector 16384"
        f.seek(FIT)
        fit = f.read(total)
        image, digest = props(fit, "/images/u-boot"), props(fit, "/images/u-boot/hash")
        if not {"data-offset", "data-size"} <= image.keys() or digest.get("algo", (0, b""))[1] != b"sha256\0":
            return "the FIT is not laid out as expected"
        start = FIT + total + struct.unpack(">I", image["data-offset"][1])[0]
        size = struct.unpack(">I", image["data-size"][1])[0]
        f.seek(start)
        data = f.read(size)
        if hashlib.sha256(data).digest() != digest["value"][1]:
            return "u-boot does not match its own hash"
        if data.count(OLD) != 1:
            return "bootdelay=2 is not in this u-boot exactly once"
        data = data.replace(OLD, NEW)
        f.seek(start)
        f.write(data)
        f.seek(FIT + digest["value"][0])
        f.write(hashlib.sha256(data).digest())
    print("u-boot: bootdelay 2 -> 0")


if __name__ == "__main__":
    err = main(sys.argv[1])
    if err:
        print("u-boot left as it is: " + err, file=sys.stderr)
