#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
_verify_e2e.py —— 端到端验证：把镜像里 kernel 分区的 lzop 流解回 vmlinux

用法
    python _verify_e2e.py <补丁后镜像.img> [原始镜像.img]
    不给原始镜像时：只做 块数 / 解压长度 / adler32 / DTB magic 检查。
    给了原始镜像：逐字节对比两边的 vmlinux，列出差异，并判断差异是否
    都落在已知补丁点（0x06352AA 主补丁、0x063528B 扩展补丁）。
"""
import sys
import os
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import csi100_patch_image as P   # noqa: E402

KNOWN = {0x06352AA: "bNumFrameDescriptors", 0x063528B: "VC_EXT bNumControls"}


def extract_kernel(img, so):
    hdr = img[so:so + P.HDR_SZ]
    dsize = P.u32(hdr, 0x14)
    zoff = so + P.HDR_SZ
    zlen = P.u32(img, zoff + 0x2C)
    if img[zoff + zlen:zoff + zlen + 4] != b"\xd0\x0d\xfe\xed":
        P.die("zImage 之后不是 DTB（%s @0x%X）" % (hex(zoff), so))
    dec, _ = P.load_lzo()
    blk_off, blocks = P.locate_lzop(img, zoff, zlen)
    if not blocks:
        P.die("LZO 流解析失败 @0x%X" % zoff)
    vml = bytearray()
    for i, (bo, u, c, ad) in enumerate(blocks):
        blk = P.lzo_decompress(dec, bytes(img[bo + 12:bo + 12 + c]), u)
        if (zlib.adler32(blk) & 0xFFFFFFFF) != ad:
            P.die("第 %d 块 adler32 失败" % i)
        vml += blk
    return vml, len(blocks), blk_off - zoff


def main():
    tgt = sys.argv[1] if len(sys.argv) > 1 else None
    orig = sys.argv[2] if len(sys.argv) > 2 else None
    if tgt is None:
        P.die("用法：python _verify_e2e.py <补丁后镜像.img> [原始镜像.img]")

    img = open(tgt, "rb").read()
    parts = P.parse_partitions(img)
    slots = P.find_kernel_slots(img, parts)
    print("待验镜像: %s  (%d 字节)" % (tgt, len(img)))

    ovml = None
    if orig:
        oimg = open(orig, "rb").read()
        oparts = P.parse_partitions(oimg)
        oslots = P.find_kernel_slots(oimg, oparts)
        ovml, onblk, odoff = extract_kernel(oimg, oslots[0][0])
        print("原始镜像: %s  （%d 块，vmlinux %d 字节，块流 +0x%X）"
              % (orig, onblk, len(ovml), odoff))

    allok = True
    for so, nm in slots:
        vml, nblk, doff = extract_kernel(img, so)
        print("\n[%s @0x%08X] %d 块，vmlinux %d 字节，块流相对 zImage +0x%X，adler32 全部通过"
              % (nm, so, nblk, len(vml), doff))
        if ovml is not None:
            d = [i for i in range(min(len(ovml), len(vml))) if ovml[i] != vml[i]]
            known_ok = all(i in KNOWN for i in d)
            allok &= known_ok
            print("  与原始 vmlinux 差异字节数 = %d" % len(d))
            for i in d[:8]:
                print("    0x%07X: 0x%02X -> 0x%02X  %s" % (i, ovml[i], vml[i], KNOWN.get(i, "?")))
            print("  差异是否全部落在已知补丁点: %s" % ("是" if known_ok else "否（有意外差异！）"))
    return 0 if allok else 1


if __name__ == "__main__":
    sys.exit(main())
