# -*- coding: utf-8 -*-
"""生成 54 字节新帧（含 IMU5136 + LINS358）的 hex 样例文件，用于验证 LINS358 解析。"""
import sys


def be16(v):
    """把物理量 ×100 取整，返回大端两字节（自动处理 int16 负数补码）。"""
    x = int(round(v * 100)) & 0xFFFF
    return (x >> 8, x & 0xFF)


def make_frame(f):
    """构造第 f 帧（54 字节）。"""
    b = [0xAA, 0x55]
    # 方位
    b += be16(100.0 + f)          # 方位目标
    b += be16(100.5 + f)          # 方位编码器
    b += be16(1.0 * f)            # 方位速度
    b += be16(0.10 * f)           # 方位前馈
    # 俯仰
    b += be16(50.0 + f)           # 俯仰目标
    b += be16(50.5 + f)           # 俯仰编码器
    b += be16(0.5 * f)            # 俯仰速度
    b += be16(0.05 * f)           # 俯仰前馈
    # 横滚（用 350+ 触发 ±180 环绕：350 -> -10）
    b += be16(350.0 + f)          # 横滚目标
    b += be16(350.5 + f)          # 横滚编码器
    b += be16(0.2 * f)            # 横滚速度
    b += be16(0.02 * f)           # 横滚前馈
    # IMU5136
    b += be16(1.0 * f)            # IMU 角度X
    b += be16(2.0 * f)            # IMU 角度Y
    b += be16(3.0 * f)            # IMU 角度Z
    b += be16(0.1 * f)            # IMU 速度X
    b += be16(0.2 * f)            # IMU 速度Y
    b += be16(0.3 * f)            # IMU 速度Z
    # LINS358（与 IMU 相差固定 0.5/0.05，模拟两个真实惯导）
    b += be16(1.0 * f + 0.5)      # LINS 角度X
    b += be16(2.0 * f + 0.5)      # LINS 角度Y
    b += be16(3.0 * f + 0.5)      # LINS 角度Z
    b += be16(0.1 * f + 0.05)     # LINS 速度X
    b += be16(0.2 * f + 0.05)     # LINS 速度Y
    b += be16(0.3 * f + 0.05)     # LINS 速度Z
    # 信标
    b += be16(-66.0 + f)
    b += [0x66, 0xAA]
    assert len(b) == 54, len(b)
    return b


frames = int(sys.argv[2]) if len(sys.argv) > 2 else 10
all_bytes = []
for f in range(frames):
    all_bytes += make_frame(f)
text = " ".join("%02X" % x for x in all_bytes)
open(sys.argv[1], "w", encoding="ascii").write(text + "\n")
print("frames=%d bytes=%d" % (frames, len(all_bytes)))
