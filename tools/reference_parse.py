# -*- coding: utf-8 -*-
"""参考实现（Python）：解析星网转台串口 hex 文本，用于与 Qt 软件解析结果交叉验证。

支持两种帧长（自动识别）：
  新帧 54 字节：含 IMU5136 与 LINS358 两套惯导；
  旧帧 42 字节：仅含 IMU5136（LINS358 字段补 0）。

用法:
    python tools/reference_parse.py <输入txt> [输出csv] [--source imu|lins|both]

修正来源（与 C++ 默认行为一致）：
  imu  = 仅 IMU5136（默认，旧版行为）
  lins = 仅 LINS358；旧帧无 LINS358 时自动回退为 IMU5136
  both = IMU5136 + LINS358 叠加

字段顺序（共 25 个）：
   0-11  方位/俯仰/横滚 的 目标/编码器/速度/前馈
  12-17  IMU5136 角度 X/Y/Z、速度 X/Y/Z
  18-23  LINS358 角度 X/Y/Z、速度 X/Y/Z
  24     信标功率值
"""

import sys
import time
import math

HEADER = (0xAA, 0x55)
TAIL = (0x66, 0xAA)
NEW_LEN = 54   # 新帧长（含 LINS358）
OLD_LEN = 42   # 旧帧长（仅 IMU5136）
MIN_LEN = min(NEW_LEN, OLD_LEN)  # 扫描循环下限（与 C++ 一致）

# (变量名, 中文名, 无符号?, 惯导修正源: 'A'角度 / 'R'速度 / None, 新帧偏移, 旧帧偏移)
# 旧帧偏移 -1 表示该字段在旧帧中不存在（解析时补 0）。
FIELDS = [
    ("AZ_TARGET",   "方位目标角度",   True,  "A", 2,  2),
    ("AZ_ENCODER",  "方位编码器角度", True,  None, 4,  4),
    ("AZ_SPEED",    "方位运行速度",   False, None, 6,  6),
    ("AZ_FF",       "方位前馈速度",   False, "R",  8,  8),
    ("EL_TARGET",   "俯仰目标角度",   True,  "A", 10, 10),
    ("EL_ENCODER",  "俯仰编码器角度", True,  None, 12, 12),
    ("EL_SPEED",    "俯仰运行速度",   False, None, 14, 14),
    ("EL_FF",       "俯仰前馈速度",   False, "R",  16, 16),
    ("RL_TARGET",   "横滚目标角度",   True,  "A", 18, 18),
    ("RL_ENCODER",  "横滚编码器角度", True,  None, 20, 20),
    ("RL_SPEED",    "横滚运行速度",   False, None, 22, 22),
    ("RL_FF",       "横滚前馈速度",   False, "R",  24, 24),
    ("GYRO_ANG_X",  "IMU5136角度X",   False, None, 26, 26),
    ("GYRO_ANG_Y",  "IMU5136角度Y",   False, None, 28, 28),
    ("GYRO_ANG_Z",  "IMU5136角度Z",   False, None, 30, 30),
    ("GYRO_RATE_X", "IMU5136速度X",   False, None, 32, 32),
    ("GYRO_RATE_Y", "IMU5136速度Y",   False, None, 34, 34),
    ("GYRO_RATE_Z", "IMU5136速度Z",   False, None, 36, 36),
    ("LINS_ANG_X",  "LINS358角度X",   False, None, 38, -1),
    ("LINS_ANG_Y",  "LINS358角度Y",   False, None, 40, -1),
    ("LINS_ANG_Z",  "LINS358角度Z",   False, None, 42, -1),
    ("LINS_RATE_X", "LINS358速度X",   False, None, 44, -1),
    ("LINS_RATE_Y", "LINS358速度Y",   False, None, 46, -1),
    ("LINS_RATE_Z", "LINS358速度Z",   False, None, 48, -1),
    ("BEACON_PWR",  "信标功率值",     False, None, 50, 38),
]

