#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
csi100_patch_image.py —— CSI100 (Vzense / Rockchip RV1108) 固件一键修补

用途
    Windows 上 usbvideo.sys 因相机 UVC 描述符不合规而拒绝启动（设备管理器 Code 10），
    导致官方 SDK 看不到相机。根因是 VS_FORMAT_UNCOMPRESSED 描述符里
    bNumFrameDescriptors 声明 2 而实际只发 1 个帧描述符。
    本脚本在**你自己 dump 出来的整片镜像**上做 1 字节修补 + 重算 bootloader 校验哈希，
    产出可直接整片烧写的镜像。

⚠️ 本脚本**不包含任何厂商固件**。输入必须是你从自己设备上导出的镜像。

用法
    python csi100_patch_image.py                     # 自动在本目录 / 包根目录找 .img
    python csi100_patch_image.py 原镜像.img 输出.img
    python csi100_patch_image.py --ext               # 附带"扩展补丁"（一般不需要）
    也可以把镜像文件直接拖到 一键修补镜像.bat 上

原理（三件事）
    1) 补丁：vmlinux 0x06352AA  0x02 -> 0x01
       （VS_FORMAT_UNCOMPRESSED.bNumFrameDescriptors 2 -> 1，让描述符自洽）
    2) 重压：vmlinux 以 LZO1X-999 分块压缩后嵌在 zImage 里。改动所在的那一块必须用
       **LZO1X-999 level 9** 重压，才能得到与原块**完全相同的压缩长度**
       （level 8 会多 113 字节 → zImage 变长 → 压掉后面的 DTB → 内核找不到设备树）。
       zImage 末尾还有 53 字节元数据（偏移表），必须原样接回。
    3) 哈希：kernel 分区头（0x800 字节）里有 bootloader 校验用的哈希
           hash = SHA256( data ‖ hdr[0x10:0x18] ‖ hdr[0x1C:0x20] )
           data = 分区起始 + 0x800，长度 = hdr[0x14]（即 zImage ‖ DTB）
       **kernel 和 kernel_1 两个槽都要改、都要分别重算**
       （loader 字符串里有 "Not found PART_KERNEL1, switch to PART_KERNEL"，它优先用备份槽）。

依赖
    _fw_artifacts/lzodll.dll   —— 解压（lzo1x_decompress_safe）
    _fw_artifacts/lzo999.dll   —— 压缩（lzo1x_999_compress_level）
    两个 DLL 由开源 LZO 2.10 库编译而来，源码见 _fw_artifacts/wrap999.c（GPL）。
    需要 **64 位 Python 3**（DLL 是 x86-64）。
