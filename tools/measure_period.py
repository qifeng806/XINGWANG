# -*- coding: utf-8 -*-
"""测量各通道真实周期（自相关）与峰值间隔，用于确定帧率与激励频率。"""

import sys
import os
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference_parse as rp  # noqa: E402

C = {n: i for i, (n, _, _, _) in enumerate(rp.FIELDS)}


def unw(v):
    return np.degrees(np.unwrap(np.radians(np.asarray(v, float))))


def col(rows, i):
    return np.array([r[2][i] for r in rows], float)


def ac_period(v):
    v = v - np.mean(v)
    ac = np.correlate(v, v, "full")[len(v) - 1:]
    ac = ac / ac[0]
    lo, hi = 40, min(420, len(v) // 2)
    k = int(np.argmax(ac[lo:hi])) + lo
    return k, ac[k]


def peak_period(v):
    """相邻峰值平均间隔。"""
    idx = [i for i in range(1, len(v) - 1) if v[i] > v[i - 1] and v[i] >= v[i + 1] and v[i] > np.mean(v)]
    if len(idx) < 2:
        return 0
    return int(round(np.mean(np.diff(idx))))


def main():
    for tag, p in ((sys.argv[1], sys.argv[2]), (sys.argv[3], sys.argv[4]), (sys.argv[5], sys.argv[6])):
        rc, _ = rp.parse(p, correct=True)
        rr, _ = rp.parse(p, correct=False)
        print("== 文件 %s  帧数 %d" % (tag, len(rc)))
        for ax, ti in (("方位目标", C["AZ_TARGET"]), ("俯仰目标", C["EL_TARGET"]),
                       ("横滚目标", C["RL_TARGET"])):
            v = unw(col(rr, ti))
            k, r = ac_period(v)
            print("   %-8s 自相关周期 %5d 帧 (r=%.3f)   峰值间隔 %5d 帧   幅值 %6.2f°"
                  % (ax, k, r, peak_period(v), (v.max() - v.min()) / 2))
        for ax, ti in (("方位前馈", C["AZ_FF"]), ("俯仰前馈", C["EL_FF"]), ("横滚前馈", C["RL_FF"])):
            v = col(rr, ti)
            k, r = ac_period(v)
            print("   %-8s 自相关周期 %5d 帧 (r=%.3f)   峰值间隔 %5d 帧   幅值 %6.2f°/s"
                  % (ax, k, r, peak_period(v), (v.max() - v.min()) / 2))
        for ax, ti in (("惯导ωX", C["GYRO_RATE_X"]), ("惯导ωY", C["GYRO_RATE_Y"]),
                       ("惯导ωZ", C["GYRO_RATE_Z"])):
            v = col(rc, ti)
            k, r = ac_period(v)
            print("   %-8s 自相关周期 %5d 帧 (r=%.3f)   峰值间隔 %5d 帧   幅值 %6.2f°/s"
                  % (ax, k, r, peak_period(v), (v.max() - v.min()) / 2))
        print()


if __name__ == "__main__":
    main()
