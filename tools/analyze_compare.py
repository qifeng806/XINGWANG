# -*- coding: utf-8 -*-
"""转台实测数据对比分析（输出 UTF-8 报告）。

用法: python tools/analyze_compare.py <输出报告> <文件1> [文件2 ...]
"""

import sys
import os
import math

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference_parse as rp  # noqa: E402

COLS = {name: i for i, (name, _, _, _) in enumerate(rp.FIELDS)}
AXES = [("方位AZ", COLS["AZ_TARGET"], COLS["AZ_ENCODER"], COLS["AZ_SPEED"], COLS["AZ_FF"], 0),
        ("俯仰EL", COLS["EL_TARGET"], COLS["EL_ENCODER"], COLS["EL_SPEED"], COLS["EL_FF"], 1),
        ("横滚RL", COLS["RL_TARGET"], COLS["RL_ENCODER"], COLS["RL_SPEED"], COLS["RL_FF"], 2)]
GYRO_ANG = [COLS["GYRO_ANG_X"], COLS["GYRO_ANG_Y"], COLS["GYRO_ANG_Z"]]
GYRO_RATE = [COLS["GYRO_RATE_X"], COLS["GYRO_RATE_Y"], COLS["GYRO_RATE_Z"]]


def unwrap(values):
    out, offset, prev = [], 0.0, None
    for v in values:
        if prev is not None:
            d = v - prev
            if d > 180:
                offset -= 360
            elif d < -180:
                offset += 360
        out.append(v + offset)
        prev = v
    return out


def stat(v):
    n = len(v)
    if not n:
        return (0.0, 0.0, 0.0, 0.0)
    m = sum(v) / n
    var = sum((x - m) ** 2 for x in v) / n
    return (m, math.sqrt(var), min(v), max(v))


def corr(a, b):
    n = min(len(a), len(b))
    if n < 2:
        return 0.0
    ma, mb = sum(a[:n]) / n, sum(b[:n]) / n
    va = sum((x - ma) ** 2 for x in a[:n])
    vb = sum((x - mb) ** 2 for x in b[:n])
    if va < 1e-12 or vb < 1e-12:
        return 0.0
    cov = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    return cov / math.sqrt(va * vb)


def best_lag(a, b, max_lag=25):
    """b 相对 a 的最佳滞后（帧），返回 (lag, corr)。lag>0 表示 b 滞后 a。"""
    best, best_c = 0, -2.0
    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            x, y = a[:len(a) - lag], b[lag:]
        else:
            x, y = a[-lag:], b[:len(b) + lag]
        c = corr(x, y)
        if c > best_c:
            best, best_c = lag, c
    return best, best_c


def main():
    out_path, files = sys.argv[1], sys.argv[2:]
    lines = []

    def w(s=""):
        lines.append(s)

    data = {}
    for path in files:
        rows_c, st = rp.parse(path, correct=True)
        rows_r, _ = rp.parse(path, correct=False)
        data[os.path.basename(path)] = (rows_c, rows_r, st)

    for name, (rows_c, rows_r, st) in data.items():
        n = len(rows_c)
        w("=" * 104)
        w("■ 文件：%s" % name)
        w("  帧数=%d（原始字节 %d）  错误帧=%d  残帧字节=%d" %
          (n, st["bytes"], st["errors"], st["residual"]))
        if n == 0:
            continue
        w("")
        w("  ── 跟踪误差对比（原始目标 / 惯导修正后目标 分别与编码器比较） ──")
        w("  %-7s %10s %10s %10s | %10s %10s | %8s %8s | %6s %6s" %
          ("轴", "原始目标σ", "修正目标σ", "编码器σ", "误差RMS(原始)", "误差RMS(修正)",
           "相关(原始)", "相关(修正)", "滞后帧", "滞后帧"))
        for axis, t_i, e_i, s_i, f_i, gi in AXES:
            t_raw = unwrap([r[2][t_i] for r in rows_r])
            t_cor = unwrap([r[2][t_i] for r in rows_c])
            enc = unwrap([r[2][e_i] for r in rows_c])
            e_raw = [a - b for a, b in zip(t_raw, enc)]
            e_cor = [a - b for a, b in zip(t_cor, enc)]
            lr, _ = best_lag(t_raw, enc)
            lc, _ = best_lag(t_cor, enc)
            w("  %-7s %10.3f %10.3f %10.3f | %10.3f %10.3f | %8.3f %8.3f | %6d %6d" %
              (axis, stat(t_raw)[1], stat(t_cor)[1], stat(enc)[1],
               math.sqrt(sum(x * x for x in e_raw) / n), math.sqrt(sum(x * x for x in e_cor) / n),
               corr(t_raw, enc), corr(t_cor, enc), lr, lc))
            w("          原始目标 %8.2f~%8.2f  修正目标 %8.2f~%8.2f  编码器 %8.2f~%8.2f" %
              (stat(t_raw)[2], stat(t_raw)[3], stat(t_cor)[2], stat(t_cor)[3],
               stat(enc)[2], stat(enc)[3]))
        w("")
        w("  ── 前馈速度 / 惯导角速度（原始 = 下位机上报值，修正 = 原始 + 对应轴惯导角速度） ──")
        w("  %-7s %12s %12s %12s %12s" % ("轴", "前馈raw σ", "前馈cor σ", "惯导ω σ", "运行速度 σ"))
        for axis, t_i, e_i, s_i, f_i, gi in AXES:
            ff_raw = [r[2][f_i] for r in rows_r]
            ff_cor = [r[2][f_i] for r in rows_c]
            gyro = [r[2][GYRO_RATE[gi]] for r in rows_c]
            spd = [r[2][s_i] for r in rows_c]
            w("  %-7s %12.3f %12.3f %12.3f %12.3f" %
              (axis, stat(ff_raw)[1], stat(ff_cor)[1], stat(gyro)[1], stat(spd)[1]))
            w("          前馈raw %8.2f~%8.2f  前馈cor %8.2f~%8.2f  惯导ω %8.2f~%8.2f" %
              (stat(ff_raw)[2], stat(ff_raw)[3], stat(ff_cor)[2], stat(ff_cor)[3],
               stat(gyro)[2], stat(gyro)[3]))
        w("")

    # ── 横向对比：同一轴在不同文件中的表现 ──
    w("=" * 104)
    w("■ 横向对比")
    names = list(data.keys())
    w("  %-46s %8s %10s %10s %10s %10s %10s" %
      ("文件", "帧数", "方位Δσ", "俯仰Δσ", "横滚Δσ", "方位前馈σ", "惯导ωXσ"))
    for name in names:
        rows_c, rows_r, st = data[name]
        if not rows_c:
            continue
        vals = []
        for axis, t_i, e_i, s_i, f_i, gi in AXES:
            t_raw = unwrap([r[2][t_i] for r in rows_r])
            enc = unwrap([r[2][e_i] for r in rows_c])
            vals.append(stat([a - b for a, b in zip(t_raw, enc)])[1])
        ff = stat([r[2][COLS["AZ_FF"]] for r in rows_r])[1]
        gx = stat([r[2][COLS["GYRO_RATE_X"]] for r in rows_c])[1]
        w("  %-46s %8d %10.3f %10.3f %10.3f %10.3f %10.3f" %
          (name[:44], len(rows_c), vals[0], vals[1], vals[2], ff, gx))

    open(out_path, "w", encoding="utf-8").write("\n".join(lines))
    print("报告已写入:", out_path)


if __name__ == "__main__":
    main()
