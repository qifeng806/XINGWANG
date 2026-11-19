# -*- coding: utf-8 -*-
"""通道相似性排查：检测字段重复写入 / 错位（例如把 EL_GyroSpeed 写进横滚速度字段）。

对 19 个通道两两计算相关系数与“完全相同帧占比”，找出可疑的重复字段。
"""

import sys
import os
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference_parse as rp  # noqa: E402

NAMES = [n for (n, _, _, _) in rp.FIELDS]


def col(rows, i):
    return np.array([r[2][i] for r in rows], float)


def main():
    for tag, p in ((sys.argv[1], sys.argv[2]), (sys.argv[3], sys.argv[4]), (sys.argv[5], sys.argv[6])):
        rc, _ = rp.parse(p, correct=True)
        rr, _ = rp.parse(p, correct=False)
        n = len(rc)
        cols = [col(rr, i) for i in range(len(NAMES))]
        print("=" * 92)
        print("■ %s（帧数 %d）通道相似性（原始值）" % (tag, n))
        print("%-14s %-14s %8s %10s %10s" % ("通道A", "通道B", "相关系数", "完全相同%", "最大差"))
        found = False
        for i in range(len(NAMES)):
            for j in range(i + 1, len(NAMES)):
                a, b = cols[i], cols[j]
                if a.std() < 1e-9 or b.std() < 1e-9:
                    continue
                c = float(np.corrcoef(a, b)[0, 1])
                same = float(np.mean(np.abs(a - b) < 1e-9)) * 100
                maxd = float(np.max(np.abs(a - b)))
                if same > 5.0 or abs(c) > 0.995:
                    print("%-14s %-14s %8.4f %9.1f%% %10.3f" % (NAMES[i], NAMES[j], c, same, maxd))
                    found = True
        if not found:
            print("   （未发现完全相同或高度重复的通道）")

        # 重点怀疑项：原始代码横滚段误用 EL_GyroSpeed
        idx = {n: i for i, n in enumerate(NAMES)}
        for a, b in (("RL_SPEED", "EL_FF"), ("RL_FF", "EL_FF"), ("RL_FF", "AZ_FF"),
                     ("RL_SPEED", "RL_FF"), ("RL_SPEED", "AZ_SPEED"), ("RL_FF", "GYRO_RATE_Z"),
                     ("AZ_FF", "GYRO_RATE_X"), ("AZ_FF", "AZ_SPEED"), ("EL_FF", "GYRO_RATE_Y")):
            x, y = cols[idx[a]], cols[idx[b]]
            c = float(np.corrcoef(x, y)[0, 1]) if x.std() > 0 and y.std() > 0 else 0.0
            k = float(np.polyfit(y, x, 1)[0]) if y.std() > 0 else 0.0
            print("   重点：%-12s vs %-12s corr=%+7.4f  回归斜率=%+7.3f" % (a, b, c, k))
        print()


if __name__ == "__main__":
    main()
