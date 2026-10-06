#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sdk_probe.py —— 用官方 Windows SDK 验收相机

修复固件并刷入后（设备管理器不再有 Code 10），运行本脚本应看到
    Ps2_GetDeviceCount -> status 0, count = 1
    并列出设备信息（type=501 / uri / 固件版本）

用法
    python sdk_probe.py            （64 位 Python；相机通电连接）

依赖
    本脚本必须放在 03_SDK与工具/ 目录内（它按相对路径找 VzenseSDK_Windows）。
    签名依据：VzenseSDK_Windows/Include/DCAM710/Vzense_api_710.h
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DLL = os.path.join(HERE, "VzenseSDK_Windows", "Bin", "x64", "vzense_api.dll")


class PsDeviceInfo(ctypes.Structure):
    _fields_ = [
        ("SessionCount", ctypes.c_int),
        ("devicetype", ctypes.c_int),
        ("uri", ctypes.c_char * 256),
        ("fw", ctypes.c_char * 50),
        ("alias", ctypes.c_char * 64),
        ("status", ctypes.c_int),
        ("ip", ctypes.c_char * 16),
    ]


def main():
    if not os.path.exists(DLL):
        print("[错误] 找不到 %s\n  本脚本要放在 03_SDK与工具/ 目录里。" % DLL)
        return 1
    if sys.maxsize <= 2 ** 32:
        print("[错误] 需要 64 位 Python（当前是 32 位）。")
        return 1

    try:
        api = ctypes.CDLL(DLL)
    except OSError as e:
        print("[错误] 加载 vzense_api.dll 失败：%s\n  需要 64 位 Python。" % e)
        return 1

    try:
        ver = ctypes.create_string_buffer(64)
        if api.Ps2_GetSDKVersion(ver, 64) == 0:
            print("SDK 版本: %s" % ver.value.decode("utf-8", "replace"))
    except AttributeError:
        pass

    n = ctypes.c_uint32(0)
    api.Ps2_GetDeviceCount.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    api.Ps2_GetDeviceCount.restype = ctypes.c_int
    st = api.Ps2_GetDeviceCount(ctypes.byref(n))
    print("Ps2_GetDeviceCount -> status %d, count = %d" % (st, n.value))

    if n.value > 0:
        try:
            lst = (PsDeviceInfo * n.value)()
            api.Ps2_GetDeviceListInfo.argtypes = [ctypes.POINTER(PsDeviceInfo), ctypes.c_uint32]
            api.Ps2_GetDeviceListInfo.restype = ctypes.c_int
            api.Ps2_GetDeviceListInfo(lst, n.value)
            for i in range(n.value):
                d = lst[i]
                print("  [%d] devicetype=%d uri=%s fw=%s status=%d"
                      % (i, d.devicetype, d.uri.decode("latin1", "replace"),
                         d.fw.decode("latin1", "replace"), d.status))
        except Exception as e:
            print("  （列出设备信息失败：%s）" % e)
        print("设备可被官方 SDK 枚举 OK —— Windows 侧验收通过（FrameViewer 也应能出图）")
        return 0

    print("count=0：相机未接 / 未通电（12V 必须插）/ 仍是 Code 10（先跑 02_一键修补 并整片刷入）")
    return 1


if __name__ == "__main__":
    sys.exit(main())
