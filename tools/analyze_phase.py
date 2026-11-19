# -*- coding: utf-8 -*-
"""基波（0.1Hz）幅值与相位分析：定量给出前馈增益与相位差。

相位定义：前馈基波相位 - 目标基波相位
    0°   = 与目标位置同相（不是速度前馈）
   90°   = 超前位置 90°（正确的速度前馈相位）
  180°   = 与目标位置反相
  -90°  = 速度前馈但极性相反
"""

import sys
import os
import cmath
import math
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference_parse as rp  # noqa: E402

C = {n: i for i, (n, _, _, _) in enumerate(rp.FIELDS)}
PERIOD = 205.0  # 由自相关测量得到（三个文件一致）
F_EXC = 0.1     # 文件名给出的激励频率


def unw(v):
    return np.degrees(np.unwrap(np.radians(np.asarray(v, float))))


def col(rows, i):
    return np.array([r[2][i] for r in rows], float)


def fundamental(v):
    """返回基波复数幅值（幅值与相位）。"""
    n = len(v)
    x = v - v.mean()
    k0 = max(1, int(round(n / PERIOD)))
    s = sum(x[i] * cmath.exp(-2j * math.pi * k0 * i / n) for i in range(n))
    return 2 * s / n, k0


def main():
    print("激励 0.1Hz，周期 %.0f 帧 => 帧率 ≈ %.2f Hz\n" % (PERIOD, PERIOD * F_EXC))
    print("%-6s %-8s %10s %12s %12s %10s %10s" %
          ("文件", "轴", "目标幅值(°)", "前馈幅值(°/s)", "理想幅值(°/s)", "前馈增益", "相位差(°)"))
    for tag, p in ((sys.argv[1], sys.argv[2]), (sys.argv[3], sys.argv[4]), (sys.argv[5], sys.argv[6])):
        rc, _ = rp.parse(p, correct=True)
        rr, _ = rp.parse(p, correct=False)
        for ax, t_i, f_i in (("AZ", C["AZ_TARGET"], C["AZ_FF"]),
                             ("EL", C["EL_TARGET"], C["EL_FF"]),
                             ("RL", C["RL_TARGET"], C["RL_FF"])):
            ct, k0 = fundamental(unw(col(rr, t_i)))
            cf, _ = fundamental(col(rr, f_i))
            amp_t, amp_f = abs(ct), abs(cf)
            ideal = 2 * math.pi * F_EXC * amp_t
            phase = math.degrees(cmath.phase(cf / ct)) if abs(ct) > 1e-9 else 0.0
            print("%-6s %-8s %10.3f %12.3f %12.3f %10.2f %10.1f" %
                  (tag, ax, amp_t, amp_f, ideal, amp_f / ideal if ideal else 0, phase))
        print()

    # 惯导角速度基波幅值（对照）
    print("附：惯导角速度基波幅值 (°/s)")
    for tag, p in ((sys.argv[1], sys.argv[2]), (sys.argv[3], sys.argv[4]), (sys.argv[5], sys.argv[6])):
        rc, _ = rp.parse(p, correct=True)
        vals = []
        for i, nm in ((C["GYRO_RATE_X"], "ωX"), (C["GYRO_RATE_Y"], "ωY"), (C["GYRO_RATE_Z"], "ωZ")):
            cf, _ = fundamental(col(rc, i))
            vals.append("%s=%6.3f" % (nm, abs(cf)))
        print("   %-6s %s" % (tag, "  ".join(vals)))


if __name__ == "__main__":
    main()
