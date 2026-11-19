# -*- coding: utf-8 -*-
"""前馈通道物理来源辨识：对 ωX/ωY/ωZ 做多元线性回归，判断每个“前馈速度”通道究竟对应哪个轴。"""

import sys
import os
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference_parse as rp  # noqa: E402

C = {n: i for i, (n, _, _, _) in enumerate(rp.FIELDS)}


def col(rows, i):
    return np.array([r[2][i] for r in rows], float)


def main():
    for tag, p in ((sys.argv[1], sys.argv[2]), (sys.argv[3], sys.argv[4]), (sys.argv[5], sys.argv[6])):
        rc, _ = rp.parse(p, correct=True)
        rr, _ = rp.parse(p, correct=False)
        n = len(rc)
        wx, wy, wz = col(rc, C["GYRO_RATE_X"]), col(rc, C["GYRO_RATE_Y"]), col(rc, C["GYRO_RATE_Z"])
        A = np.column_stack([wx, wy, wz, np.ones(n)])
        print("=" * 96)
        print("■ %s（帧数 %d）  前馈速度 ≈ a·ωX + b·ωY + c·ωZ + d" % (tag, n))
        print("   %-10s %9s %9s %9s %9s %8s %10s" %
              ("前馈通道", "a(ωX)", "b(ωY)", "c(ωZ)", "d", "R²", "1−R²残差σ"))
        for name, idx in (("方位前馈", C["AZ_FF"]), ("俯仰前馈", C["EL_FF"]), ("横滚前馈", C["RL_FF"])):
            y = col(rr, idx)
            coef, *_ = np.linalg.lstsq(A, y, rcond=None)
            pred = A @ coef
            ss_res = float(np.sum((y - pred) ** 2))
            ss_tot = float(np.sum((y - y.mean()) ** 2))
            r2 = 1 - ss_res / ss_tot if ss_tot > 0 else 0
            print("   %-10s %+9.3f %+9.3f %+9.3f %+9.3f %8.4f %10.3f" %
                  (name, coef[0], coef[1], coef[2], coef[3], r2, np.sqrt(ss_res / n)))

        # 修正后目标（=原始+惯导角度）是否恒定：判断该轴是否真的完成了稳定
        print("   修正前后目标/编码器波动（σ，单位°）：")
        for ax, t_i, e_i, g_i in (("方位AZ", C["AZ_TARGET"], C["AZ_ENCODER"], C["GYRO_ANG_X"]),
                                  ("俯仰EL", C["EL_TARGET"], C["EL_ENCODER"], C["GYRO_ANG_Y"]),
                                  ("横滚RL", C["RL_TARGET"], C["RL_ENCODER"], C["GYRO_ANG_Z"])):
            raw = col(rr, t_i)
            cor = col(rc, t_i)
            gyro = col(rc, g_i)
            enc = col(rc, e_i)
            print("      %-8s 原始目标σ=%7.3f  惯导角度σ=%7.3f  修正目标σ=%7.3f  编码器σ=%7.3f"
                  % (ax, raw.std(), gyro.std(), cor.std(), enc.std()))
        print()


if __name__ == "__main__":
    main()