# 各字段单位（仅用于 CSV 表头，与 C++ DataManager::csvHeader 保持一致）
UNITS = ["°", "°", "°/s", "°/s"] * 3 + ["°", "°", "°", "°/s", "°/s", "°/s"] * 2 + ["dBm"]

# 需要把角度环绕到 ±180 的字段索引（横滚目标角度、横滚编码器角度）
WRAP_SIGNED180 = {8, 9}


def wrap_signed180(v):
    """把角度归一化到 (-180, 180]：超过 180 减 360、显示为负（如 350° -> -10°）。"""
    v = math.fmod(v, 360.0)
    if v > 180.0:
        v -= 360.0
    elif v <= -180.0:
        v += 360.0
    return v


def read_bytes(path):
    """读取 hex 文本，过滤空白，按两个 hex 字符拼装字节，返回字节列表。"""
    data = open(path, "rb").read()
    # utf-8-sig 会跳过可选的 UTF-8 BOM（Windows/Excel 导出的文本常见）
    text = data.decode("utf-8-sig", errors="replace")
    out = []
    hi = -1
    pos = 0
    for ch in text:
        if ch.isspace():
            pos += 1
            continue
        if ch not in "0123456789abcdefABCDEF":
            raise ValueError("非法 hex 字符 '%s' @ 偏移 %d" % (ch, pos))
        if hi < 0:
            hi = int(ch, 16)
        else:
            out.append((hi << 4) | int(ch, 16))
            hi = -1
        pos += 1
    if hi >= 0:
        raise ValueError("末尾存在半个字节的 hex 字符（奇数个 nibble）")
    return out


def frame_fits(b, p, length, n):
    """强匹配：帧尾正确，且（帧到文件末尾，或紧接下一帧帧头）。"""
    if p + length > n:
        return False
    if b[p + length - 2] != TAIL[0] or b[p + length - 1] != TAIL[1]:
        return False
    if p + length == n:
        return True
    return p + length + 1 < n and b[p + length] == HEADER[0] and b[p + length + 1] == HEADER[1]


def tail_fits(b, p, length, n):
    """弱匹配：仅帧尾正确。"""
    return p + length <= n and b[p + length - 2] == TAIL[0] and b[p + length - 1] == TAIL[1]


def locate_frames(b):
    """扫描并定位所有帧，返回 (帧起始列表, 对应帧长列表, 错误帧计数)。"""
    n = len(b)
    starts = []
    lengths = []
    errors = 0
    i = 0
    while i + MIN_LEN <= n:
        accepted = False
        if b[i] == HEADER[0] and b[i + 1] == HEADER[1]:
            # 强匹配优先（新帧优先），弱匹配兜底
            if frame_fits(b, i, NEW_LEN, n):
                starts.append(i)
                lengths.append(NEW_LEN)
                i += NEW_LEN
                accepted = True
            elif frame_fits(b, i, OLD_LEN, n):
                starts.append(i)
                lengths.append(OLD_LEN)
                i += OLD_LEN
                accepted = True
            elif tail_fits(b, i, NEW_LEN, n):
                starts.append(i)
                lengths.append(NEW_LEN)
                i += NEW_LEN
                accepted = True
            elif tail_fits(b, i, OLD_LEN, n):
                starts.append(i)
                lengths.append(OLD_LEN)
                i += OLD_LEN
                accepted = True
            else:
                errors += 1
        if not accepted:
            i += 1
    return starts, lengths, errors