"""
import ctypes
import hashlib
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------- 常量

RKFP_TBL_OFF = 0x200          # RKFP 分区表起始
RKFP_ENT_SZ = 0x80            # 每项大小
HDR_SZ = 0x800                # 分区头大小
VMLINUX_PATCH = 0x06352AA     # vmlinux 内偏移
PATCH_OLD, PATCH_NEW = 0x02, 0x01
VMLINUX_PATCH_EXT = 0x063528B  # 扩展补丁（VC_EXTENSION_UNIT.bNumControls 40 -> 32）
EXT_OLD, EXT_NEW = 0x28, 0x20
LZO_LEVEL = 9                 # ★ 必须 9
LZO_TAIL = 53                 # zImage 尾部元数据长度（本平台实测固定 53 字节）

HERE = os.path.dirname(os.path.abspath(__file__))
DLL_DEC = os.path.join(HERE, "_fw_artifacts", "lzodll.dll")
DLL_999 = os.path.join(HERE, "_fw_artifacts", "lzo999.dll")


def _fix_encoding():
    """stdout 被重定向（管道/文件）时 cp936 等旧编码会炸（⚠ 这类字符无法编码）。
    这里只把 errors 改成 replace：交互终端保持本地编码不乱码，重定向时不再抛异常。"""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except Exception:
            pass


_fix_encoding()


def die(msg, code=1):
    print("\n[错误] " + msg, file=sys.stderr)
    sys.exit(code)


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


# ---------------------------------------------------------------- LZO

def load_lzo():
    for p in (DLL_DEC, DLL_999):
        if not os.path.exists(p):
            die("缺少 %s\n  这两个 DLL 是重打包必需的（源码见 _fw_artifacts/wrap999.c）" % p)
    if struct.calcsize("P") != 8:
        die("当前 Python 是 32 位（%d 位）。本脚本依赖的 LZO DLL 是 x86-64，"
            "请改用 64 位 Python。\n  安装：https://www.python.org/ 勾选 64-bit" % (struct.calcsize("P") * 8))
    try:
        dec = ctypes.CDLL(DLL_DEC)
        enc = ctypes.CDLL(DLL_999)
    except OSError as e:
        die("加载 LZO DLL 失败：%s\n  DLL 是 x86-64，需要 64 位 Python。" % e)
    dec.lzo_init_w.restype = ctypes.c_int
    dec.lzo_dec.restype = ctypes.c_int
    dec.lzo_dec.argtypes = [ctypes.c_char_p, ctypes.c_uint,
                            ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint)]
    if dec.lzo_init_w() != 0:
        die("lzo_init 失败")
    enc.lzo_init_w.restype = ctypes.c_int
    if enc.lzo_init_w() != 0:
        die("lzo_init(999) 失败")
    enc.lzo_enc999_level.restype = ctypes.c_int
    enc.lzo_enc999_level.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_char_p,
                                     ctypes.POINTER(ctypes.c_uint), ctypes.c_int]
    return dec, enc


def lzo_decompress(dec, data, ulen):
    dst = ctypes.create_string_buffer(ulen)
    n = ctypes.c_uint(ulen)
    if dec.lzo_dec(data, len(data), dst, ctypes.byref(n)) != 0:
        die("LZO 解压失败")
    return dst.raw[:n.value]


def lzo_compress999(enc, seg, level=LZO_LEVEL):
    cap = len(seg) + len(seg) // 16 + 64 + 3 + 8192
    buf = ctypes.create_string_buffer(cap)
    n = ctypes.c_uint(cap)
    if enc.lzo_enc999_level(seg, len(seg), buf, ctypes.byref(n), level) != 0:
        die("LZO1X-999 压缩失败")
    return buf.raw[:n.value]


# ---------------------------------------------------------------- 镜像结构

def parse_partitions(img):
    """从 RKFP 分区表里取 name -> (字节偏移, 字节大小)。"""
    if img[:4] != b"RKFP":
        die("这不是 Rockchip 整片镜像（开头不是 'RKFP'，而是 %r）。\n"
            "  请用 RKDevTool 的『导出镜像』从你的设备整片 dump 一份。" % bytes(img[:4]))
    parts = {}
    for o in range(RKFP_TBL_OFF, RKFP_TBL_OFF + RKFP_ENT_SZ * 16, RKFP_ENT_SZ):
        if o + 0x40 > len(img):
            break
        nm = bytes(img[o:o + 16]).split(b"\0")[0]
        if not nm or not nm.isascii() or len(nm) < 2:
            continue
        f = struct.unpack_from("<8I", img, o + 0x20)
        parts[nm.decode()] = (f[1] * 512, f[2] * 512)
    return parts


def find_kernel_slots(img, parts):
    """找出所有名为 kernel / kernel_1 的分区（按名字，不写死地址）。"""
    slots = []
    for nm, (off, size) in sorted(parts.items(), key=lambda kv: kv[1][0]):
        if nm.lower() in ("kernel", "kernel_1"):
            if img[off:off + 6] != b"KERNEL":
                die("分区 %s @0x%X 的头部不是 'KERNEL'（读到 %r）"
                    % (nm, off, bytes(img[off:off + 8])))
            slots.append((off, nm))
    if not slots:
        die("在分区表里找不到 kernel / kernel_1 分区")
    return slots


def kernel_hash(hdr, data):
    """bootloader 的 Code check 公式。"""
    hlen = u32(hdr, 0x1C)
    if hlen not in (0x20, 0x14):
        die("分区头的 hash_len=%d 不是 32/20，镜像可能不是 CSI100 的" % hlen)
    fn = hashlib.sha256 if hlen == 0x20 else hashlib.sha1
    return fn(bytes(data) + bytes(hdr[0x10:0x18]) + bytes(hdr[0x1C:0x20])).digest()


def locate_lzop(img, zoff, zlen):
    """在 zImage 里自动定位 LZO 块流：扫所有候选起点，取能一路解析到
    'zImage 末尾 - 53 字节尾部元数据' 的那一个。返回 (块流起点, blocks)。"""
    stream_end = zoff + zlen - LZO_TAIL
    for cand in range(zoff, zoff + 0x8000, 2):
        bl, o = [], cand
        while o + 12 <= stream_end:
            u, c = struct.unpack_from(">II", img, o)
            if u == 0 or u > (1 << 25) or c > (1 << 25):
                break
            bl.append((o, u, c, struct.unpack_from(">I", img, o + 8)[0]))
            o += 12 + c
        if len(bl) >= 2 and o == stream_end:
            return cand, bl
    return None, None


def auto_find_image():
    """自动找镜像：本脚本目录 → 包根目录 → 当前目录，取唯一的 *.img。"""
    cands = []
    for d in (HERE, os.path.dirname(HERE), os.getcwd()):
        try:
            names = sorted(os.listdir(d))
        except OSError:
            continue
        for f in names:
            if f.lower().endswith(".img") and "_patched" not in f.lower():
                p = os.path.join(d, f)
                if os.path.isfile(p) and p not in cands:
                    cands.append(p)
    return cands


def count_diff(src_path, img, chunk=1 << 20):
    """分块比较输出镜像与原文件，返回不同字节数（几乎全等时接近 C 速度）。"""
    n = 0
    i = 0
    with open(src_path, "rb") as f:
        while True:
            x = f.read(chunk)
            if not x:
                break
            y = bytes(img[i:i + len(x)])
            if x != y:
                n += sum(1 for p, q in zip(x, y) if p != q)
            i += len(x)
    return n


# ---------------------------------------------------------------- 主流程

def main():
    args = [a for a in sys.argv[1:]]
    use_ext = "--ext" in args
    pos = [a for a in args if not a.startswith("--")]

    src = pos[0] if len(pos) > 0 else None
    dst = pos[1] if len(pos) > 1 else None

    if src is None:
        cands = auto_find_image()
        if len(cands) == 1:
            src = cands[0]
        elif not cands:
            die("没找到 .img 镜像。\n"
                "  请把从你自己设备导出的整片镜像放到本目录（02_一键修补\\）或包根目录，\n"
                "  或直接指定：python csi100_patch_image.py <镜像.img>")
        else:
            die("找到多个 .img，无法确定用哪个。请明确指定：\n"
                "  python csi100_patch_image.py <镜像.img>\n  候选：\n  %s"
                % "\n  ".join(cands))
    if not os.path.exists(src):
        die("找不到输入镜像 %s\n  用法: python csi100_patch_image.py <原始镜像.img> [输出.img]" % src)

    if dst is None:
        d = os.path.dirname(src) or "."
        dst = os.path.join(d, "CSI100_patched%s.img" % ("_ext" if use_ext else ""))

    print("=" * 74)
    print("CSI100 固件一键修补")
    print("=" * 74)
    print("输入 : %s" % src)
    print("输出 : %s" % dst)
    print("模式 : %s" % ("补丁 + 扩展补丁" if use_ext else "仅主补丁（推荐）"))

    img = bytearray(open(src, "rb").read())
    print("\n镜像 %d 字节  md5=%s" % (len(img), hashlib.md5(img).hexdigest()))

    # --- 1. 分区表 ---
    parts = parse_partitions(img)
    print("\n[1/6] RKFP 分区表：")
    for nm, (off, size) in sorted(parts.items(), key=lambda kv: kv[1][0]):
        print("      %-10s @0x%08X  size=0x%X" % (nm, off, size))

    slots = find_kernel_slots(img, parts)
    print("      kernel 槽：%s" % ", ".join("%s@0x%X" % (n, o) for o, n in slots))

    # --- 2. 读 kernel 分区头，定位 zImage / LZO 流 ---
    koff, kname = slots[0]
    hdr = bytearray(img[koff:koff + HDR_SZ])
    load_addr = u32(hdr, 0x10)
    dsize = u32(hdr, 0x14)
    hlen = u32(hdr, 0x1C)
    zoff = koff + HDR_SZ
    if u32(img, zoff + 0x24) != 0x016F2818:
        die("zImage magic 不对（期望 0x016f2818，读到 0x%08X）" % u32(img, zoff + 0x24))
    zlen = u32(img, zoff + 0x2C)          # zImage 总长（含 53 字节尾部元数据）
    dtb_len = dsize - zlen
    print("\n[2/6] kernel 分区头：")
    print("      load_addr=0x%08X  dsize=0x%X  hash_len=%d" % (load_addr, dsize, hlen))
    print("      zImage@0x%X  len=0x%X  后面跟 DTB %d 字节" % (zoff, zlen, dtb_len))
    if img[zoff + zlen:zoff + zlen + 4] != b"\xd0\x0d\xfe\xed":
        die("zImage 之后不是 DTB（magic 应为 d00dfeed）—— 镜像结构异常")
    print("      DTB magic OK")

    # --- 3. 定位 LZO 块流并解压 ---
    dec, enc = load_lzo()
    magic = img.find(b"\x89LZO", zoff, zoff + 0x8000)
    blk_off, blocks = locate_lzop(img, zoff, zlen)
    if not blocks:
        die("LZO 块流解析失败（在 zImage 头部 32KB 内没找到能解析到末尾的起点）")
    print("      lzop 魔数@0x%X，块流@0x%X（自动定位，相对 zImage +0x%X）"
          % (magic, blk_off, blk_off - zoff))

    vml = bytearray()
    for i, (bo, u, c, ad) in enumerate(blocks):
        blk = lzo_decompress(dec, bytes(img[bo + 12:bo + 12 + c]), u)
        if (zlib.adler32(blk) & 0xFFFFFFFF) != ad:
            die("第 %d 块 adler32 校验失败" % i)
        vml += blk
    print("\n[3/6] LZO 流：%d 块，解出 vmlinux %d 字节，全部 adler32 通过" % (len(blocks), len(vml)))

    # --- 4. 打补丁（按块边界累加定位，不假设块大小一致）---
    patches = [(VMLINUX_PATCH, PATCH_OLD, PATCH_NEW, "bNumFrameDescriptors")]
    if use_ext:
        patches.append((VMLINUX_PATCH_EXT, EXT_OLD, EXT_NEW, "VC_EXT bNumControls"))

    pos = 0
    bidx = None
    for i, (bo, u, c, ad) in enumerate(blocks):
        if pos <= VMLINUX_PATCH < pos + u:
            bidx = i
            break
        pos += u
    if bidx is None:
        die("补丁点 0x%07X 不在任何 LZO 块内" % VMLINUX_PATCH)

    print("\n[4/6] 打补丁：")
    blk_start = sum(b[1] for b in blocks[:bidx])
    blk_end = blk_start + blocks[bidx][1]
    for at, old, new, name in patches:
        # 用块边界精确判断：at 必须落在 bidx 块覆盖的 vmlinux 区间内
        if not (blk_start <= at < blk_end):
            die("补丁点 0x%07X 不在第 %d 块（vmlinux 0x%X-0x%X）内，本脚本不支持跨块"
                % (at, bidx, blk_start, blk_end))
        if vml[at] != old:
            die("%s：期望原值 0x%02X，实际 0x%02X —— 镜像可能已打过补丁或不是该版本"
                % (name, old, vml[at]))
        vml[at] = new
        print("      %-22s vmlinux 0x%07X: 0x%02X -> 0x%02X" % (name, at, old, new))

    # --- 5. 重压那一块 + 回读自检 + 重建 zImage ---
    bo, u, c, ad = blocks[bidx]
    blk_start = sum(b[1] for b in blocks[:bidx])
    seg = bytes(vml[blk_start: blk_start + u])
    new_blk = lzo_compress999(enc, seg)
    print("\n[5/6] 重压第 %d 块：原 c=%d，level%d 后 c=%d  %s"
          % (bidx, c, LZO_LEVEL, len(new_blk), "等长 OK" if len(new_blk) == c else "不等长！"))
    if len(new_blk) != c:
        die("重压后长度不一致 —— 不能用（这会撞坏后面的 DTB）。\n"
            "  zImage 变长会压掉 DTB、变短会让 DTB 位置对不上，都不能烧。")
    # 回读自检：新压缩块必须能解回和打补丁后完全一样的 256KB
    rt = lzo_decompress(dec, new_blk, u)
    if rt != seg:
        die("重压回读自检失败：压缩/解压不一致（LZO 环境异常）")

    stream = bytearray()
    for i, (b_o, b_u, b_c, b_ad) in enumerate(blocks):
        if i == bidx:
            stream += struct.pack(">III", b_u, len(new_blk),
                                  zlib.adler32(seg) & 0xFFFFFFFF) + new_blk
        else:
            stream += struct.pack(">III", b_u, b_c, b_ad) + bytes(img[b_o + 12:b_o + 12 + b_c])
    lzop_end = blocks[-1][0] + 12 + blocks[-1][2]
    tail = bytes(img[lzop_end:zoff + zlen])           # ★ 53 字节元数据，原样保留
    new_zimg = bytearray(bytes(img[zoff:blk_off])) + stream + tail
    struct.pack_into("<I", new_zimg, 0x2C, len(new_zimg))
    if len(new_zimg) != zlen:
        die("重建后的 zImage 长度 %d != 原来 %d" % (len(new_zimg), zlen))
    print("      zImage 重建完成，长度不变 = 0x%X（尾部元数据 %d 字节已保留）"
          % (len(new_zimg), len(tail)))

    blob = bytes(new_zimg) + bytes(img[zoff + zlen: zoff + zlen + dtb_len])

    # --- 6. 写两个槽 + 重算哈希 ---
    print("\n[6/6] 写入各槽并重算 bootloader 哈希：")
    for so, name in slots:
        h = bytearray(img[so:so + HDR_SZ])
        if u32(h, 0x14) != dsize:
            die("%s 的数据长度(%d)与 %s 不一致" % (name, u32(h, 0x14), kname))
        img[so + HDR_SZ: so + HDR_SZ + dsize] = blob
        dg = kernel_hash(h, blob)
        h[0x20:0x20 + len(dg)] = dg
        img[so:so + HDR_SZ] = h
        print("      %-9s @0x%08X  哈希 = %s" % (name, so, dg.hex()))

    open(dst, "wb").write(bytes(img))
    print("\n已写出 %s" % dst)
    print("  %d 字节  md5=%s" % (len(img), hashlib.md5(img).hexdigest()))

    # --- 复检（结构性断言，不做"期望差异数"这种硬编码）---
    chk = open(dst, "rb").read()
    print("\n复检：")
    ok = True
    for so, name in slots:
        h = chk[so:so + HDR_SZ]
        s = u32(h, 0x14)
        good = h[0x20:0x20 + u32(h, 0x1C)] == kernel_hash(h, chk[so + HDR_SZ:so + HDR_SZ + s])
        ok &= good
        print("  哈希 %-9s %s" % (name, "OK" if good else "FAIL"))
        # 两个槽的数据区应逐字节相同
        same = chk[slots[0][0] + HDR_SZ: slots[0][0] + HDR_SZ + s] == \
               chk[so + HDR_SZ: so + HDR_SZ + s]
        if not same:
            die("%s 与 %s 的数据区不一致" % (slots[0][1], name))
    diff = count_diff(src, img)
    print("  与原始镜像差异 %d 字节（只报告，不设期望值；应集中在 zImage 一块 + 两个哈希）"
          % diff)
    print("  端到端解压自检（可选）：python _verify_e2e.py %s %s" % (dst, src))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
