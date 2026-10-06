#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
_hash_recalc.py —— 校验（或重算）CSI100 kernel 分区头的 bootloader 哈希

用法
    python _hash_recalc.py <镜像.img>            # 校验 kernel 与 kernel_1 两个槽
    python _hash_recalc.py <镜像.img> 0x80000    # 只校验指定槽

公式（bootloader 的 Code check）
    hash = SHA256( data ‖ hdr[0x10:0x18] ‖ hdr[0x1C:0x20] )   →  hdr[0x20 : 0x20+hash_len]
    data = 分区起始 + 0x800，长度 = hdr[0x14]（zImage ‖ DTB）

本脚本只读不改；重算请用 csi100_patch_image.py。
"""
import sys
import os
import hashlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import csi100_patch_image as P   # noqa: E402


def check_slot(img, off, label):
    hdr = img[off:off + P.HDR_SZ]
    size = P.u32(hdr, 0x14)
    hlen = P.u32(hdr, 0x1C)
    want = hdr[0x20:0x20 + hlen]
    got = P.kernel_hash(hdr, img[off + P.HDR_SZ: off + P.HDR_SZ + size])
    ok = want == got
    print("  %-9s @0x%08X  name=%-6r  load=0x%08X  size=0x%X  hash_len=%d  ->  %s"
          % (label, off, bytes(hdr[:16]).split(b"\0")[0].decode("latin1"),
             P.u32(hdr, 0x10), size, hlen, "HASH-OK" if ok else "HASH-MISMATCH"))
    if not ok:
        print("      stored = %s" % want.hex())
        print("      calc   = %s" % got.hex())
    return ok


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else None
    if src is None:
        cands = P.auto_find_image()
        if len(cands) == 1:
            src = cands[0]
        else:
            P.die("用法：python _hash_recalc.py <镜像.img> [槽偏移...]")
    img = open(src, "rb").read()
    print("镜像: %s  (%d 字节, md5=%s)" % (src, len(img), hashlib.md5(img).hexdigest()))
    parts = P.parse_partitions(img)
    if len(sys.argv) > 2:
        slots = [(int(x, 0), "user@" + x) for x in sys.argv[2:]]
    else:
        slots = [(o, n) for o, n in P.find_kernel_slots(img, parts)]
    allok = True
    for off, label in slots:
        allok &= check_slot(img, off, label)
    print("结论:", "全部通过" if allok else "存在不通过项")
    return 0 if allok else 1


if __name__ == "__main__":
    sys.exit(main())