def parse(path, correct=True, source="imu"):
    """解析文件。

    source: "imu"（默认）/ "lins" / "both"。
    "lins" 在旧帧（无 LINS358）上自动回退为 IMU5136，与 C++ FrameParser 一致。
    """
    if source not in ("imu", "lins", "both"):
        raise ValueError("source must be one of: imu, lins, both")

    b = read_bytes(path)
    n = len(b)
    starts, lengths, errors = locate_frames(b)
    # 残帧口径：最后一个已接受帧之后的全部字节（无有效帧时为全部字节）
    residual = n - (starts[-1] + lengths[-1]) if starts else n

    rows = []
    for idx, (off, frame_len) in enumerate(zip(starts, lengths)):
        is_legacy = (frame_len == OLD_LEN)
        vals = []
        for k, (_, _, unsigned, _, new_off, old_off) in enumerate(FIELDS):
            foff = old_off if is_legacy else new_off
            if foff < 0:
                vals.append(0.0)  # 旧帧无 LINS358
                continue
            p = off + foff
            raw = (b[p] << 8) | b[p + 1]
            if not unsigned and raw >= 0x8000:
                raw -= 0x10000
            v = raw / 100.0
            if k in WRAP_SIGNED180:
                v = wrap_signed180(v)
            vals.append(v)
        if correct:
            # 前 12 个机械轴量参与修正：
            #   IMU5136 角度 12-14 / 速度 15-17；LINS358 角度 18-20 / 速度 21-23
            for k, (_, _, _, src, _, _) in enumerate(FIELDS):
                if k >= 12 or src is None:
                    continue
                axis = k // 4  # 0=方位, 1=俯仰, 2=横滚
                if src == "A":
                    imu = vals[12 + axis]
                    lins = vals[18 + axis]
                else:
                    imu = vals[15 + axis]
                    lins = vals[21 + axis]
                if source == "imu":
                    g = imu
                elif source == "lins":
                    g = lins if not is_legacy else imu  # 旧帧回退 IMU
                else:  # both
                    g = imu + (lins if not is_legacy else 0.0)
                vals[k] += g
                if k in WRAP_SIGNED180:
                    vals[k] = wrap_signed180(vals[k])
        rows.append((idx, off, vals))
    return rows, dict(bytes=n, frames=len(starts), errors=errors, residual=residual)


def csv_header():
    """与 C++ DataManager::csvHeader() 一致：25 项原始值 + 6 项修正值。"""
    raw_cols = []
    for (_, name, _, _, _, _), unit in zip(FIELDS, UNITS):
        raw_cols.append("%s(%s)" % (name, unit) if unit else name)
    corrected_cols = []
    for (_, name, _, src, _, _), unit in zip(FIELDS, UNITS):
        if src is None:
            continue
        corrected_cols.append("%s(修正)(%s)" % (name, unit) if unit else "%s(修正)" % name)
    return "帧序号," + ",".join(raw_cols + corrected_cols)


def main():
    import argparse

    parser = argparse.ArgumentParser(description="星网转台 hex 数据参考解析（Python）")
    parser.add_argument("src", help="输入 hex 文本文件")
    parser.add_argument("out", nargs="?", help="输出 CSV（可选）")
    parser.add_argument("--source", choices=("imu", "lins", "both"), default="imu",
                        help="惯导修正来源（默认 imu，与 C++ 默认行为一致）")
    args = parser.parse_args()

    t0 = time.time()
    rows, stats = parse(args.src, source=args.source)
    dt = (time.time() - t0) * 1000.0
    print("bytes=%d frames=%d errors=%d residual=%d elapsed=%.1fms source=%s" %
          (stats["bytes"], stats["frames"], stats["errors"], stats["residual"], dt,
           args.source))
    for idx, off, vals in rows[:3]:
        print("frame %d @byte %d: %s" % (idx, off, " ".join("%.2f" % v for v in vals)))
    if args.out:
        corrected = [k for k, (_, _, _, src, _, _) in enumerate(FIELDS) if src is not None]
        with open(args.out, "w", encoding="utf-8-sig") as f:
            f.write(csv_header() + "\n")
            for idx, off, vals in rows:
                fields = [str(idx)]
                fields += ["%.2f" % v for v in vals]
                fields += ["%.2f" % vals[k] for k in corrected]
                f.write(",".join(fields) + "\n")
        print("csv ->", args.out)


if __name__ == "__main__":
    main()
