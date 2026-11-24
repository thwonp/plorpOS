#!/usr/bin/env python3
"""Build the fast-boot update image for the Miyoo Mini Flip (plorpos-f1p.38, .97).

The stock miyoo285_fw.img is a U-Boot script followed by raw partition
payloads (script ends at "% <- this is end of script symbol", padding 0xFF to
0x4000, first payload at 0x4000). This writes the same kind of file that
touches only:
  - customer  (mtd6, squashfs): /customer/main and demo.sh, see lean-customer.patch
  - miservice (mtd5, squashfs): mi_sys.ko with its 2 s start-up wait cut
  - the U-Boot env: the stock bootcmd without its `sleepms 1000;`
U-Boot itself, the kernel and rootfs are not written, so the stock SD update
(which rewrites all three of the above) stays the way back.

  mkfw.py --stock miyoo285_fw.img --extract customer customer.squashfs
  mkfw.py --stock miyoo285_fw.img --customer c.squashfs --miservice m.squashfs \\
          --build 202511240001 --out miyoo285_fw.img

A payload bigger than its partition is refused.
"""
import argparse, re, sys

POS = 0x4000
PARTS = {"customer": 0x770000, "miservice": 0x370000}
END = "% <- this is end of script symbol\n"
SLEEP = "sleepms 1000; "


def stock_script(d):
    end = 0
    while end < len(d) and 9 <= d[end] < 128:
        end += 1
    return d[:end].decode()


def stock_part(d, name):
    blk = re.search(r"# File Partition: %s\n(.*?)(?=\n# File Partition|\Z)" % name, stock_script(d), re.S).group(1)
    n, pos = (int(x, 16) for x in re.search(r"\$\(SdUpgradeImage\) (0x[0-9a-fA-F]+) (0x[0-9a-fA-F]+)", blk).groups())
    return d[pos:pos + n]


def fast_bootcmd(d):
    """The stock image's own `setenv bootcmd` line, minus the 1 s sleep."""
    lines = [l for l in stock_script(d).split("\n") if l.startswith("setenv bootcmd ")]
    if len(lines) != 1 or lines[0].count(SLEEP) != 1:
        sys.exit("stock bootcmd not as expected (another stock build?)")
    return lines[0].replace(SLEEP, "")


def block(name, pay, pos):
    return ("# File Partition: %s\nmxp r.info %s\nsf probe 0\n"
            "sf erase ${sf_part_start} ${sf_part_size}\n"
            "fatload mmc 0 0x21000000 $(SdUpgradeImage) 0x%X 0x%X\n"
            "sf write 0x21000000 ${sf_part_start} 0x%X\n\n" % (name, name, len(pay), pos, len(pay)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stock", required=True, help="Miyoo's miyoo285_fw.img (build 202510011046)")
    ap.add_argument("--extract", nargs=2, metavar=("PART", "OUT"), help="write the stock image's customer or miservice squashfs to OUT and exit")
    ap.add_argument("--customer", help="squashfs to write to /customer")
    ap.add_argument("--miservice", help="squashfs to write to miservice (/config)")
    ap.add_argument("--build", help="build id, 12 digits (stock is 202510011046)")
    ap.add_argument("--out")
    a = ap.parse_args()
    d = open(a.stock, "rb").read()
    if a.extract:
        if a.extract[0] not in PARTS:
            sys.exit("--extract takes customer or miservice")
        open(a.extract[1], "wb").write(stock_part(d, a.extract[0]))
        return
    if not (a.customer and a.miservice and a.build and a.out):
        sys.exit("--customer, --miservice, --build and --out are required")
    if not re.fullmatch(r"\d{12}", a.build):
        sys.exit("--build must be 12 digits like the stock one")
    script, data, pos = "build:%s\n\n" % a.build, b"", POS
    for name, path in (("customer", a.customer), ("miservice", a.miservice)):
        pay = open(path, "rb").read()
        if pay[:4] != b"hsqs":
            sys.exit("%s payload is not a squashfs" % name)
        if len(pay) > PARTS[name]:
            sys.exit("%s payload %#x is larger than the partition %#x" % (name, len(pay), PARTS[name]))
        pay += b"\xff" * (-len(pay) % 0x1000)
        script += block(name, pay, pos)
        data += pay
        pos += len(pay)
    script += "%s\nsetenv miyoo_version %s\nsaveenv\nreset\n%s" % (fast_bootcmd(d), a.build, END)
    script = script.encode()
    assert len(script) < POS
    open(a.out, "wb").write(script + b"\xff" * (POS - len(script)) + data)
    print("wrote %s: script %d B, payloads %#x B" % (a.out, len(script), len(data)))


main()
