#!/usr/bin/env python3
"""Build a customer-only update image for the Miyoo Mini Flip (plorpos-f1p.38).

The stock miyoo285_fw.img is a U-Boot script followed by raw partition
payloads (script ends at "% <- this is end of script symbol", padding 0xFF to
0x4000, first payload at 0x4000). This writes the same kind of file that
touches ONLY the `customer` partition (mtd6, squashfs): U-Boot, kernel and
rootfs are not written, so the stock SD update stays the way back.

  mkfw.py --customer customer.squashfs --build 202511090001 --out miyoo285_fw.img
  mkfw.py --stock ~/Downloads/miyoo285_fw.img --build 202511090001 --out t.img   # test: the stock /customer bytes

The customer partition is 0x770000 bytes; a bigger payload is refused.
"""
import argparse, re, sys

POS = 0x4000
PART = 0x770000
END = "% <- this is end of script symbol\n"


def stock_customer(path):
    d = open(path, "rb").read()
    end = 0
    while end < len(d) and 9 <= d[end] < 128:
        end += 1
    t = d[:end].decode()
    blk = re.search(r"# File Partition: customer\n(.*?)(?=\n# File Partition|\Z)", t, re.S).group(1)
    n, pos = (int(x, 16) for x in re.search(r"\$\(SdUpgradeImage\) (0x[0-9a-fA-F]+) (0x[0-9a-fA-F]+)", blk).groups())
    return d[pos:pos + n]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--extract-customer", metavar="OUT", help="write the stock image's /customer squashfs to OUT and exit (needs --stock)")
    ap.add_argument("--customer", help="squashfs to write to /customer")
    ap.add_argument("--stock", help="take the /customer payload from this stock image instead")
    ap.add_argument("--build", help="build id, 12 digits (stock is 202510011046)")
    ap.add_argument("--out")
    a = ap.parse_args()
    if a.extract_customer:
        if not a.stock:
            sys.exit("--extract-customer needs --stock")
        open(a.extract_customer, "wb").write(stock_customer(a.stock))
        return
    if not a.build or not a.out:
        sys.exit("--build and --out are required")
    if bool(a.customer) == bool(a.stock):
        sys.exit("give exactly one of --customer / --stock")
    if not re.fullmatch(r"\d{12}", a.build):
        sys.exit("--build must be 12 digits like the stock one")
    pay = open(a.customer, "rb").read() if a.customer else stock_customer(a.stock)
    if pay[:4] != b"hsqs":
        sys.exit("payload is not a squashfs")
    if len(pay) > PART:
        sys.exit("payload %#x is larger than the partition %#x" % (len(pay), PART))
    pay += b"\xff" * (-len(pay) % 0x1000)
    script = (
        "build:%s\n\n# File Partition: customer\nmxp r.info customer\nsf probe 0\n"
        "sf erase ${sf_part_start} ${sf_part_size}\n"
        "fatload mmc 0 0x21000000 $(SdUpgradeImage) 0x%X 0x%X\n"
        "sf write 0x21000000 ${sf_part_start} 0x%X\n\n"
        "setenv miyoo_version %s\nsaveenv\nreset\n%s" % (a.build, len(pay), POS, len(pay), a.build, END)
    ).encode()
    assert len(script) < POS
    open(a.out, "wb").write(script + b"\xff" * (POS - len(script)) + pay)
    print("wrote %s: script %d B, customer payload %#x B" % (a.out, len(script), len(pay)))


main()
